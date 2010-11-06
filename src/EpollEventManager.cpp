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

#include "EpollEventManager.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/inotify.h>
#include <algorithm>
#include <stdexcept>
#include <assert.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <limits.h>

#include "Debug.h"
#include "Table.h"
#include "DiskFile.h"
#include "OsHandle.h"

namespace ekam {

namespace {

// TODO:  This could be somewhat more object-oriented (but only somewhat -- these are signals,
//   after all).

bool initialized = false;
sigset_t handledSignals;

static sigjmp_buf sigJumpBuf;
siginfo_t* saveSignalTo = NULL;

static const int SIGPOLL_ERROR = -1;
static const int SIGPOLL_SIGNALED = -2;

void signalHandler(int number, siginfo_t* info, void* context) {
  DEBUG_INFO << "Got signal: " << number;
  *saveSignalTo = *info;
  siglongjmp(sigJumpBuf, 1);
}

void initSignalHandler(int number) {
  DEBUG_INFO << "Registering signal: " << number;

  if (sigaddset(&handledSignals, number) < 0) {
    DEBUG_ERROR << "sigaddset: " << strerror(errno);
  }
  if (sigprocmask(SIG_BLOCK, &handledSignals, NULL) < 0) {
    DEBUG_ERROR << "sigprocmask: " << strerror(errno);
  }

  struct sigaction action;

  action.sa_sigaction = &signalHandler;
  action.sa_flags = SA_SIGINFO;

  if (number == SIGCHLD) {
    // The siginfo_t contains all the info we need, so let the child process be reaped
    // immediately.
    // We only want to know if the child exits, not if it stops.
    action.sa_flags |= SA_NOCLDSTOP;

    // RANT(kenton):  The siginfo_t actually contains the exit status.  Ideally we'd just use
    //   that and not call wait().  However, if we don't call wait(), we leak a zombie process.
    //   Unless, of course, we set SA_NOCLDWAIT, which tells the system that we're never going
    //   to call wait() so it shouldn't bother creating zombies.  Just one problem with that:
    //   on some systems, SA_NOCLDWAIT also causes SIGCHLD not to be sent!  WTF?  This behavior
    //   defeats the whole purpose of SA_NOCLDWAIT -- it becomes effectively equivalent to
    //   calling signal(SIGCHLD, SIG_IGN).  Argh.

    // RANT(kenton):  The above rant, of course, is completely irrelevant if we're waiting on
    //   more than one child at a time, because if multiple SIGCHLDs are received while the signal
    //   is blocked, only ONE will actually be delivered when unblocked.  So you *must* call wait()
    //   otherwise you might miss a child.  Signals are stupid.  Weirdly, on FreeBSD, I never
    //   actually encountered this issue, whereas it happened immediately on OSX and Linux.
  }

  // Block all signals in handler.
  if (sigfillset(&action.sa_mask) < 0) {
    DEBUG_ERROR << "sigfillset: " << strerror(errno);
  }

  if (sigaction(number, &action, NULL) < 0) {
    DEBUG_ERROR << "sigaction: " << strerror(errno);
  }
}

void initSignalHandling() {
  if (!initialized) {
    sigemptyset(&handledSignals);
    initSignalHandler(SIGCHLD);
    initialized = true;
  }
}

int sigPoll(struct pollfd pfd[], nfds_t nfds, int timeout, siginfo_t* siginfo) {
  saveSignalTo = siginfo;

  if (sigsetjmp(sigJumpBuf, true) != 0) {
    // Got a signal.
    return SIGPOLL_SIGNALED;
  }

  sigprocmask(SIG_UNBLOCK, &handledSignals, NULL);
  int result = poll(pfd, nfds, timeout);
  sigprocmask(SIG_BLOCK, &handledSignals, NULL);
  return result < 0 ? SIGPOLL_ERROR : result;
}

bool sigWait(siginfo_t* siginfo) {
  saveSignalTo = siginfo;

  if (sigsetjmp(sigJumpBuf, true) != 0) {
    // Got a signal.
    return true;
  }

  sigset_t sigset;
  sigemptyset(&sigset);
  sigsuspend(&sigset);

  // Must have received a signal not handled by us.
  return false;
}

}  // namespace

// =======================================================================================

EpollEventManager::EpollEventManager() {
  initSignalHandling();
  inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
}

EpollEventManager::~EpollEventManager() {
  if (close(inotifyFd) != 0) {
    DEBUG_ERROR << "close(inotifyFd): " << strerror(errno);
  }
}

class EpollEventManager::IoHandler {
public:
  virtual ~IoHandler() {}

