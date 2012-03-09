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

#include "PollEventManager.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <algorithm>
#include <stdexcept>
#include <assert.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>

#include "base/Debug.h"

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

PollEventManager::PollEventManager() {
  initSignalHandling();
}

PollEventManager::~PollEventManager() {}

class PollEventManager::IoHandler {
public:
  virtual ~IoHandler() {}

  virtual void handle(short pollFlags) = 0;
};

// =======================================================================================

class PollEventManager::AsyncCallbackHandler : public AsyncOperation {
public:
  AsyncCallbackHandler(PollEventManager* eventManager, Callback* callback)
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
  PollEventManager* eventManager;
  bool called;
  Callback* callback;
};

OwnedPtr<AsyncOperation> PollEventManager::runAsynchronously(Callback* callback) {
  return newOwned<AsyncCallbackHandler>(this, callback);
}

// =======================================================================================

class PollEventManager::ProcessExitHandler : public AsyncOperation {
public:
  ProcessExitHandler(PollEventManager* eventManager, pid_t pid,
                     ProcessExitCallback* callback)
      : eventManager(eventManager), pid(pid), callback(callback) {
    if (!eventManager->processExitHandlerMap.insert(std::make_pair(pid, this)).second) {
      throw std::runtime_error("Already waiting on this process.");
    }
  }
  ~ProcessExitHandler() {
    if (pid != -1) eventManager->processExitHandlerMap.erase(pid);
  }

  void handle(int waitStatus) {
    DEBUG_INFO << "Process " << pid << " exited with status: " << waitStatus;

    eventManager->processExitHandlerMap.erase(pid);
    pid = -1;

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
  PollEventManager* eventManager;
  pid_t pid;
  ProcessExitCallback* callback;
};

OwnedPtr<AsyncOperation> PollEventManager::onProcessExit(pid_t pid, ProcessExitCallback* callback) {
  return newOwned<ProcessExitHandler>(this, pid, callback);
}

// =======================================================================================

class PollEventManager::ReadHandler : public AsyncOperation, public IoHandler {
public:
  ReadHandler(PollEventManager* eventManager, int fd, IoCallback* callback)
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
  PollEventManager* eventManager;
  int fd;
  IoCallback* callback;
};

OwnedPtr<AsyncOperation> PollEventManager::onReadable(int fd, IoCallback* callback) {
  return newOwned<ReadHandler>(this, fd, callback);
}

// =======================================================================================

class PollEventManager::WriteHandler : public AsyncOperation, public IoHandler {
public:
  WriteHandler(PollEventManager* eventManager, int fd, IoCallback* callback)
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
  PollEventManager* eventManager;
  int fd;
  IoCallback* callback;
};

OwnedPtr<AsyncOperation> PollEventManager::onWritable(int fd, IoCallback* callback) {
  return newOwned<WriteHandler>(this, fd, callback);
}

// =======================================================================================

OwnedPtr<AsyncOperation> PollEventManager::onFileChange(const std::string& filename,
                                                        FileChangeCallback* callback) {
  throw std::logic_error("PollEventManager::onFileChange not implemented.");
}

// =======================================================================================

void PollEventManager::loop() {
  while (handleEvent()) {}
}

bool PollEventManager::handleEvent() {
  // Run any async callbacks first.
  // TODO:  Avoid starvation of I/O?  Probably doesn't matter.
  if (!asyncCallbacks.empty()) {
    AsyncCallbackHandler* handler = asyncCallbacks.front();
    asyncCallbacks.pop_front();
    handler->run();
    return true;
  }

  int n = readHandlerMap.size() + writeHandlerMap.size();
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
        handlers[i]->handle(pollFds[i].revents);

        // We can only handle one event at a time because handling that event may have affected
        // the others, e.g. they may have been canceled / deleted.  And anyway, handleEvent()
        // claims to only handle one event at a time.
        break;
      }
    }
  }

  return true;
}

void PollEventManager::handleSignal(const siginfo_t& siginfo) {
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
          // NOT the case with PollEventManager because it waits on all sub-processes whether
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

OwnedPtr<RunnableEventManager> newPreferredEventManager() {
  return newOwned<PollEventManager>();
}

}  // namespace ekam
