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
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <algorithm>
#include <stdexcept>
#include <assert.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <limits.h>

#include "base/Debug.h"
#include "base/Table.h"

namespace ekam {

namespace {

std::string epollEventsToString(uint32_t events) {
  std::string result;
  if (events & EPOLLIN) {
    result.append(" EPOLLIN");
  }
  if (events & EPOLLOUT) {
    result.append(" EPOLLOUT");
  }
  if (events & EPOLLRDHUP) {
    result.append(" EPOLLRDHUP");
  }
  if (events & EPOLLPRI) {
    result.append(" EPOLLPRI");
  }
  if (events & EPOLLERR) {
    result.append(" EPOLLERR");
  }
  if (events & EPOLLHUP) {
    result.append(" EPOLLHUP");
  }
  if (events & EPOLLET) {
    result.append(" EPOLLET");
  }
  if (events & EPOLLONESHOT) {
    result.append(" EPOLLONESHOT");
  }

  if (events & ~(EPOLLIN || EPOLLOUT || EPOLLRDHUP || EPOLLPRI || EPOLLERR || EPOLLHUP ||
                 EPOLLET || EPOLLONESHOT)) {
    result.append(" (others)");
  }

  return result;
}

// TODO:  Copied from DiskFile.cpp.  Share code somehow?
bool statIfExists(const std::string& path, struct stat* output) {
  int result;
  do {
    result = stat(path.c_str(), output);
  } while (result < 0 && errno == EINTR);

  if (result == 0) {
    return true;
  } else if (errno == ENOENT) {
    return false;
  } else {
    throw OsError(path, "stat", errno);
  }
}

bool isDirectory(const std::string& path) {
  struct stat stats;
  return statIfExists(path.c_str(), &stats) && S_ISDIR(stats.st_mode);
}

}  // namespace

EpollEventManager::Epoller::Epoller()
    : epollHandle("epoll", WRAP_SYSCALL(epoll_create1, (int)EPOLL_CLOEXEC)),
      watchCount(0) {}

EpollEventManager::Epoller::~Epoller() {
  if (watchCount > 0) {
    DEBUG_ERROR << "Epoller destroyed before all Watches destroyed.";
  }
}

EpollEventManager::Epoller::Watch::Watch(Epoller* epoller, OsHandle* handle,
                                         uint32_t events, IoHandler* handler)
    : epoller(epoller), events(0), registeredEvents(0), fd(handle->get()), name(handle->getName()),
      handler(handler) {
  addEvents(events);
}

EpollEventManager::Epoller::Watch::Watch(Epoller* epoller, int fd,
                                         uint32_t events, IoHandler* handler)
    : epoller(epoller), events(0), registeredEvents(0), fd(fd), name(toString(fd)),
      handler(handler) {
  addEvents(events);
}

EpollEventManager::Epoller::Watch::~Watch() {
  removeEvents(events);

  if (epoller->watchesNeedingUpdate.erase(this) > 0) {
    updateRegistration();
  }
}

void EpollEventManager::Epoller::Watch::addEvents(uint32_t eventsToAdd) {
  uint32_t newEvents = events | eventsToAdd;
  if (newEvents == events) {
    return;
  }

  events = newEvents;
  if (events == registeredEvents) {
    epoller->watchesNeedingUpdate.erase(this);
  } else {
    epoller->watchesNeedingUpdate.insert(this);
  }
}

void EpollEventManager::Epoller::Watch::removeEvents(uint32_t eventsToRemove) {
  uint32_t newEvents = events & ~eventsToRemove;
  if (newEvents == events) {
    return;
  }

  events = newEvents;
  if (events == registeredEvents) {
    epoller->watchesNeedingUpdate.erase(this);
  } else {
    epoller->watchesNeedingUpdate.insert(this);
  }
}

void EpollEventManager::Epoller::Watch::updateRegistration() {
  if (registeredEvents == events) {
    DEBUG_ERROR << "Watch does not need updating.";
    return;
  }

  int op = EPOLL_CTL_MOD;
  if (registeredEvents == 0) {
    ++epoller->watchCount;
    op = EPOLL_CTL_ADD;
  } else if (events == 0) {
    --epoller->watchCount;
    op = EPOLL_CTL_DEL;
  }
  registeredEvents = events;

  struct epoll_event event;
  event.events = registeredEvents;
  event.data.ptr = this;
  WRAP_SYSCALL(epoll_ctl, epoller->epollHandle, op, fd, &event);
}

bool EpollEventManager::Epoller::handleEvent() {
  // Run pending updates.
  for (Watch* watch : watchesNeedingUpdate) {
    watch->updateRegistration();
  }
  watchesNeedingUpdate.clear();

  if (watchCount == 0) {
    DEBUG_INFO << "No more events.";
    return false;
  }

  DEBUG_INFO << "Waiting for " << watchCount << " events...";
  struct epoll_event event;
  int result = WRAP_SYSCALL(epoll_wait, epollHandle, &event, 1, -1);
  if (result == 0) {
    throw std::logic_error("epoll_wait() returned zero despite infinite timeout.");
  } else if (result > 1) {
    throw std::logic_error("epoll_wait() returned more than one event when only one requested.");
  }

  Watch* watch = reinterpret_cast<Watch*>(event.data.ptr);
  DEBUG_INFO << "epoll event: " << watch->name << ": " << epollEventsToString(event.events);

  watch->handler->handle(event.events);

  return true;
}

// =============================================================================

namespace {

sigset_t getHandledSignals() {
  sigset_t result;
  sigemptyset(&result);
  sigaddset(&result, SIGCHLD);
  return result;
}

const sigset_t HANDLED_SIGNALS = getHandledSignals();

void dummyHandler(int i) {}

}  // namespace

EpollEventManager::SignalHandler::SignalHandler(Epoller* epoller)
    : signalStream(WRAP_SYSCALL(signalfd, -1, &HANDLED_SIGNALS, SFD_NONBLOCK | SFD_CLOEXEC),
                   "signalfd"),
      watch(epoller, signalStream.getHandle(), 0, this) {
  sigprocmask(SIG_BLOCK, &HANDLED_SIGNALS, NULL);
}

EpollEventManager::SignalHandler::~SignalHandler() {
  sigprocmask(SIG_UNBLOCK, &HANDLED_SIGNALS, NULL);
}

void EpollEventManager::SignalHandler::handle(uint32_t events) {
  DEBUG_INFO << "Received signal on signalfd.";

  struct signalfd_siginfo signalEvent;
  if (signalStream.read(&signalEvent, sizeof(signalEvent)) != sizeof(signalEvent)) {
    DEBUG_ERROR << "read(signalfd) returned wrong size.";
    return;
  }

  if (signalEvent.ssi_signo == SIGCHLD) {
    // Alas, the contents of signalEvent are useless, as the signal queue only holds one signal
    // per signal number.  Therefore, if two SIGCHLDs are received before we have a chance to
    // handle the first one, one of the two is discarded.  But we have to wait() to avoid zombies
    // anyway so I guess it's no big deal.
    handleProcessExit();
  } else {
    DEBUG_ERROR << "Unexpected signal number: " << signalEvent.ssi_signo;
  }
}

void EpollEventManager::SignalHandler::maybeStopExpecting() {
  if (processExitHandlerMap.empty()) {
    watch.removeEvents(EPOLLIN);
  }
}

// =======================================================================================

class EpollEventManager::AsyncCallbackHandler : public AsyncOperation, public PendingRunnable {
public:
  AsyncCallbackHandler(EpollEventManager* eventManager, Callback* callback)
      : eventManager(eventManager), called(false), callback(callback) {
    eventManager->asyncCallbacks.push_back(this);
  }
  AsyncCallbackHandler(EpollEventManager* eventManager, OwnedPtr<Runnable> runnable)
      : eventManager(eventManager), called(false), callback(nullptr), runnable(runnable.release()) {
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
    if (runnable == nullptr) {
      callback->run();
    } else {
      runnable->run();
    }
  }

private:
  EpollEventManager* eventManager;
  bool called;
  Callback* callback;
  OwnedPtr<Runnable> runnable;
};

OwnedPtr<AsyncOperation> EpollEventManager::runAsynchronously(Callback* callback) {
  return newOwned<AsyncCallbackHandler>(this, callback);
}

OwnedPtr<PendingRunnable> EpollEventManager::runLater(OwnedPtr<Runnable> runnable) {
  return newOwned<AsyncCallbackHandler>(this, runnable.release());
}

// =======================================================================================

class EpollEventManager::SignalHandler::ProcessExitHandler
    : public PromiseFulfiller<ProcessExitCode> {
public:
  ProcessExitHandler(Callback* callback, SignalHandler* signalHandler, pid_t pid)
      : callback(callback), signalHandler(signalHandler), pid(pid) {
    if (!signalHandler->processExitHandlerMap.insert(
        std::make_pair(pid, this)).second) {
      throw std::runtime_error("Already waiting on this process.");
    }
    signalHandler->watch.addEvents(EPOLLIN);
  }
  ~ProcessExitHandler() {
    if (pid != -1) {
      signalHandler->processExitHandlerMap.erase(pid);
      signalHandler->maybeStopExpecting();
    }
  }

  void handle(int waitStatus) {
    DEBUG_INFO << "Process " << pid << " exited with status: " << waitStatus;

    signalHandler->processExitHandlerMap.erase(pid);
    signalHandler->maybeStopExpecting();
    pid = -1;

    if (WIFEXITED(waitStatus)) {
      callback->fulfill(ProcessExitCode(WEXITSTATUS(waitStatus)));
    } else if (WIFSIGNALED(waitStatus)) {
      callback->fulfill(ProcessExitCode(ProcessExitCode::SIGNALED, WTERMSIG(waitStatus)));
    } else {
      DEBUG_ERROR << "Didn't understand process exit status.";
      callback->fulfill(ProcessExitCode(-1));
    }
  }

private:
  Callback* callback;
  SignalHandler* signalHandler;
  pid_t pid;
};

void EpollEventManager::SignalHandler::handleProcessExit() {
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
}

Promise<ProcessExitCode> EpollEventManager::SignalHandler::onProcessExit(pid_t pid) {
  return newPromise<ProcessExitHandler>(this, pid);
}

Promise<ProcessExitCode> EpollEventManager::onProcessExit(pid_t pid) {
  return signalHandler.onProcessExit(pid);
}

// =======================================================================================

class EpollEventManager::IoWatcherImpl: public IoWatcher, public IoHandler {
public:
  IoWatcherImpl(Epoller* epoller, int fd)
      : watch(epoller, fd, 0, this),
        readFulfiller(nullptr), writeFulfiller(nullptr) {}

