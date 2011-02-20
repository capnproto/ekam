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

#ifndef EKAM_OS_EVENTMANAGER_H_
#define EKAM_OS_EVENTMANAGER_H_

#include <stddef.h>
#include <sys/types.h>
#include <string>
#include "base/OwnedPtr.h"
#include "base/Promise.h"

namespace ekam {

class ProcessExitCode {
public:
  ProcessExitCode(): signaled(false), exitCodeOrSignal(0) {}
  ProcessExitCode(int exitCode)
      : signaled(false), exitCodeOrSignal(exitCode) {}
  enum Signaled { SIGNALED };
  ProcessExitCode(Signaled, int signalNumber)
      : signaled(true), exitCodeOrSignal(signalNumber) {}

  bool wasSignaled() {
    return signaled;
  }

  int getExitCode() {
    if (signaled) {
      throwError();
    }
    return exitCodeOrSignal;
  }

  int getSignalNumber() {
    if (!signaled) {
      throwError();
    }
    return exitCodeOrSignal;
  }

private:
  bool signaled;
  int exitCodeOrSignal;

  void throwError();
};

// Any function which begins an operation that completes asynchronously should return an
// AsyncOperation.  Deleting the AsyncOperation immediately cancels it, freeing any resources
// associated with the operation.  Note that if there is a callback associated with an operation,
// that callback can be destroyed as soon as the AsyncOperation has been destroyed.  Note also
// that it is always safe to destroy an AsyncOperation *during* a callback.
class AsyncOperation {
public:
  virtual ~AsyncOperation();
};

class EventManager : public Executor {
public:
  virtual ~EventManager();

  class Callback {
  public:
    virtual ~Callback();

    virtual void run() = 0;
  };

  // Queue the callback to run in the event loop.
  virtual OwnedPtr<AsyncOperation> runAsynchronously(Callback* callback) = 0;

  // Fulfills the promise when the process exits.
  virtual Promise<ProcessExitCode> onProcessExit(pid_t pid) = 0;

  class IoCallback {
  public:
    virtual ~IoCallback();

    // File descriptor is ready for I/O.
    virtual void ready() = 0;
  };

  // Call the callback whenever the file descriptor is readable.
  virtual OwnedPtr<AsyncOperation> onReadable(int fd, IoCallback* callback) = 0;

  // Call the callback whenever the file descriptor is writable.
  virtual OwnedPtr<AsyncOperation> onWritable(int fd, IoCallback* callback) = 0;

  class FileChangeCallback {
  public:
    virtual ~FileChangeCallback();

    virtual void modified() = 0;
    virtual void deleted() = 0;
  };

  virtual OwnedPtr<AsyncOperation> onFileChange(const std::string& filename,
                                                FileChangeCallback* callback) = 0;
};

class RunnableEventManager : public EventManager {
public:
  virtual ~RunnableEventManager();

  virtual void loop() = 0;
};

OwnedPtr<RunnableEventManager> newPreferredEventManager();

}  // namespace ekam

#endif  // EKAM_OS_EVENTMANAGER_H_
