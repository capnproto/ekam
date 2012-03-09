// Kenton's Code Playground -- http://code.google.com/p/kentons-code
// Author: Kenton Varda (temporal@gmail.com)
// Copyright (c) 2010 Google, Inc. and contributors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "KqueueEventManager.h"

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <algorithm>
#include <stdexcept>
#include <assert.h>
#include <string.h>

#include "base/Debug.h"

namespace ekam {

class KqueueEventManager::KEventHandler : public AsyncOperation {
public:
  virtual ~KEventHandler() {}

  virtual void handle(const KEvent& event) = 0;
};

class KqueueEventManager::AsyncCallbackHandler : public AsyncOperation {
public:
  AsyncCallbackHandler(KqueueEventManager* eventManager, Callback* callback)
      : eventManager(eventManager), called(false), callback(callback) {
    eventManager->asyncCallbacks.push_back(this);
  }
  ~AsyncCallbackHandler() {
    if (!called) {
      for (std::deque<AsyncCallbackHandler*>::iterator iter = eventManager->asyncCallbacks.begin();
           iter != eventManager->asyncCallbacks.end(); ++iter) {
        if (*iter == this) {
          eventManager->asyncCallbacks.erase(iter);
          return;
        }
      }
      DEBUG_ERROR << "AsyncCallbackHandler not called but not in asyncCallbacks.";
    }
  }

  void run() {
    called = true;
    callback->run();
  }

private:
  KqueueEventManager* eventManager;
  bool called;
  Callback* callback;
};

// =======================================================================================

KqueueEventManager::KqueueEventManager()
    : kqueueFd(kqueue()), handlerCount(0) {
  if (kqueueFd < 0) {
    std::string error(strerror(errno));
    throw std::runtime_error("kqueue: " + error);
  }
}

KqueueEventManager::~KqueueEventManager() {
  if (close(kqueueFd) < 0) {
    DEBUG_ERROR << "close(kqueue): " << strerror(errno);
  }
  kqueueFd = -1;
}

void KqueueEventManager::loop() {
  while (handleEvent()) {}
}

bool KqueueEventManager::handleEvent() {
  // Run any async callbacks first.
  // TODO:  Avoid starvation of the kqueue?  Probably doesn't matter.
  if (!asyncCallbacks.empty()) {
    AsyncCallbackHandler* handler = asyncCallbacks.front();
    asyncCallbacks.pop_front();
    handler->run();
    return true;
  }

  if (handlerCount == 0) {
    DEBUG_INFO << "No more events.";
    return false;
  }

  DEBUG_INFO << "Waiting for events...";

  struct kevent event;
  int n;
  if (fakeEvents.empty()) {
    n = kevent(kqueueFd, NULL, 0, &event, 1, NULL);
    DEBUG_INFO << "Received event.";
  } else {
    n = 1;
    event = fakeEvents.front();
    fakeEvents.pop_front();
    DEBUG_INFO << "Received fake event.";
  }

  if (n < 0) {
    DEBUG_ERROR << "kevent: " << strerror(errno);
  } else if (n == 0) {
    DEBUG_ERROR << "kevent() timed out, but timeout was infinite.";
  } else {
    if (n > 1) {
      DEBUG_ERROR << "kevent() returned more events than requested.";
    }

    KEventHandler* handler = reinterpret_cast<KEventHandler*>(event.udata);
    handler->handle(event);
  }

  return true;
}

void KqueueEventManager::updateKqueue(const KEvent& event) {
  if (kqueueFd == -1) {
    // We're in KqueueEventManager's destructor and the kqueue has already been closed.
    if (event.flags != EV_DELETE) {
      DEBUG_ERROR << "Tried to add events to kqueue after it was closed.";
    }
    return;
  }

  int n = kevent(kqueueFd, &event, 1, NULL, 0, NULL);
  if (n < 0) {
    if (event.filter == EVFILT_PROC && errno == ESRCH) {
      // HACK HACK HACK
      // Child process may have already exited and is now a zombie.  Unfortunately, kevent()
      // rejects this instead of doing the obvious thing and producing an exit event immediately.
      // So we must fake it.
      // NOTE:  I have only observed this happening on OSX, not FreeBSD.
      DEBUG_INFO << "Child exited before we could wait?  PID: " << event.ident;
      KEvent fakeEvent = event;
      fakeEvent.data = -1;  // We'll have to get the status from wait() later.
      fakeEvents.push_back(fakeEvent);
    } else {
      throw std::runtime_error("kevent: " + std::string(strerror(errno)));
    }
  } else if (n > 0) {
    DEBUG_ERROR << "kevent() returned events when not asked to.";
  }
}

void KqueueEventManager::updateKqueue(uintptr_t ident, short filter, u_short flags,
                                      KEventHandler* handler, u_int fflags, intptr_t data) {
  KEvent event;
  EV_SET(&event, ident, filter, flags, fflags, data, handler);
  updateKqueue(event);
}

// =======================================================================================

OwnedPtr<AsyncOperation> KqueueEventManager::runAsynchronously(Callback* callback) {
  return newOwned<AsyncCallbackHandler>(this, callback);
}

// =======================================================================================

class KqueueEventManager::ProcessExitHandler : public KEventHandler {
public:
  ProcessExitHandler(KqueueEventManager* eventManager, pid_t pid, ProcessExitCallback* callback)
      : eventManager(eventManager), pid(pid), exited(false), callback(callback) {
    eventManager->updateKqueue(pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, this, NOTE_EXIT);
    ++eventManager->handlerCount;
  }
  ~ProcessExitHandler() {
    if (!exited) {
      --eventManager->handlerCount;
      eventManager->updateKqueue(pid, EVFILT_PROC, EV_DELETE);

      // Also make sure there are no dangling fake events targeting us.
      for (std::deque<KEvent>::iterator iter = eventManager->fakeEvents.begin();
           iter != eventManager->fakeEvents.end(); ++iter) {
        if (iter->udata == this) {
          eventManager->fakeEvents.erase(iter);
          break;
        }
      }
    }
  }