  ~IoWatcherImpl() {}

  // implements IoWatcher --------------------------------------------------------------

  Promise<void> onReadable() {
    if (readFulfiller != nullptr) {
      throw std::logic_error("Already waiting for readability on this fd.");
    }
    return newPromise<Fulfiller>(&watch, EPOLLIN, &readFulfiller);
  }

  Promise<void> onWritable() {
    if (readFulfiller != nullptr) {
      throw std::logic_error("Already waiting for writability on this fd.");
    }
    return newPromise<Fulfiller>(&watch, EPOLLOUT, &writeFulfiller);
  }

  // implements IoHandler --------------------------------------------------------------
  void handle(uint32_t events) {
    if (events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
      if (readFulfiller != nullptr) {
        readFulfiller->ready();
      }
    }
    if (events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) {
      if (writeFulfiller != nullptr) {
        writeFulfiller->ready();
      }
    }
  }

private:
  class Fulfiller: public PromiseFulfiller<void> {
  public:
    Fulfiller(Callback* callback, Epoller::Watch* watch, uint32_t events, Fulfiller** ptr)
        : callback(callback), watch(watch), events(events), ptr(ptr) {
      *ptr = this;
      watch->addEvents(events);
    }
    ~Fulfiller() {
      if (ptr != nullptr) {
        *ptr = nullptr;
        watch->removeEvents(events);
      }
    }

