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

#include "Debug.h"

namespace ekam {

class KqueueEventManager::KEventHandler : public EventHandler<KEvent> {
public:
  virtual ~KEventHandler() {}

  virtual void describe(KEvent* event) = 0;
};

class KqueueEventManager::KEventRegistration : public EventHandler<KEvent> {
public:
  KEventRegistration(KqueueEventManager* eventManager, RepeatCount repeatCount,
                     OwnedPtr<KEventHandler>* handlerToAdopt)
      : eventManager(eventManager), repeatCount(repeatCount), seenAtLeastOnce(false) {
    handler.adopt(handlerToAdopt);
  }

  ~KEventRegistration() {
    if (repeatCount == UNTIL_CANCELED || !seenAtLeastOnce) {
      // Unregister the event.
      KEvent event;
      handler->describe(&event);
      event.flags = EV_DELETE;
      eventManager->updateKqueue(event);

      if (eventManager->activeEvents.erase(std::make_pair(event.ident, event.filter)) == 0) {
        DEBUG_ERROR << "Event not in activeEvents?";
      }
    }
  }

  // implements EventHandler -------------------------------------------------------------
  bool handle(const KEvent& event) {
    seenAtLeastOnce = true;

    if (repeatCount == ONCE) {
      if (eventManager->activeEvents.erase(std::make_pair(event.ident, event.filter)) == 0) {
        DEBUG_ERROR << "Event not in activeEvents?";
      }

      if (!handler->handle(event)) {
        DEBUG_ERROR << "KEventHandler registered with EventType ONCE did not complete in one run.";
      }
      return true;
    } else {
      return handler->handle(event);
    }
  }

private:
  KqueueEventManager* eventManager;
  RepeatCount repeatCount;
  bool seenAtLeastOnce;
  OwnedPtr<KEventHandler> handler;
};

KqueueEventManager::KEventHandlerRegistrar::KEventHandlerRegistrar(
    KqueueEventManager* eventManager) : eventManager(eventManager) {}
KqueueEventManager::KEventHandlerRegistrar::~KEventHandlerRegistrar() {}
void KqueueEventManager::KEventHandlerRegistrar::unregister(EventHandler<KEvent>* handler) {
  if (!eventManager->handlers.erase(handler)) {
    DEBUG_ERROR << "Tried to unregister handler that was not registered.";
  }
}

void KqueueEventManager::initKEvent(KEvent* event, uintptr_t ident, short filter,
                                    u_int fflags, intptr_t data) {
  EV_SET(event, ident, filter, 0, fflags, data, NULL);
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
    throw std::runtime_error("kevent: " + std::string(strerror(errno)));
  } else if (n > 0) {
    DEBUG_ERROR << "kevent() returned events when not asked to.";
  }
}

// =======================================================================================

KqueueEventManager::KqueueEventManager()
    : kqueueFd(kqueue()), handlerRegistrar(this) {
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
    OwnedPtr<Callback> callback;
    asyncCallbacks.release(&callback);
    callback->run();
    return true;
  }

  if (handlers.empty()) {
    DEBUG_INFO << "No more events.";
    return false;
  }

  DEBUG_INFO << "Waiting for events...";

  struct kevent event;
  int n = kevent(kqueueFd, NULL, 0, &event, 1, NULL);

  DEBUG_INFO << "Received event.";

  if (n < 0) {
    DEBUG_ERROR << "kevent: " << strerror(errno);
  } else if (n == 0) {
    DEBUG_ERROR << "kevent() timed out, but timeout was infinite.";
  } else {
    if (n > 1) {
      DEBUG_ERROR << "kevent() returned more events than requested.";
    }

    EventHandler<KEvent>* handler = reinterpret_cast<EventHandler<KEvent>*>(event.udata);
    if (handler->handle(event)) {
      // Handler is all done; remove it.
      handlers.erase(handler);
    }
  }

  return true;
}

void KqueueEventManager::addHandler(RepeatCount repeatCount,
                                    OwnedPtr<KEventHandler>* handlerToAdopt,
                                    OwnedPtr<Canceler>* output) {
  KEvent event;
  (*handlerToAdopt)->describe(&event);

  if (!activeEvents.insert(std::make_pair(event.ident, event.filter)).second) {
    throw std::invalid_argument("Already waiting on that event.");
  }

  // Create the handler.
  OwnedPtr<EventHandler<KEvent> > handler;
  handler.allocateSubclass<KEventRegistration>(this, repeatCount, handlerToAdopt);

  // If we need a cancel callback, wrap in CancelableEventHandler.
  if (output != NULL) {
    handler.allocateSubclass<CancelableEventHandler<KEvent> >(&handler, &handlerRegistrar, output);
  }

  // Update the kqueue.
  event.flags = EV_ADD;
  if (repeatCount == ONCE) {
    event.flags |= EV_ONESHOT;
  }
  event.udata = handler.get();

  updateKqueue(event);

  // Add to handlers.
  handlers.adopt(handler.get(), &handler);
}

