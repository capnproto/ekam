// kake2 -- http://code.google.com/p/kake2
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
//     * Neither the name of the kake2 project nor the names of its
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

#include "Debug.h"

namespace kake2 {

void KqueueEventManager::updateKqueue(int kqueueFd, uintptr_t ident, short filter, u_short flags,
                                      u_int fflags, intptr_t data, EventHandlerFunc* handler) {
  KEvent event;
  EV_SET(&event, ident, filter, flags, fflags, data, reinterpret_cast<void*>(handler));
  int n = kevent(kqueueFd, &event, 1, NULL, 0, NULL);
  if (n < 0) {
    DEBUG_ERROR << "kevent: " << strerror(errno);
  } else if (n > 0) {
    DEBUG_ERROR << "kevent() returned events when not asked to.";
  }
}

KqueueEventManager::KqueueEventManager()
    : kqueueFd(kqueue()) {
  if (kqueueFd < 0) {
    std::string error(strerror(errno));
    throw std::runtime_error("kqueue: " + error);
  }
}

KqueueEventManager::~KqueueEventManager() {
  if (close(kqueueFd) < 0) {
    perror("close(kqueue)");
  }
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

  if (processExitCallbacks.empty() &&
      readCallbacks.empty() &&
      writeCallbacks.empty() &&
      continuousReadCallbacks.empty()) {
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

    EventHandlerFunc* handler = reinterpret_cast<EventHandlerFunc*>(event.udata);
    handler(this, event);
  }

  return true;
}

// =======================================================================================

void KqueueEventManager::runAsynchronously(OwnedPtr<Callback>* callbackToAdopt) {
  asyncCallbacks.adopt(callbackToAdopt);
}

// =======================================================================================

class KqueueEventManager::ProcessExitCanceler : public Canceler {
public:
  ProcessExitCanceler(KqueueEventManager* manager, pid_t pid) : manager(manager), pid(pid) {}
  ~ProcessExitCanceler() {}

  // implements Canceler -----------------------------------------------------------------
  void cancel() {
    if (manager->processExitCallbacks.erase(pid)) {
      updateKqueue(manager->kqueueFd, pid, EVFILT_PROC, EV_DELETE, 0, 0, NULL);
    }
  }

private:
  KqueueEventManager* manager;
  pid_t pid;
};

void KqueueEventManager::waitPid(
    pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt,
    OwnedPtr<Canceler>* output) {
  if (!processExitCallbacks.adoptIfNew(process, callbackToAdopt)) {
    throw std::invalid_argument("A callback is already registered for this process.");
  }

  updateKqueue(kqueueFd, process, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0,
               &handleProcessExit);

  if (output != NULL) output->allocateSubclass<ProcessExitCanceler>(this, process);
}

void KqueueEventManager::handleProcessExit(KqueueEventManager* self, const struct kevent& event) {
  pid_t pid = event.ident;

  if (event.fflags & NOTE_EXIT == 0) {
    DEBUG_ERROR << "EVFILT_PROC kevent had unexpected fflags: " << event.fflags;
    return;
  }

  DEBUG_INFO << "Process " << pid << " exited with status: " << event.data;

  OwnedPtr<ProcessExitCallback> callback;
  if (!self->processExitCallbacks.release(pid, &callback)) {
    DEBUG_ERROR << "PID not on Action's processExitCallbacks map.";
    return;
  }

  if (WIFEXITED(event.data)) {
    callback->exited(WEXITSTATUS(event.data));
  } else if (WIFSIGNALED(event.data)) {
    callback->signaled(WTERMSIG(event.data));
  } else {
    DEBUG_ERROR << "Didn't understand process exit status.";
    callback->exited(-1);
  }
}

// =======================================================================================

class KqueueEventManager::ReadCanceler : public Canceler {
public:
  ReadCanceler(KqueueEventManager* manager, int fd) : manager(manager), fd(fd) {}
  ~ReadCanceler() {}

