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

#define HANDLE_EXCEPTIONS(STATEMENT)                             \
  EventGroup* group = this->group;                               \
  try {                                                          \
    STATEMENT;                                                   \
    if (group->eventCount == 0) {                                \
      group->exceptionHandler->noMoreEvents();                   \
    }                                                            \
  } catch (const std::exception& exception) {                    \
    group->exceptionHandler->threwException(exception);          \
  } catch (...) {                                                \
    group->exceptionHandler->threwUnknownException();            \
  }

class EventGroup::CallbackWrapper : public Callback, public AsyncOperation {
public:
  CallbackWrapper(EventGroup* group, Callback* wrapped)
      : group(group), wrapped(wrapped) {
    ++group->eventCount;
  }
  ~CallbackWrapper() {
    --group->eventCount;
  }

  OwnedPtr<AsyncOperation> inner;

  // implements Callback -----------------------------------------------------------------
  void run() { HANDLE_EXCEPTIONS(wrapped->run()); }

private:
  EventGroup* group;
  Callback* wrapped;
};

class EventGroup::ProcessExitCallbackWrapper : public ProcessExitCallback, public AsyncOperation {
public:
  ProcessExitCallbackWrapper(EventGroup* group, ProcessExitCallback* wrapped)
      : group(group), wrapped(wrapped), done(false) {
    ++group->eventCount;
  }
  ~ProcessExitCallbackWrapper() {
    if (!done) --group->eventCount;
  }

  OwnedPtr<AsyncOperation> inner;

  // implements ProcessExitCallback ------------------------------------------------------
  void exited(int exitCode) {
    --group->eventCount;
    done = true;
    HANDLE_EXCEPTIONS(wrapped->exited(exitCode));
  }
  void signaled(int signalNumber) {
    --group->eventCount;
    done = true;
    HANDLE_EXCEPTIONS(wrapped->signaled(signalNumber));
  }

private:
  EventGroup* group;
  ProcessExitCallback* wrapped;
  bool done;
};

class EventGroup::IoCallbackWrapper : public IoCallback, public AsyncOperation {
public:
  IoCallbackWrapper(EventGroup* group, IoCallback* wrapped)
      : group(group), wrapped(wrapped) {
    ++group->eventCount;
  }
  ~IoCallbackWrapper() {
    --group->eventCount;
  }

  OwnedPtr<AsyncOperation> inner;

  // implements ProcessExitCallback ------------------------------------------------------
  void ready() { HANDLE_EXCEPTIONS(wrapped->ready()); }

private:
  EventGroup* group;
  IoCallback* wrapped;
};

#undef HANDLE_EXCEPTIONS

// =======================================================================================

EventGroup::EventGroup(EventManager* inner, ExceptionHandler* exceptionHandler)
    : inner(inner), exceptionHandler(exceptionHandler), eventCount(0) {}

EventGroup::~EventGroup() {}

void EventGroup::runAsynchronously(Callback* callback, OwnedPtr<AsyncOperation>* output) {
  OwnedPtr<CallbackWrapper> wrappedCallback;
  wrappedCallback.allocate(this, callback);
  inner->runAsynchronously(wrappedCallback.get(), &wrappedCallback->inner);
  output->adopt(&wrappedCallback);
}

void EventGroup::onProcessExit(pid_t pid, ProcessExitCallback* callback,
                               OwnedPtr<AsyncOperation>* output) {
  OwnedPtr<ProcessExitCallbackWrapper> wrappedCallback;
  wrappedCallback.allocate(this, callback);
  inner->onProcessExit(pid, wrappedCallback.get(), &wrappedCallback->inner);
  output->adopt(&wrappedCallback);
}

void EventGroup::onReadable(int fd, IoCallback* callback, OwnedPtr<AsyncOperation>* output) {
  OwnedPtr<IoCallbackWrapper> wrappedCallback;
  wrappedCallback.allocate(this, callback);
  inner->onReadable(fd, wrappedCallback.get(), &wrappedCallback->inner);
  output->adopt(&wrappedCallback);
}

void EventGroup::onWritable(int fd, IoCallback* callback, OwnedPtr<AsyncOperation>* output) {
  OwnedPtr<IoCallbackWrapper> wrappedCallback;
  wrappedCallback.allocate(this, callback);
  inner->onWritable(fd, wrappedCallback.get(), &wrappedCallback->inner);
  output->adopt(&wrappedCallback);
}

}  // namespace ekam