  // implements KEventHandler ------------------------------------------------------------
  void handle(const KEvent& event) {
    if ((event.fflags & NOTE_EXIT) == 0) {
      DEBUG_ERROR << "EVFILT_PROC kevent had unexpected fflags: " << event.fflags;
      return;
    }

    exited = true;
    --eventManager->handlerCount;

    // Clean up zombie child.
    // Note that we could set signal(SIGCHLD, SIG_IGN) to tell the system not to create zombies
    // in the first place.  This does not prevent kqueue() from receiving EV_PROC notifications.
    // However, this could cause a problem if a child process exited before we registered it with
    // the kqueue.  See hack in updateKqueue() which deals with exactly this.  Note also that
    // we CANNOT use the exit status from event.data because of said hack.
    int waitStatus;
    if (waitpid(pid, &waitStatus, 0) != pid) {
      DEBUG_ERROR << "waitpid: " << strerror(errno);
    }

    DEBUG_INFO << "Process " << pid << " exited with status: " << waitStatus;

    if (WIFEXITED(waitStatus)) {
      callback->exited(WEXITSTATUS(waitStatus));
    } else if (WIFSIGNALED(waitStatus)) {
      callback->signaled(WTERMSIG(waitStatus));
    } else {
      DEBUG_ERROR << "Didn't understand process exit status.";
      callback->exited(-1);
    }
    // WARNING:  May have been deleted!
  }

private:
  KqueueEventManager* eventManager;
  pid_t pid;
  bool exited;
  ProcessExitCallback* callback;
};

OwnedPtr<AsyncOperation> KqueueEventManager::onProcessExit(
    pid_t pid, ProcessExitCallback* callback) {
  return newOwned<ProcessExitHandler>(this, pid, callback);
}

// =======================================================================================

class KqueueEventManager::ReadHandler : public KEventHandler {
public:
  ReadHandler(KqueueEventManager* eventManager, int fd, IoCallback* callback)
      : eventManager(eventManager), fd(fd), callback(callback) {
    eventManager->updateKqueue(fd, EVFILT_READ, EV_ADD, this);
    deleted.allocate(false);
    ++eventManager->handlerCount;
  }
  ~ReadHandler() {
    --eventManager->handlerCount;
    eventManager->updateKqueue(fd, EVFILT_READ, EV_DELETE);
    *deleted = true;
  }

