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

#ifndef KENTONSCODE_OS_POLLEVENTMANAGER_H_
#define KENTONSCODE_OS_POLLEVENTMANAGER_H_

#include <sys/types.h>
#include <stdint.h>
#include <signal.h>
#include <deque>
#include <tr1/unordered_map>

#include "EventManager.h"
#include "base/OwnedPtr.h"

typedef struct pollfd PollFd;

namespace ekam {

class PollEventManager : public RunnableEventManager {
public:
  PollEventManager();
  ~PollEventManager();

  // implements RunnableEventManager -----------------------------------------------------
  void loop();

  // implements EventManager -------------------------------------------------------------
  OwnedPtr<AsyncOperation> runAsynchronously(Callback* callback);
  OwnedPtr<AsyncOperation> onProcessExit(pid_t pid, ProcessExitCallback* callback);
  OwnedPtr<AsyncOperation> onReadable(int fd, IoCallback* callback);
  OwnedPtr<AsyncOperation> onWritable(int fd, IoCallback* callback);
  OwnedPtr<AsyncOperation> onFileChange(const std::string& filename, FileChangeCallback* callback);

private:
  class IoHandler;

  class AsyncCallbackHandler;
  class ProcessExitHandler;
  class ReadHandler;
  class WriteHandler;

  std::deque<AsyncCallbackHandler*> asyncCallbacks;
  std::tr1::unordered_map<pid_t, ProcessExitHandler*> processExitHandlerMap;
  std::tr1::unordered_map<int, IoHandler*> readHandlerMap;
  std::tr1::unordered_map<int, IoHandler*> writeHandlerMap;

  bool handleEvent();
  void handleSignal(const siginfo_t& siginfo);
};

}  // namespace ekam

#endif  // KENTONSCODE_OS_POLLEVENTMANAGER_H_