  virtual void handle(short pollFlags) = 0;
};

// =======================================================================================

class EpollEventManager::AsyncCallbackHandler : public AsyncOperation {
public:
  AsyncCallbackHandler(EpollEventManager* eventManager, Callback* callback)
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
  EpollEventManager* eventManager;
  bool called;
  Callback* callback;
};

void EpollEventManager::runAsynchronously(Callback* callback, OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<AsyncCallbackHandler>(this, callback);
}

// =======================================================================================

class EpollEventManager::ProcessExitHandler : public AsyncOperation {
public:
  ProcessExitHandler(EpollEventManager* eventManager, pid_t pid,
                     ProcessExitCallback* callback)
      : eventManager(eventManager), pid(pid), callback(callback) {
    if (!eventManager->processExitHandlerMap.insert(std::make_pair(pid, this)).second) {
      throw std::runtime_error("Already waiting on this process.");
    }
  }
  ~ProcessExitHandler() {
    eventManager->processExitHandlerMap.erase(pid);
  }

  void handle(int waitStatus) {
    DEBUG_INFO << "Process " << pid << " exited with status: " << waitStatus;

    if (WIFEXITED(waitStatus)) {
      callback->exited(WEXITSTATUS(waitStatus));
    } else if (WIFSIGNALED(waitStatus)) {
      callback->signaled(WTERMSIG(waitStatus));
    } else {
      DEBUG_ERROR << "Didn't understand process exit status.";
      callback->exited(-1);
    }
  }

private:
  EpollEventManager* eventManager;
  pid_t pid;
  ProcessExitCallback* callback;
};

void EpollEventManager::onProcessExit(pid_t pid,
                                      ProcessExitCallback* callback,
                                      OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<ProcessExitHandler>(this, pid, callback);
}

// =======================================================================================

class EpollEventManager::ReadHandler : public AsyncOperation, public IoHandler {
public:
  ReadHandler(EpollEventManager* eventManager, int fd, IoCallback* callback)
      : eventManager(eventManager), fd(fd), callback(callback) {
    if (!eventManager->readHandlerMap.insert(std::make_pair(fd, this)).second) {
      throw std::runtime_error("Already waiting for readability on this file descriptor.");
    }
  }
  ~ReadHandler() {
    eventManager->readHandlerMap.erase(fd);
  }

  void handle(short pollFlags) {
    if (pollFlags & POLLIN) {
      DEBUG_INFO << "FD is readable: " << fd;
    } else if (pollFlags & POLLERR) {
      DEBUG_INFO << "FD has error: " << fd;
    } else if (pollFlags & POLLHUP) {
      DEBUG_INFO << "FD hung up: " << fd;
    } else {
      DEBUG_ERROR << "ReadHandler should only get POLLIN, POLLERR, or POLLHUP events.";
      return;
    }

    callback->ready();
  }

private:
  EpollEventManager* eventManager;
  int fd;
  IoCallback* callback;
};

void EpollEventManager::onReadable(int fd, IoCallback* callback, OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<ReadHandler>(this, fd, callback);
}

// =======================================================================================

class EpollEventManager::WriteHandler : public AsyncOperation, public IoHandler {
public:
  WriteHandler(EpollEventManager* eventManager, int fd, IoCallback* callback)
      : eventManager(eventManager), fd(fd), callback(callback) {
    if (!eventManager->writeHandlerMap.insert(std::make_pair(fd, this)).second) {
      throw std::runtime_error("Already waiting for writability on this file descriptor.");
    }
  }
  ~WriteHandler() {
    eventManager->writeHandlerMap.erase(fd);
  }