  // implements KEventHandler ------------------------------------------------------------
  void handle(const KEvent& event) {
    SmartPtr<bool> deleted = this->deleted;

    DEBUG_INFO << "FD is readable: " << fd;
    callback->ready();

    if (event.flags & EV_EOF) {
      // kqueue will not return this event again, so keep calling the callback until it gets the
      // message.
      while (!*deleted) {
        callback->ready();
      }
    }
  }

private:
  KqueueEventManager* eventManager;
  int fd;
  IoCallback* callback;
  SmartPtr<bool> deleted;
};

OwnedPtr<AsyncOperation> KqueueEventManager::onReadable(int fd, IoCallback* callback) {
  return newOwned<ReadHandler>(this, fd, callback);
}

// =======================================================================================

class KqueueEventManager::WriteHandler : public KEventHandler {
public:
  WriteHandler(KqueueEventManager* eventManager, int fd, IoCallback* callback)
      : eventManager(eventManager), fd(fd), callback(callback) {
    eventManager->updateKqueue(fd, EVFILT_WRITE, EV_ADD, this);
    ++eventManager->handlerCount;
  }
  ~WriteHandler() {
    --eventManager->handlerCount;
    eventManager->updateKqueue(fd, EVFILT_WRITE, EV_DELETE);
  }

  // implements KEventHandler ------------------------------------------------------------
  void handle(const KEvent& event) {
    DEBUG_INFO << "FD is writable: " << fd;
    callback->ready();
  }

private:
  KqueueEventManager* eventManager;
  int fd;
  IoCallback* callback;
};

OwnedPtr<AsyncOperation> KqueueEventManager::onWritable(int fd, IoCallback* callback) {
  return newOwned<WriteHandler>(this, fd, callback);
}

// =======================================================================================

class KqueueEventManager::FileChangeHandler : public KEventHandler {
public:
  FileChangeHandler(KqueueEventManager* eventManager, const std::string& filename,
                    FileChangeCallback* callback)
      : eventManager(eventManager), callback(callback) {
    fd = open(filename.c_str(), O_RDONLY);
    if (fd >= 0) {
      eventManager->updateKqueue(fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, this,
                                 NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND |
                                 NOTE_RENAME | NOTE_REVOKE);
      ++eventManager->handlerCount;

      DEBUG_INFO << "Monitoring changes on " << fd << " for: " << filename;
    } else {
      DEBUG_ERROR << "Cannot monitor changes: " << filename << ": " << strerror(errno);
    }
  }
  ~FileChangeHandler() {
    if (fd >= 0) {
      cleanup();
    }
  }

  // implements KEventHandler ------------------------------------------------------------
  void handle(const KEvent& event) {
    DEBUG_INFO << "File change on " << fd << ":"
               << ((event.fflags & NOTE_DELETE) ? " NOTE_DELETE" : "")
               << ((event.fflags & NOTE_WRITE ) ? " NOTE_WRITE"  : "")
               << ((event.fflags & NOTE_EXTEND) ? " NOTE_EXTEND" : "")
               << ((event.fflags & NOTE_RENAME) ? " NOTE_RENAME" : "")
               << ((event.fflags & NOTE_REVOKE) ? " NOTE_REVOKE" : "");

    if (event.fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) {
      // In the rename case, the kqueue will continue notifying us of changes to the file in its
      // new location, but we don't want that.  So, call cleanup() to unregister the event.  Do
      // this before calling the callback since the callback may delete the handler.
      cleanup();
      callback->deleted();
    } else {
      callback->modified();
    }
  }

private:
  KqueueEventManager* eventManager;
  int fd;
  FileChangeCallback* callback;

  void cleanup() {
    --eventManager->handlerCount;
    eventManager->updateKqueue(fd, EVFILT_VNODE, EV_DELETE);

    if (close(fd) < 0) {
      DEBUG_ERROR << "close: " << strerror(errno);
    }
    fd = -1;
  }
};

OwnedPtr<AsyncOperation> KqueueEventManager::onFileChange(const std::string& filename,
                                                          FileChangeCallback* callback) {
  return newOwned<FileChangeHandler>(this, filename, callback);
}

// =======================================================================================

OwnedPtr<RunnableEventManager> newPreferredEventManager() {
  return newOwned<KqueueEventManager>();
}

}  // namespace ekam