  // implements Canceler -----------------------------------------------------------------
  void cancel() {
    if (manager->readCallbacks.erase(fd)) {
      updateKqueue(manager->kqueueFd, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
  }

private:
  KqueueEventManager* manager;
  int fd;
};

void KqueueEventManager::read(int fd, void* buffer, int size,
                              OwnedPtr<IoCallback>* callbackToAdopt,
                              OwnedPtr<Canceler>* output) {
  OwnedPtr<ReadContext> context;
  context.allocate(buffer, size, callbackToAdopt);

  if (continuousReadCallbacks.contains(fd) || !readCallbacks.adoptIfNew(fd, &context)) {
    throw std::invalid_argument("A read is already in progress on this fd.");
  }

  updateKqueue(kqueueFd, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, &handleRead);

  if (output != NULL) output->allocateSubclass<ReadCanceler>(this, fd);
}

void KqueueEventManager::handleRead(KqueueEventManager* self, const struct kevent& event) {
  int fd = event.ident;

  DEBUG_INFO << "FD is readable: " << fd;

  OwnedPtr<ReadContext> context;
  if (!self->readCallbacks.release(fd, &context)) {
    DEBUG_ERROR << "FD not on Action's readCallbacks map.";
    return;
  }

  while (true) {
    int bytesRead = ::read(fd, context->buffer, context->size);

    if (bytesRead >= 0) {
      context->callback->done(bytesRead);
      break;
    } else if (errno != EINTR) {
      context->callback->error(errno);
      break;
    }
  }
}

void KqueueEventManager::readAll(int fd, void* buffer, int size,
                                 OwnedPtr<IoCallback>* callbackToAdopt,
                                 OwnedPtr<Canceler>* output) {
  OwnedPtr<ReadContext> context;
  context.allocate(buffer, size, callbackToAdopt);

  if (continuousReadCallbacks.contains(fd) || !readCallbacks.adoptIfNew(fd, &context)) {
    throw std::invalid_argument("A read is already in progress on this fd.");
  }

  updateKqueue(kqueueFd, fd, EVFILT_READ, EV_ADD, 0, 0, &handleReadAll);

  if (output != NULL) output->allocateSubclass<ReadCanceler>(this, fd);
}

void KqueueEventManager::handleReadAll(KqueueEventManager* self, const struct kevent& event) {
  int fd = event.ident;

  DEBUG_INFO << "FD is readable: " << fd;

  ReadContext* context = self->readCallbacks.get(fd);
  if (context == NULL) {
    DEBUG_ERROR << "FD not on Action's readCallbacks map.";
    return;
  }

  try {
    while (true) {
      int bytesRead = ::read(fd, context->buffer + context->pos, context->size - context->pos);

      if (bytesRead == 0) {
        context->callback->done(context->pos);
        break;
      } else if (bytesRead > 0) {
        context->pos += bytesRead;
        if (context->pos == context->size) {
          context->callback->done(context->pos);
          break;
        }
        return;  // Not done; don't unregister.
      } else if (errno != EINTR) {
        context->callback->error(errno);
        break;
      }
    }
  } catch (...) {
    updateKqueue(self->kqueueFd, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    self->readCallbacks.erase(fd);
    throw;
  }

  updateKqueue(self->kqueueFd, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  self->readCallbacks.erase(fd);
}

// =======================================================================================

class KqueueEventManager::WriteCanceler : public Canceler {
public:
  WriteCanceler(KqueueEventManager* manager, int fd) : manager(manager), fd(fd) {}
  ~WriteCanceler() {}

  // implements Canceler -----------------------------------------------------------------
  void cancel() {
    if (manager->writeCallbacks.erase(fd)) {
      updateKqueue(manager->kqueueFd, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    }
  }

private:
  KqueueEventManager* manager;
  int fd;
};

void KqueueEventManager::write(int fd, const void* buffer, int size,
                               OwnedPtr<IoCallback>* callbackToAdopt,
                               OwnedPtr<Canceler>* output) {
  OwnedPtr<WriteContext> context;
  context.allocate(buffer, size, callbackToAdopt);

  if (!writeCallbacks.adoptIfNew(fd, &context)) {
    throw std::invalid_argument("A write is already in progress on this fd.");
  }

  updateKqueue(kqueueFd, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, &handleWrite);

  if (output != NULL) output->allocateSubclass<WriteCanceler>(this, fd);
}

void KqueueEventManager::handleWrite(KqueueEventManager* self, const struct kevent& event) {
  int fd = event.ident;

  DEBUG_INFO << "FD is writeable: " << fd;

  OwnedPtr<WriteContext> context;
  if (!self->writeCallbacks.release(fd, &context)) {
    DEBUG_ERROR << "FD not on Action's writeCallbacks map.";
    return;
  }

  while (true) {
    int bytesWritten = ::write(fd, context->buffer, context->size);

    if (bytesWritten >= 0) {
      context->callback->done(bytesWritten);
      break;
    } else if (errno != EINTR) {
      context->callback->error(errno);
      break;
    }
  }
}

void KqueueEventManager::writeAll(int fd, const void* buffer, int size,
                                  OwnedPtr<IoCallback>* callbackToAdopt,
                                  OwnedPtr<Canceler>* output) {
  OwnedPtr<WriteContext> context;
  context.allocate(buffer, size, callbackToAdopt);

  if (!writeCallbacks.adoptIfNew(fd, &context)) {
    throw std::invalid_argument("A write is already in progress on this fd.");
  }

  updateKqueue(kqueueFd, fd, EVFILT_WRITE, EV_ADD, 0, 0, &handleWriteAll);

  if (output != NULL) output->allocateSubclass<WriteCanceler>(this, fd);
}

void KqueueEventManager::handleWriteAll(KqueueEventManager* self, const struct kevent& event) {
  int fd = event.ident;

  DEBUG_INFO << "FD is writable: " << fd;

  WriteContext* context = self->writeCallbacks.get(fd);
  if (context == NULL) {
    DEBUG_ERROR << "FD not on Action's writeCallbacks map.";
    return;
  }

  try {
    while (true) {
      int bytesWritten = ::write(fd, context->buffer + context->pos, context->size - context->pos);

      if (bytesWritten >= 0) {
        context->pos += bytesWritten;
        if (context->pos == context->size) {
          context->callback->done(context->pos);
          break;
        }
        return;  // Not done; don't unregister.
      } else if (errno != EINTR) {
        context->callback->error(errno);
        break;
      }
    }
  } catch (...) {
    updateKqueue(self->kqueueFd, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    self->writeCallbacks.erase(fd);
    throw;
  }

  updateKqueue(self->kqueueFd, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
  self->writeCallbacks.erase(fd);
}

// =======================================================================================

class KqueueEventManager::ReadContinuouslyCanceler : public Canceler {
public:
  ReadContinuouslyCanceler(KqueueEventManager* manager, int fd) : manager(manager), fd(fd) {}
  ~ReadContinuouslyCanceler() {}

  // implements Canceler -----------------------------------------------------------------
  void cancel() {
    if (manager->continuousReadCallbacks.erase(fd)) {
      updateKqueue(manager->kqueueFd, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
  }

private:
  KqueueEventManager* manager;
  int fd;
};

void KqueueEventManager::readContinuously(
    int fd, OwnedPtr<ContinuousReadCallback>* callbackToAdopt, OwnedPtr<Canceler>* output) {
  if (readCallbacks.contains(fd) || !continuousReadCallbacks.adoptIfNew(fd, callbackToAdopt)) {
    throw std::invalid_argument("A read is already in progress on this fd.");
  }

  updateKqueue(kqueueFd, fd, EVFILT_READ, EV_ADD, 0, 0, &handleContinuousRead);

  if (output != NULL) output->allocateSubclass<ReadContinuouslyCanceler>(this, fd);
}

void KqueueEventManager::handleContinuousRead(
    KqueueEventManager* self, const struct kevent& event) {
  int fd = event.ident;

  DEBUG_INFO << "FD is readable: " << fd;

  ContinuousReadCallback* callback = self->continuousReadCallbacks.get(fd);
  if (callback == NULL) {
    DEBUG_ERROR << "FD not on Action's continuousReadCallbacks map.";
    return;
  }

  char buffer[8192];

  try {
    while (true) {
      int bytesRead = ::read(fd, buffer, sizeof(buffer));

      if (bytesRead == 0) {
        callback->eof();
        break;
      } else if (bytesRead > 0) {
        callback->data(buffer, bytesRead);
        return;  // Not done; don't unregister.
      } else if (errno != EINTR) {
        callback->error(errno);
        break;
      }
    }
  } catch (...) {
    updateKqueue(self->kqueueFd, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    self->continuousReadCallbacks.erase(fd);
    throw;
  }

  updateKqueue(self->kqueueFd, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  self->continuousReadCallbacks.erase(fd);
}

}  // namespace kake2