    void ready() {
      *ptr = nullptr;
      ptr = nullptr;
      watch->removeEvents(events);
      callback->fulfill();
    }

  private:
    Callback* callback;
    Epoller::Watch* watch;
    uint32_t events;
    Fulfiller** ptr;
  };

  Epoller::Watch watch;
  Fulfiller* readFulfiller;
  Fulfiller* writeFulfiller;
};

OwnedPtr<EventManager::IoWatcher> EpollEventManager::watchFd(int fd) {
  return newOwned<IoWatcherImpl>(&epoller, fd);
}

// =======================================================================================

class EpollEventManager::InotifyHandler::WatchedDirectory {
  class CallbackTable : public Table<IndexedColumn<std::string>,
                                     UniqueColumn<WatchOperation*> > {
  public:
    static const int BASENAME = 0;
    static const int WATCH_OP = 1;
  };

public:
  WatchedDirectory(InotifyHandler* inotifyHandler, const std::string& path)
      : inotifyHandler(inotifyHandler), path(path), currentlyHandling(NULL) {
    wd = WRAP_SYSCALL(inotify_add_watch, *inotifyHandler->inotifyStream.getHandle(), path.c_str(),
                      IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF |
                      IN_MOVED_FROM | IN_MOVED_TO);
    DEBUG_INFO << "inotify_add_watch(" << path << ") [" << wd << "]";
    inotifyHandler->watchMap[wd] = this;
    inotifyHandler->watchByNameMap[path] = this;
    inotifyHandler->watch.addEvents(EPOLLIN);
  }