  void handle(short pollFlags) {
    if (pollFlags & POLLOUT) {
      DEBUG_INFO << "FD is writable: " << fd;
    } else if (pollFlags & POLLERR) {
      DEBUG_INFO << "FD has error: " << fd;
    } else {
      DEBUG_ERROR << "WriteHandler should only get POLLOUT or POLLERR events.";
      return;
    }

    callback->ready();
  }

private:
  EpollEventManager* eventManager;
  int fd;
  IoCallback* callback;
};

void EpollEventManager::onWritable(int fd, IoCallback* callback, OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<WriteHandler>(this, fd, callback);
}

// =======================================================================================

class EpollEventManager::WatchedDirectory {
  class CallbackTable : public Table<IndexedColumn<std::string>,
                                     UniqueColumn<WatchOperation*> > {
  public:
    static const int BASENAME = 0;
    static const int WATCH_OP = 1;
  };

public:
  WatchedDirectory(EpollEventManager* eventManager, const std::string& path)
      : eventManager(eventManager), path(path), currentlyHandling(NULL) {
    wd = inotify_add_watch(eventManager->inotifyFd, path.c_str(),
                           IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF |
                           IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) {
      throw OsError(path, "inotify_add_watch", errno);
    } else {
      DEBUG_INFO << "inotify_add_watch(" << path << ") [" << wd << "]";
      eventManager->watchMap[wd] = this;
      eventManager->watchByNameMap[path] = this;
    }
  }

  ~WatchedDirectory() {
    DEBUG_INFO << "~WatchedDirectory(): " << path;

    if (callbackTable.size() > 0) {
      DEBUG_ERROR << "Deleting WatchedDirectory before all WatchOperations were removed.";
    }

    if (wd >= 0) {
      DEBUG_INFO << "inotify_rm_watch(" << path << ") [" << wd << "]";

      if (inotify_rm_watch(eventManager->inotifyFd, wd) < 0) {
        DEBUG_ERROR << "inotify_rm_watch(" << path << "): " << strerror(errno);
      }
    }

    invalidate();

    // Make sure that if this WatchedDirectory has a pending event, that event is ignored.
    eventManager->currentlyHandlingWatches.erase(this);
  }

  void addWatch(const std::string& basename, WatchOperation* op) {
    DEBUG_INFO << "Watch directory " << path << " now covering: " << basename;
    callbackTable.add(basename, op);
  }

  void removeWatch(WatchOperation* op) {
    DEBUG_INFO << "Watch directory " << path << " no longer covering: " << basenameForOp(op);
    if (callbackTable.erase<CallbackTable::WATCH_OP>(op) == 0) {
      DEBUG_ERROR << "Trying to remove watch that was never added.";
    }
    if (currentlyHandling == NULL) {
      deleteSelfIfEmpty();
    } else {
      // Cancel any pending event callback.
      currentlyHandling->erase(op);
    }
  }

  void invalidate() {
    if (wd >= 0) {
      eventManager->watchMap.erase(wd);
      eventManager->watchByNameMap.erase(path);
      wd = -1;
    }
  }

  void handle(struct inotify_event* event);

private:
  EpollEventManager* eventManager;
  int wd;
  std::string path;

  CallbackTable callbackTable;

  std::set<WatchOperation*>* currentlyHandling;

  void deleteSelfIfEmpty() {
    if (callbackTable.size() == 0) {
      eventManager->ownedWatchDirectories.erase(this);
    }
  }

