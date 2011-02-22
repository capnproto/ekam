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

class EventManager : public Executor {
public:
  virtual ~EventManager();

  // Fulfills the promise when the process exits.
  virtual Promise<ProcessExitCode> onProcessExit(pid_t pid) = 0;

  class IoWatcher {
  public:
    virtual ~IoWatcher();

    // Fulfills the promise when the file descriptor is readable.
    virtual Promise<void> onReadable() = 0;
    // Fulfills the promise when the file descriptor is writable.
    virtual Promise<void> onWritable() = 0;
  };

  // Watch the file descriptor for readability and writability.
  virtual OwnedPtr<IoWatcher> watchFd(int fd) = 0;

  enum class FileChangeType {
    MODIFIED,
    DELETED
  };

  class FileWatcher {
  public:
    virtual ~FileWatcher();

    virtual Promise<FileChangeType> onChange() = 0;
  };

  // Watch a file (on disk) for changes or deletion.
  virtual OwnedPtr<FileWatcher> watchFile(const std::string& filename) = 0;
};

class RunnableEventManager : public EventManager {
public:
  virtual ~RunnableEventManager();

  virtual void loop() = 0;
};

OwnedPtr<RunnableEventManager> newPreferredEventManager();

}  // namespace ekam

#endif  // EKAM_OS_EVENTMANAGER_H_