  ~WatchedDirectory() {
    DEBUG_INFO << "~WatchedDirectory(): " << path;

    if (callbackTable.size() > 0) {
      DEBUG_ERROR << "Deleting WatchedDirectory before all WatchOperations were removed.";
    }

    if (wd >= 0) {
      DEBUG_INFO << "inotify_rm_watch(" << path << ") [" << wd << "]";

      if (WRAP_SYSCALL(inotify_rm_watch, *inotifyHandler->inotifyStream.getHandle(), wd) < 0) {
        DEBUG_ERROR << "inotify_rm_watch(" << path << "): " << strerror(errno);
      }
    }

    invalidate();

    // Make sure that if this WatchedDirectory has a pending event, that event is ignored.
    inotifyHandler->currentlyHandlingWatches.erase(this);

    if (inotifyHandler->currentlyHandlingWatches.empty()) {
      inotifyHandler->watch.removeEvents(EPOLLIN);
    }
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
      inotifyHandler->watchMap.erase(wd);
      inotifyHandler->watchByNameMap.erase(path);
      wd = -1;
    }
  }

  void handle(struct inotify_event* event);

private:
  InotifyHandler* inotifyHandler;
  int wd;
  std::string path;

  CallbackTable callbackTable;

  std::set<WatchOperation*>* currentlyHandling;

  void deleteSelfIfEmpty() {
    if (callbackTable.size() == 0) {
      inotifyHandler->ownedWatchDirectories.erase(this);
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

class EpollEventManager::InotifyHandler::WatchOperation : public AsyncOperation {
public:
  WatchOperation(InotifyHandler* inotifyHandler, const std::string& filename,
                 FileChangeCallback* callback)
      : callback(callback), watchedDirectory(NULL), modified(false), deleted(false) {
    // Split directory and basename.
    std::string directory;
    std::string basename;
    if (isDirectory(filename)) {
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
    WatchByNameMap::iterator iter = inotifyHandler->watchByNameMap.find(directory);
    if (iter == inotifyHandler->watchByNameMap.end()) {
      auto newWatchedDirectory = newOwned<WatchedDirectory>(inotifyHandler, directory);
      watchedDirectory = newWatchedDirectory.get();
      inotifyHandler->ownedWatchDirectories.add(watchedDirectory, newWatchedDirectory.release());
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

EpollEventManager::InotifyHandler::InotifyHandler(Epoller* epoller)
    : inotifyStream(WRAP_SYSCALL(inotify_init1, IN_NONBLOCK | IN_CLOEXEC), "inotify"),
      watch(epoller, inotifyStream.getHandle(), 0, this) {}

EpollEventManager::InotifyHandler::~InotifyHandler() {}

void EpollEventManager::InotifyHandler::WatchedDirectory::handle(struct inotify_event* event) {
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

void EpollEventManager::InotifyHandler::handle(uint32_t epollEvents) {
  char buffer[sizeof(struct inotify_event) + PATH_MAX];

  ssize_t n = inotifyStream.read(buffer, sizeof(buffer));

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

OwnedPtr<AsyncOperation> EpollEventManager::InotifyHandler::onFileChange(
    const std::string& filename, FileChangeCallback* callback) {
  return newOwned<WatchOperation>(this, filename, callback);
}

OwnedPtr<AsyncOperation> EpollEventManager::onFileChange(
    const std::string& filename, FileChangeCallback* callback) {
  return inotifyHandler.onFileChange(filename, callback);
}

// =======================================================================================

EpollEventManager::EpollEventManager()
  : signalHandler(&epoller), inotifyHandler(&epoller) {}
EpollEventManager::~EpollEventManager() {}

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

  return epoller.handleEvent();
}

// =======================================================================================

OwnedPtr<RunnableEventManager> newPreferredEventManager() {
  return newOwned<EpollEventManager>();
}

}  // namespace ekam