  std::string basenameForOp(WatchOperation* op) {
    const CallbackTable::Row* row = callbackTable.find<CallbackTable::WATCH_OP>(op);
    if (row == NULL) {
      return "(invalid)";
    } else {
      return row->cell<CallbackTable::BASENAME>();
    }
  }
};

class EpollEventManager::WatchOperation : public AsyncOperation {
public:
  WatchOperation(EpollEventManager* eventManager, const std::string& filename,
                 FileChangeCallback* callback)
      : callback(callback), watchedDirectory(NULL), modified(false), deleted(false) {
    // Split directory and basename.
    std::string directory;
    std::string basename;
    if (DiskFile(filename, NULL).isDirectory()) {
      directory = filename;
    } else {
      std::string::size_type slashPos = filename.find_last_of('/');
      if (slashPos == std::string::npos) {
        directory.assign(".");
        basename.assign(filename);
      } else {
        directory.assign(filename, 0, slashPos);
        basename.assign(filename, slashPos + 1, std::string::npos);
      }
    }

    // Find or create WatchedDirectory object.
    WatchByNameMap::iterator iter = eventManager->watchByNameMap.find(directory);
    if (iter == eventManager->watchByNameMap.end()) {
      OwnedPtr<WatchedDirectory> newWatchedDirectory;
      newWatchedDirectory.allocate(eventManager, directory);
      watchedDirectory = newWatchedDirectory.get();
      eventManager->ownedWatchDirectories.adopt(watchedDirectory, &newWatchedDirectory);
    } else {
      watchedDirectory = iter->second;
    }

    // Add to WatchedDirectory.
    watchedDirectory->addWatch(basename, this);
  }

  ~WatchOperation() {
    if (watchedDirectory != NULL) {
      watchedDirectory->removeWatch(this);
    }
  }

  void flagAsModified() {
    modified = true;
  }

  void flagAsDeleted() {
    deleted = true;
  }

  // Call the callback based on previous flagsAs*() calls.
  void callCallback() {
    if (deleted) {
      deleted = false;
      modified = false;
      watchedDirectory->removeWatch(this);
      watchedDirectory = NULL;
      callback->deleted();
    } else if (modified) {
      modified = false;
      callback->modified();
    }
    // WARNING:  "this" may have been destroyed.
  }

private:
  FileChangeCallback* callback;
  WatchedDirectory* watchedDirectory;
  bool modified;
  bool deleted;
};

void EpollEventManager::WatchedDirectory::handle(struct inotify_event* event) {
  std::string basename;

  if (event->len > 0) {
    basename = event->name;
  }

  DEBUG_INFO << "inotify event on: " << path << "\n  basename: " << basename << "\n  flags:"
             << ((event->mask & IN_CREATE     ) ? " IN_CREATE"      : "")
             << ((event->mask & IN_DELETE     ) ? " IN_DELETE"      : "")
             << ((event->mask & IN_DELETE_SELF) ? " IN_DELETE_SELF" : "")
             << ((event->mask & IN_MODIFY     ) ? " IN_MODIFY"      : "")
             << ((event->mask & IN_MOVE_SELF  ) ? " IN_MOVE_SELF"   : "")
             << ((event->mask & IN_MOVED_FROM ) ? " IN_MOVED_FROM"  : "")
             << ((event->mask & IN_MOVED_TO   ) ? " IN_MOVED_TO"    : "");

  if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
    // Watch descriptor is automatically removed.
    wd = -1;
  }

  // We must collect the complete set of WatchOperations before we call any callbacks, because
  // any callback could come back and change what is being watched.
  std::set<WatchOperation*> currentlyHandling;
  this->currentlyHandling = &currentlyHandling;