// =======================================================================================

void KqueueEventManager::runAsynchronously(OwnedPtr<Callback>* callbackToAdopt) {
  asyncCallbacks.adopt(callbackToAdopt);
}

// =======================================================================================

class KqueueEventManager::ProcessExitHandler : public KEventHandler {
public:
  ProcessExitHandler(pid_t pid, OwnedPtr<ProcessExitCallback>* callbackToAdopt)
      : pid(pid) {
    callback.adopt(callbackToAdopt);
  }
  ~ProcessExitHandler() {}

  // implements KEventHandler ------------------------------------------------------------
  void describe(KEvent* event) {
    initKEvent(event, pid, EVFILT_PROC, NOTE_EXIT, 0);
  }

  bool handle(const KEvent& event) {
    if (event.fflags & NOTE_EXIT == 0) {
      DEBUG_ERROR << "EVFILT_PROC kevent had unexpected fflags: " << event.fflags;
      return false;
    }

    DEBUG_INFO << "Process " << pid << " exited with status: " << event.data;

    if (WIFEXITED(event.data)) {
      callback->exited(WEXITSTATUS(event.data));
    } else if (WIFSIGNALED(event.data)) {
      callback->signaled(WTERMSIG(event.data));
    } else {
      DEBUG_ERROR << "Didn't understand process exit status.";
      callback->exited(-1);
    }

    return true;
  }

private:
  pid_t pid;
  OwnedPtr<ProcessExitCallback> callback;
};

void KqueueEventManager::onProcessExit(pid_t process,
                                       OwnedPtr<ProcessExitCallback>* callbackToAdopt,
                                       OwnedPtr<Canceler>* output) {
  OwnedPtr<KEventHandler> handler;
  handler.allocateSubclass<ProcessExitHandler>(process, callbackToAdopt);
  addHandler(ONCE, &handler, output);
}

// =======================================================================================

class KqueueEventManager::ReadHandler : public KEventHandler {
public:
  ReadHandler(int fd, OwnedPtr<IoCallback>* callbackToAdopt)
      : fd(fd) {
    callback.adopt(callbackToAdopt);
  }
  ~ReadHandler() {}

  // implements KEventHandler ------------------------------------------------------------
  void describe(KEvent* event) {
    initKEvent(event, fd, EVFILT_READ, 0, 0);
  }

  bool handle(const KEvent& event) {
    DEBUG_INFO << "FD is readable: " << fd;
    IoCallback::Status status = callback->ready();

    if (event.flags & EV_EOF) {
      // kqueue will not return this event again, so keep calling the callback until it gets the
      // message.
      DEBUG_INFO << "FD has EV_EOF: " << fd;
      while (status != IoCallback::DONE) {
        status = callback->ready();
      }
    }

    return status == IoCallback::DONE;
  }

private:
  int fd;
  OwnedPtr<IoCallback> callback;
};

void KqueueEventManager::onReadable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                                    OwnedPtr<Canceler>* output) {
  OwnedPtr<KEventHandler> handler;
  handler.allocateSubclass<ReadHandler>(fd, callbackToAdopt);
  addHandler(UNTIL_CANCELED, &handler, output);
}

// =======================================================================================

class KqueueEventManager::WriteHandler : public KEventHandler {
public:
  WriteHandler(int fd, OwnedPtr<IoCallback>* callbackToAdopt)
      : fd(fd) {
    callback.adopt(callbackToAdopt);
  }
  ~WriteHandler() {}

  // implements KEventHandler ------------------------------------------------------------
  void describe(KEvent* event) {
    initKEvent(event, fd, EVFILT_WRITE, 0, 0);
  }

  bool handle(const KEvent& event) {
    DEBUG_INFO << "FD is writable: " << fd;
    return callback->ready() == IoCallback::DONE;
  }

private:
  int fd;
  OwnedPtr<IoCallback> callback;
};

void KqueueEventManager::onWritable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                                    OwnedPtr<Canceler>* output) {
  OwnedPtr<KEventHandler> handler;
  handler.allocateSubclass<WriteHandler>(fd, callbackToAdopt);
  addHandler(UNTIL_CANCELED, &handler, output);
}

}  // namespace ekam
