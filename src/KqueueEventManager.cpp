// ekam -- http://code.google.com/p/ekam
// Copyright (c) 2010 Kenton Varda and contributors.  All rights reserved.
// Portions copyright Google, Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of the ekam project nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "KqueueEventManager.h"

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <sys/time.h>
#include <algorithm>
#include <stdexcept>
#include <assert.h>
#include <string.h>

#include "Debug.h"

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

void KqueueEventManager::runAsynchronously(Callback* callback, OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<AsyncCallbackHandler>(this, callback);
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

void KqueueEventManager::onProcessExit(pid_t pid,
                                       ProcessExitCallback* callback,
                                       OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<ProcessExitHandler>(this, pid, callback);
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

void KqueueEventManager::onReadable(int fd, IoCallback* callback,
                                    OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<ReadHandler>(this, fd, callback);
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

void KqueueEventManager::onWritable(int fd, IoCallback* callback,
                                    OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<WriteHandler>(this, fd, callback);
}

// =======================================================================================

void newPreferredEventManager(OwnedPtr<RunnableEventManager>* output) {
  output->allocateSubclass<KqueueEventManager>();
}

}  // namespace ekam