  for (CallbackTable::SearchIterator<CallbackTable::BASENAME> iter(callbackTable, basename);
       iter.next();) {
    WatchOperation* op = iter.cell<CallbackTable::WATCH_OP>();
    if (event->mask & (IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM | IN_MOVE_SELF)) {
      op->flagAsDeleted();
    } else {
      op->flagAsModified();
    }
    currentlyHandling.insert(op);
  }

  // If this event is indicating creation or deletion of a file in the directory, then call the
  // directory's modified() callback as well.
  if (!basename.empty() &&
      (event->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO))) {
    for (CallbackTable::SearchIterator<CallbackTable::BASENAME> iter(callbackTable, "");
         iter.next();) {
      WatchOperation* op = iter.cell<CallbackTable::WATCH_OP>();
      op->flagAsModified();
      currentlyHandling.insert(op);
    }
  }

  // Repeatedly call a callback until none are left.  Note that we can't just iterate through
  // the set because if the callback deletes other WatchedOperations in the set, they will be
  // removed from it, invalidating any iterators.
  while (!currentlyHandling.empty()) {
    WatchOperation* op = *currentlyHandling.begin();
    currentlyHandling.erase(currentlyHandling.begin());
    op->callCallback();
  }

  this->currentlyHandling = NULL;
  deleteSelfIfEmpty();
}

void EpollEventManager::onFileChange(const std::string& filename, FileChangeCallback* callback,
                                     OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<WatchOperation>(this, filename, callback);
}

