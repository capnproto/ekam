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

#include "EventGroup.h"

#include "Debug.h"

namespace ekam {

EventGroup::ExceptionHandler::~ExceptionHandler() {}

// =======================================================================================

class EventGroup::CancelerWrapper : public Canceler {
public:
  CancelerWrapper(CallbackContext* inner) : inner(inner) {
    inner->cancelerWrapper = this;
  }
  ~CancelerWrapper() {
    if (inner != NULL) inner->cancelerWrapper = NULL;
  }

  static void wrap(CallbackContext* inner, OwnedPtr<Canceler>* output) {
    if (output != NULL) {
      output->allocateSubclass<CancelerWrapper>(inner);
    }
  }

  // implements Canceler -----------------------------------------------------------------
  void cancel() {
    if (inner != NULL) inner->canceler->cancel();
  }

private:
  friend struct CallbackContext;

  CallbackContext* inner;
};

EventGroup::CallbackContext::~CallbackContext() {
  if (cancelerWrapper != NULL) {
    cancelerWrapper->inner = NULL;
  }
}

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
  Status ready() {
    HANDLE_EXCEPTIONS(return wrapped->ready());

    // Control gets here if context.groupCanceled is true or if ready() threw an exception.
    // Return DONE so that this callback doesn't get called again.
    return DONE;
  }

private:
  EventGroup* group;
  OwnedPtr<IoCallback> wrapped;
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

void EventGroup::onProcessExit(pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt,
                               OwnedPtr<Canceler>* output) {
  OwnedPtr<ProcessExitCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<ProcessExitCallbackWrapper>(this, callbackToAdopt, &context);
  inner->onProcessExit(process, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context, output);
}

void EventGroup::onReadable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                            OwnedPtr<Canceler>* output) {
  OwnedPtr<IoCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<IoCallbackWrapper>(this, callbackToAdopt, &context);
  inner->onReadable(fd, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context, output);
}

void EventGroup::onWritable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                            OwnedPtr<Canceler>* output) {
  OwnedPtr<IoCallback> wrappedCallback;
  CallbackContext* context;
  wrappedCallback.allocateSubclass<IoCallbackWrapper>(this, callbackToAdopt, &context);
  inner->onWritable(fd, &wrappedCallback, &context->canceler);
  activeCallbacks.insert(context);
  CancelerWrapper::wrap(context, output);
}

}  // namespace ekam
