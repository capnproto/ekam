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