void EpollEventManager::handleInotify(short pollFlags) {
  if (pollFlags & POLLIN) {
    DEBUG_INFO << "inotify FD is readable";
  } else if (pollFlags & POLLERR) {
    DEBUG_INFO << "inotify FD has error";
  } else if (pollFlags & POLLHUP) {
    DEBUG_INFO << "inotify FD hung up";
  } else {
    DEBUG_ERROR << "handleInotify should only get POLLIN, POLLERR, or POLLHUP events.";
    return;
  }

  char buffer[sizeof(struct inotify_event) + PATH_MAX];

  ssize_t n = read(inotifyFd, buffer, sizeof(buffer));
  if (n < 0) {
    DEBUG_ERROR << "read(inotifyFd): " << strerror(errno);
  } else if (n == 0) {
    DEBUG_ERROR << "read(inotifyFd) returned zero";
  } else {
    char* pos = buffer;
    char* end = buffer + n;

    // Annoyingly, inotify() provides no way to read a single event at a time.  Unfortunately,
    // each time we handle an event, any subsequent event could become invalid.  E.g. handling
    // the first event might cause the watch descriptor for the second event to be unregistered.
    //
    // As if that weren't bad enough, any particular event in the stream might indicate that a
    // particular watch descriptor has been automatically removed because the thing it was watching
    // no longer exists.  This removal takes place at the time of the read().  So if the second
    // event in the buffer has the "watch descriptor removed" flag, and then while handling the
    // *first* event we create a new watch descriptor, that new descriptor may have the same
    // number as the one associated with the second event THAT WE HAVEN'T EVEN HANDLED YET.
    //
    // Therefore, we have to verify carefully make multiple passes over the events, making sure
    // that we understand exactly what is happening to each descriptor before we call any
    // callbacks.
    //
    // FFFFFFFFFFFFFFFFFFFFFFUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU-

    // First parse all the events.
    std::vector<std::pair<struct inotify_event*, WatchedDirectory*> > events;

    while (pos < end) {
      if (end - pos < (signed)sizeof(struct inotify_event)) {
        DEBUG_ERROR << "read(inotifyFd) returned too few bytes to be an inotify_event.";
        break;
      }

      struct inotify_event* event = reinterpret_cast<struct inotify_event*>(pos);

      if (end - pos - sizeof(struct inotify_event) < event->len) {
        DEBUG_ERROR
            << "read(inotifyFd) returned inotify_event with 'len' that overruns the buffer.";
        break;
      }

      pos += sizeof(struct inotify_event) + event->len;

      DEBUG_INFO << "inotify " << event->wd << ":"
                 << ((event->mask & IN_CREATE     ) ? " IN_CREATE"      : "")
                 << ((event->mask & IN_DELETE     ) ? " IN_DELETE"      : "")
                 << ((event->mask & IN_DELETE_SELF) ? " IN_DELETE_SELF" : "")
                 << ((event->mask & IN_MODIFY     ) ? " IN_MODIFY"      : "")
                 << ((event->mask & IN_MOVE_SELF  ) ? " IN_MOVE_SELF"   : "")
                 << ((event->mask & IN_MOVED_FROM ) ? " IN_MOVED_FROM"  : "")
                 << ((event->mask & IN_MOVED_TO   ) ? " IN_MOVED_TO"    : "");

      WatchMap::iterator iter = watchMap.find(event->wd);
      if (iter == watchMap.end()) {
        DEBUG_ERROR << "inotify event had unknown watch descriptor?";
      } else {
        events.push_back(std::make_pair(event, iter->second));
      }
    }

    // Some events implicitly remove the watch descriptor (because the watched directory no longer
    // exists).  Such descriptors are now invalid and may be reused the next time
    // inotify_add_watch() is called.  But, the corresponding WatchedDirectories still have
    // WatchOperations pointing at them, so we can't just delete them.  So, invalidate them so
    // that no future WatchOperations will use them.
    //
    // inotify has a special kind of event called IN_IGNORED which is supposed to signal that the
    // watch descriptor was removed, either implicitly or explicitly.  However, in my testing,
    // this event does not seem to be generated in the case of IN_MOVE_SELF, even though this
    // case *does* implicitly remove the watch descriptor (which I know because the next call
    // to inotify_add_watch() typically reuses it).  This appears to be a bug in Linux (observed
    // in 2.6.35-22-generic).  Will file a bug report if I get time to write a demo program.
    for (size_t i = 0; i < events.size(); i++) {
      struct inotify_event* event = events[i].first;
      WatchedDirectory* watchedDirectory = events[i].second;

      if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
        DEBUG_INFO << "Watch descriptor implicitly removed: " << event->wd;
        watchedDirectory->invalidate();
      }
    }

    // Calling a callback could delete the WatchedDirectory for a subsequent event.  So, record
    // what objects we're handling.  The destructor for WatchedDirectory will remove the object
    // from this set if present.
    currentlyHandlingWatches.clear();
    for (size_t i = 0; i < events.size(); i++) {
      currentlyHandlingWatches.insert(events[i].second);
    }

    // Finally, let's call the callbacks.
    for (size_t i = 0; i < events.size(); i++) {
      struct inotify_event* event = events[i].first;
      WatchedDirectory* watchedDirectory = events[i].second;

      if (currentlyHandlingWatches.count(watchedDirectory) > 0) {
        watchedDirectory->handle(event);
      }
    }
  }
}

// =======================================================================================

void EpollEventManager::loop() {
  while (handleEvent()) {}
}

