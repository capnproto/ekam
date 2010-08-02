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

#include "EventGroup.h"

#include "Debug.h"

namespace kake2 {

EventGroup::ExceptionHandler::~ExceptionHandler() {}

// =======================================================================================

class EventGroup::CancelerWrapper : public Canceler {
public:
  CancelerWrapper(Canceler* inner) : inner(inner) {}
  ~CancelerWrapper() {}

  static void wrap(Canceler* inner, OwnedPtr<Canceler>* output) {
    if (output != NULL) {
      output->allocateSubclass<CancelerWrapper>(inner);
    }
  }

  // implements Canceler -----------------------------------------------------------------
  void cancel() { inner->cancel(); }

private:
  Canceler* inner;
};

#define HANDLE_EXCEPTIONS(STATEMENT)                               \
  if (!context.groupCanceled) {                                    \
    try {                                                          \
      STATEMENT;                                                   \
    } catch (const std::exception& exception) {                    \
      group->exceptionHandler->threwException(exception);          \
    } catch (...) {                                                \
      group->exceptionHandler->threwUnknownException();            \
    }                                                              \
  }

class EventGroup::CallbackWrapper : public Callback {
public:
  CallbackWrapper(EventGroup* group, OwnedPtr<Callback>* wrappedToAdopt,
                  CallbackContext** saveContext)
      : group(group) {
    wrapped.adopt(wrappedToAdopt);
    *saveContext = &context;
  }
  ~CallbackWrapper() {
    group->activeCallbacks.erase(&context);
  }

  // implements ProcessExitCallback ------------------------------------------------------
  void run() { HANDLE_EXCEPTIONS(wrapped->run()); }

private:
  EventGroup* group;
  OwnedPtr<Callback> wrapped;
  CallbackContext context;
};

class EventGroup::ProcessExitCallbackWrapper : public ProcessExitCallback {
public:
  ProcessExitCallbackWrapper(EventGroup* group, OwnedPtr<ProcessExitCallback>* wrappedToAdopt,
                             CallbackContext** saveContext)
      : group(group) {
    wrapped.adopt(wrappedToAdopt);
    *saveContext = &context;
  }
  ~ProcessExitCallbackWrapper() {
    group->activeCallbacks.erase(&context);
  }

  // implements ProcessExitCallback ------------------------------------------------------
  void exited(int exitCode) { HANDLE_EXCEPTIONS(wrapped->exited(exitCode)); }
  void signaled(int signalNumber) { HANDLE_EXCEPTIONS(wrapped->signaled(signalNumber)); }

private:
  EventGroup* group;
  OwnedPtr<ProcessExitCallback> wrapped;
  CallbackContext context;
};

class EventGroup::IoCallbackWrapper : public IoCallback {
public:
  IoCallbackWrapper(EventGroup* group, OwnedPtr<IoCallback>* wrappedToAdopt,
                    CallbackContext** saveContext)
      : group(group) {
    wrapped.adopt(wrappedToAdopt);
    *saveContext = &context;
  }
  ~IoCallbackWrapper() {
    group->activeCallbacks.erase(&context);
  }

  // implements ProcessExitCallback ------------------------------------------------------
  void done(int bytesTransferred) { HANDLE_EXCEPTIONS(wrapped->done(bytesTransferred)); }
  void error(int number) { HANDLE_EXCEPTIONS(wrapped->error(number)); }

private:
  EventGroup* group;
  OwnedPtr<IoCallback> wrapped;
  CallbackContext context;
};

class EventGroup::ContinuousReadCallbackWrapper : public ContinuousReadCallback {
public:
  ContinuousReadCallbackWrapper(EventGroup* group,
                                OwnedPtr<ContinuousReadCallback>* wrappedToAdopt,
                                CallbackContext** saveContext)
      : group(group) {
    wrapped.adopt(wrappedToAdopt);
    *saveContext = &context;
  }
  ~ContinuousReadCallbackWrapper() {
    group->activeCallbacks.erase(&context);
  }

  // implements ProcessExitCallback ------------------------------------------------------
  void data(const void* buffer, int size) { HANDLE_EXCEPTIONS(wrapped->data(buffer, size)); }
  void eof() { HANDLE_EXCEPTIONS(wrapped->eof()); }
  void error(int number) { HANDLE_EXCEPTIONS(wrapped->error(number)); }

private:
  EventGroup* group;
  OwnedPtr<ContinuousReadCallback> wrapped;
  CallbackContext context;
};

#undef HANDLE_EXCEPTIONS

// =======================================================================================

EventGroup::EventGroup(EventManager* inner, ExceptionHandler* exceptionHandler)
    : inner(inner), exceptionHandler(exceptionHandler) {}

EventGroup::~EventGroup() {
  cancelAll();
}

void EventGroup::cancelAll() {
  // Calling cancel() may delete the callback which calls cancelers.erase(), so we need to
  // iterate over a copy of the list.
  std::vector<CallbackContext*> activeCallbacksCopy(activeCallbacks.begin(), activeCallbacks.end());

  for (unsigned int i = 0; i < activeCallbacksCopy.size(); i++) {
    activeCallbacksCopy[i]->groupCanceled = true;
    if (activeCallbacksCopy[i]->canceler != NULL) {
      activeCallbacksCopy[i]->canceler->cancel();
    }
  }
}

void EventGroup::runAsynchronously(OwnedPtr<Callback>* callbackToAdopt) {
  OwnedPtr<Callback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<CallbackWrapper>(this, callbackToAdopt, &context);
  inner->runAsynchronously(&wrappedCallback);
  activeCallbacks.insert(context);
}

void EventGroup::waitPid(pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt,
                         OwnedPtr<Canceler>* output) {
  OwnedPtr<ProcessExitCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<ProcessExitCallbackWrapper>(this, callbackToAdopt, &context);
  inner->waitPid(process, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context->canceler.get(), output);
}

void EventGroup::read(int fd, void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt,
                      OwnedPtr<Canceler>* output) {
  OwnedPtr<IoCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<IoCallbackWrapper>(this, callbackToAdopt, &context);
  inner->read(fd, buffer, size, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context->canceler.get(), output);
}

void EventGroup::readAll(int fd, void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt,
                         OwnedPtr<Canceler>* output) {
  OwnedPtr<IoCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<IoCallbackWrapper>(this, callbackToAdopt, &context);
  inner->readAll(fd, buffer, size, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context->canceler.get(), output);
}

void EventGroup::write(int fd, const void* buffer, int size,
                       OwnedPtr<IoCallback>* callbackToAdopt,
                       OwnedPtr<Canceler>* output) {
  OwnedPtr<IoCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<IoCallbackWrapper>(this, callbackToAdopt, &context);
  inner->write(fd, buffer, size, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context->canceler.get(), output);
}

void EventGroup::writeAll(int fd, const void* buffer, int size,
                          OwnedPtr<IoCallback>* callbackToAdopt,
                          OwnedPtr<Canceler>* output) {
  OwnedPtr<IoCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<IoCallbackWrapper>(this, callbackToAdopt, &context);
  inner->writeAll(fd, buffer, size, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context->canceler.get(), output);
}

void EventGroup::readContinuously(int fd, OwnedPtr<ContinuousReadCallback>* callbackToAdopt,
                                  OwnedPtr<Canceler>* output) {
  OwnedPtr<ContinuousReadCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<ContinuousReadCallbackWrapper>(this, callbackToAdopt, &context);
  inner->readContinuously(fd, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context->canceler.get(), output);
}

}  // namespace kake2
