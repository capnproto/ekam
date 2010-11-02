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


#ifndef EKAM_EPOLLEVENTMANAGER_H_
#define EKAM_EPOLLEVENTMANAGER_H_

#include <sys/types.h>
#include <stdint.h>
#include <signal.h>
#include <deque>
#include <set>
#include <tr1/unordered_map>

#include "EventManager.h"
#include "OwnedPtr.h"

typedef struct pollfd PollFd;

namespace ekam {

class EpollEventManager : public RunnableEventManager {
public:
  EpollEventManager();
  ~EpollEventManager();

  // implements RunnableEventManager -----------------------------------------------------
  void loop();

  // implements EventManager -------------------------------------------------------------
  void runAsynchronously(Callback* callback, OwnedPtr<AsyncOperation>* output);
  void onProcessExit(pid_t pid, ProcessExitCallback* callback,
                     OwnedPtr<AsyncOperation>* output);
  void onReadable(int fd, IoCallback* callback, OwnedPtr<AsyncOperation>* output);
  void onWritable(int fd, IoCallback* callback, OwnedPtr<AsyncOperation>* output);
  void onFileChange(const std::string& filename, FileChangeCallback* callback,
                    OwnedPtr<AsyncOperation>* output);

private:
  class IoHandler;

  class AsyncCallbackHandler;
  class ProcessExitHandler;
  class ReadHandler;
  class WriteHandler;
  class WatchedDirectory;
  class WatchOperation;

  std::deque<AsyncCallbackHandler*> asyncCallbacks;
  std::tr1::unordered_map<pid_t, ProcessExitHandler*> processExitHandlerMap;
  std::tr1::unordered_map<int, IoHandler*> readHandlerMap;
  std::tr1::unordered_map<int, IoHandler*> writeHandlerMap;

  int inotifyFd;
  OwnedPtrMap<WatchedDirectory*, WatchedDirectory> ownedWatchDirectories;
  typedef std::tr1::unordered_map<int, WatchedDirectory*> WatchMap;
  WatchMap watchMap;
  typedef std::tr1::unordered_map<std::string, WatchedDirectory*> WatchByNameMap;
  WatchByNameMap watchByNameMap;
  std::set<WatchedDirectory*> currentlyHandlingWatches;

  bool handleEvent();
  void handleSignal(const siginfo_t& siginfo);
  void handleInotify(short pollFlags);
};

}  // namespace ekam

#endif  // EKAM_EPOLLEVENTMANAGER_H_
