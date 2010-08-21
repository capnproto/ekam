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

#include "Debug.h"

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

PollEventManager::PollEventManager()
    : processExitHandlerRegistrar(&processExitHandlers),
      ioHandlerRegistrar(&ioHandlers) {
  initSignalHandling();
}

PollEventManager::~PollEventManager() {}

void PollEventManager::loop() {
  while (handleEvent()) {}
}

bool PollEventManager::handleEvent() {
  DEBUG_INFO << "handleEvent()";

  // Run any async callbacks first.
  // TODO:  Avoid starvation of I/O?  Probably doesn't matter.
  if (!asyncCallbacks.empty()) {
    OwnedPtr<Callback> callback;
    asyncCallbacks.release(&callback);
    callback->run();
    return true;
  }

  int n = readHandlerMap.size() + writeHandlerMap.size();
  if (n == 0) {
    if (processExitHandlerMap.empty()) {
      DEBUG_INFO << "No more events.";
      return false;
    } else {
      siginfo_t siginfo;
      if (sigWait(&siginfo)) {
        handleSignal(siginfo);
      }
      return true;
    }
  }

  std::vector<PollFd> pollFds(n);
  std::vector<EventHandler<IoEvent>*> handlers(n);

  int pos = 0;

  for (std::tr1::unordered_map<int, EventHandler<IoEvent>*>::iterator iter = readHandlerMap.begin();
       iter != readHandlerMap.end(); ++iter) {
    pollFds[pos].fd = iter->first;
    pollFds[pos].events = POLLIN;
    pollFds[pos].revents = 0;
    handlers[pos] = iter->second;
    ++pos;
  }

  for (std::tr1::unordered_map<int, EventHandler<IoEvent>*>::iterator iter = writeHandlerMap.begin();
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
        IoEvent event;
        event.pollFlags = pollFds[i].revents;
        if (handlers[i]->handle(event)) {
          // Handler is all done; remove it.
          ioHandlers.erase(handlers[i]);
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

void PollEventManager::handleSignal(const siginfo_t& siginfo) {
  switch (siginfo.si_signo) {
    case SIGCHLD: {
      // Clean up zombie child.  See rant earlier in this file.
      int waitStatus;
      if (waitpid(siginfo.si_pid, &waitStatus, 0) != siginfo.si_pid) {
        DEBUG_ERROR << "waitpid: " << strerror(errno);
      }

      // Get the handler associated with this PID.
      std::tr1::unordered_map<pid_t, EventHandler<ProcessExitEvent>*>::iterator iter =
          processExitHandlerMap.find(siginfo.si_pid);
      if (iter == processExitHandlerMap.end()) {
        // It is actually important that every sub-process be waited on via the EventManager to
        // avoid a built-up of zombie sub-processes.  Technically PollEventManager doesn't have
        // this problem since it wait()s on any PID for which it gets a SIGCHLD, but other
        // implementations are likely to be different.
        DEBUG_ERROR << "Got SIGCHLD for PID we weren't waiting for: " << siginfo.si_pid;
        return;
      }
      EventHandler<ProcessExitEvent>* handler = iter->second;

      // Call handler.
      ProcessExitEvent event;
      event.waitStatus = waitStatus;
      if (handler->handle(event)) {
        // Handler is all done; remove it.
        processExitHandlers.erase(handler);
      }
      break;
    }

    default:
      DEBUG_ERROR << "Unexpected signal number: " << siginfo.si_signo;
      break;
  }
}

// =======================================================================================

void PollEventManager::runAsynchronously(OwnedPtr<Callback>* callbackToAdopt) {
  asyncCallbacks.adopt(callbackToAdopt);
}

// =======================================================================================

class PollEventManager::ProcessExitHandler : public EventHandler<ProcessExitEvent> {
public:
  ProcessExitHandler(PollEventManager* eventManager, pid_t pid,
                     OwnedPtr<ProcessExitCallback>* callbackToAdopt)
      : eventManager(eventManager), pid(pid) {
    callback.adopt(callbackToAdopt);
  }
  ~ProcessExitHandler() {
    eventManager->processExitHandlerMap.erase(pid);
  }

  // implements EventHandler -------------------------------------------------------------

  bool handle(const ProcessExitEvent& event) {
    DEBUG_INFO << "Process " << pid << " exited with status: " << event.waitStatus;

    if (WIFEXITED(event.waitStatus)) {
      callback->exited(WEXITSTATUS(event.waitStatus));
    } else if (WIFSIGNALED(event.waitStatus)) {
      callback->signaled(WTERMSIG(event.waitStatus));
    } else {
      DEBUG_ERROR << "Didn't understand process exit status.";
      callback->exited(-1);
    }

    return true;
  }

private:
  PollEventManager* eventManager;
  pid_t pid;
  OwnedPtr<ProcessExitCallback> callback;
};

void PollEventManager::onProcessExit(pid_t process,
                                     OwnedPtr<ProcessExitCallback>* callbackToAdopt,
                                     OwnedPtr<Canceler>* output) {
  OwnedPtr<EventHandler<ProcessExitEvent> > handler;
  handler.allocateSubclass<ProcessExitHandler>(this, process, callbackToAdopt);

  if (output != NULL) {
    handler.allocateSubclass<CancelableEventHandler<ProcessExitEvent> >(
        &handler, &processExitHandlerRegistrar, output);
  }

  if (!processExitHandlerMap.insert(std::make_pair(process, handler.get())).second) {
    throw std::runtime_error("Already waiting on this PID.");
  }
  processExitHandlers.adopt(handler.get(), &handler);
}

// =======================================================================================

class PollEventManager::ReadHandler : public EventHandler<IoEvent> {
public:
  ReadHandler(PollEventManager* eventManager, int fd, OwnedPtr<IoCallback>* callbackToAdopt)
      : eventManager(eventManager), fd(fd) {
    callback.adopt(callbackToAdopt);
  }
  ~ReadHandler() {
    eventManager->readHandlerMap.erase(fd);
  }

  // implements EventHandler -------------------------------------------------------------

  bool handle(const IoEvent& event) {
    if (event.pollFlags & POLLIN) {
      DEBUG_INFO << "FD is readable: " << fd;
    } else if (event.pollFlags & POLLERR) {
      DEBUG_INFO << "FD has error: " << fd;
    } else if (event.pollFlags & POLLHUP) {
      DEBUG_INFO << "FD hung up: " << fd;
    } else {
      DEBUG_ERROR << "ReadHandler should only get POLLIN, POLLERR, or POLLHUP events.";
      return false;
    }

    return callback->ready() == IoCallback::DONE;
  }

private:
  PollEventManager* eventManager;
  int fd;
  OwnedPtr<IoCallback> callback;
};

void PollEventManager::onReadable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                                  OwnedPtr<Canceler>* output) {
  OwnedPtr<EventHandler<IoEvent> > handler;
  handler.allocateSubclass<ReadHandler>(this, fd, callbackToAdopt);

  if (output != NULL) {
    handler.allocateSubclass<CancelableEventHandler<IoEvent> >(
        &handler, &ioHandlerRegistrar, output);
  }

  if (!readHandlerMap.insert(std::make_pair(fd, handler.get())).second) {
    throw std::runtime_error("Already reading this FD.");
  }
  ioHandlers.adopt(handler.get(), &handler);
}

// =======================================================================================

class PollEventManager::WriteHandler : public EventHandler<IoEvent> {
public:
  WriteHandler(PollEventManager* eventManager, int fd, OwnedPtr<IoCallback>* callbackToAdopt)
      : eventManager(eventManager), fd(fd) {
    callback.adopt(callbackToAdopt);
  }
  ~WriteHandler() {
    eventManager->writeHandlerMap.erase(fd);
  }

  // implements EventHandler -------------------------------------------------------------

  bool handle(const IoEvent& event) {
    if (event.pollFlags & POLLOUT) {
      DEBUG_INFO << "FD is writable: " << fd;
    } else if (event.pollFlags & POLLERR) {
      DEBUG_INFO << "FD has error: " << fd;
    } else {
      DEBUG_ERROR << "WriteHandler should only get POLLOUT or POLLERR events.";
      return false;
    }

    return callback->ready() == IoCallback::DONE;
  }

private:
  PollEventManager* eventManager;
  int fd;
  OwnedPtr<IoCallback> callback;
};

void PollEventManager::onWritable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                                  OwnedPtr<Canceler>* output) {
  OwnedPtr<EventHandler<IoEvent> > handler;
  handler.allocateSubclass<WriteHandler>(this, fd, callbackToAdopt);

  if (output != NULL) {
    handler.allocateSubclass<CancelableEventHandler<IoEvent> >(
        &handler, &ioHandlerRegistrar, output);
  }

  if (!writeHandlerMap.insert(std::make_pair(fd, handler.get())).second) {
    throw std::runtime_error("Already writing this FD.");
  }
  ioHandlers.adopt(handler.get(), &handler);
}

}  // namespace ekam