bool EpollEventManager::handleEvent() {
  // Run any async callbacks first.
  // TODO:  Avoid starvation of I/O?  Probably doesn't matter.
  if (!asyncCallbacks.empty()) {
    AsyncCallbackHandler* handler = asyncCallbacks.front();
    asyncCallbacks.pop_front();
    handler->run();
    return true;
  }

  int n = readHandlerMap.size() + writeHandlerMap.size() + (watchMap.empty() ? 0 : 1);
  if (n == 0) {
    if (processExitHandlerMap.empty()) {
      DEBUG_INFO << "No more events.";
      return false;
    } else {
      siginfo_t siginfo;
      DEBUG_INFO << "Waiting for signals...";
      if (sigWait(&siginfo)) {
        handleSignal(siginfo);
      }
      return true;
    }
  }

  DEBUG_INFO << "Waiting for events...";

  std::vector<PollFd> pollFds(n);
  std::vector<IoHandler*> handlers(n);

  int pos = 0;

  for (std::tr1::unordered_map<int, IoHandler*>::iterator iter = readHandlerMap.begin();
       iter != readHandlerMap.end(); ++iter) {
    pollFds[pos].fd = iter->first;
    pollFds[pos].events = POLLIN;
    pollFds[pos].revents = 0;
    handlers[pos] = iter->second;
    ++pos;
  }

  for (std::tr1::unordered_map<int, IoHandler*>::iterator iter = writeHandlerMap.begin();
       iter != writeHandlerMap.end(); ++iter) {
    pollFds[pos].fd = iter->first;
    pollFds[pos].events = POLLOUT;
    pollFds[pos].revents = 0;
    handlers[pos] = iter->second;
    ++pos;
  }

  if (!watchMap.empty()) {
    pollFds[pos].fd = inotifyFd;
    pollFds[pos].events = POLLIN;
    pollFds[pos].revents = 0;
    handlers[pos] = NULL;
    ++pos;
  }

  assert(pos == n);

  siginfo_t siginfo;
  int result = sigPoll(&pollFds[0], n, -1, &siginfo);

  if (result == SIGPOLL_ERROR) {
    DEBUG_ERROR << "poll(): " << strerror(errno);
  } else if (result == SIGPOLL_SIGNALED) {
    handleSignal(siginfo);
  } else {
    for (int i = 0; i < n; i++) {
      if (pollFds[i].revents != 0) {
        if (handlers[i] == NULL) {
          handleInotify(pollFds[i].revents);
        } else {
          handlers[i]->handle(pollFds[i].revents);
        }

        // We can only handle one event at a time because handling that event may have affected
        // the others, e.g. they may have been canceled / deleted.  And anyway, handleEvent()
        // claims to only handle one event at a time.
        break;
      }
    }
  }

  return true;
}

void EpollEventManager::handleSignal(const siginfo_t& siginfo) {
  switch (siginfo.si_signo) {
    case SIGCHLD: {
      // If multiple signals with the same signal number are delivered while signals are blocked,
      // only one of them is actually delivered once un-blocked.  The others are cast into the
      // void.  Therefore, the contents of the siginfo structure are effectively useless for
      // SIGCHLD.  We must instead call waitpid() repeatedly until there are no more completed
      // children.  Signals suck so much.
      while (true) {
        int waitStatus;
        pid_t pid = waitpid(-1, &waitStatus, WNOHANG);
        if (pid < 0) {
          // ECHILD indicates there are no child processes.  Anything else is a real error.
          if (errno != ECHILD) {
            DEBUG_ERROR << "waitpid: " << strerror(errno);
          }
          break;
        } else if (pid == 0) {
          // There are child processes, but they are still running.
          break;
        }

        // Get the handler associated with this PID.
        std::tr1::unordered_map<pid_t, ProcessExitHandler*>::iterator iter =
            processExitHandlerMap.find(pid);
        if (iter == processExitHandlerMap.end()) {
          // It is actually important that any code creating a subprocess call onProcessExit()
          // to receive notification of completion even if it doesn't care about completion,
          // because otherwise the sub-process may be stuck as a zombie.  This is actually
          // NOT the case with EpollEventManager because it waits on all sub-processes whether
          // onProcessExit() was called or not, but we should warn if we encounter a process for
          // which onProcessExit() was never called so that the code can be fixed.
          DEBUG_ERROR << "Got SIGCHLD for PID we weren't waiting for: " << pid;
          return;
        }

        iter->second->handle(waitStatus);
      }
      break;
    }

    default:
      DEBUG_ERROR << "Unexpected signal number: " << siginfo.si_signo;
      break;
  }
}

// =======================================================================================

void newPreferredEventManager(OwnedPtr<RunnableEventManager>* output) {
  output->allocateSubclass<EpollEventManager>();
}

}  // namespace ekam
