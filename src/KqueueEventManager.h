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

#ifndef EKAM_KQUEUEEVENTMANAGER_H_
#define EKAM_KQUEUEEVENTMANAGER_H_

#include <sys/types.h>
#include <tr1/unordered_set>
#include <deque>

#include "EventManager.h"
#include "OwnedPtr.h"

typedef struct kevent KEvent;

namespace ekam {

class KqueueEventManager: public RunnableEventManager {
public:
  KqueueEventManager();
  ~KqueueEventManager();

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
  class KEventHandler;

  class AsyncCallbackHandler;
  class ProcessExitHandler;
  class ReadHandler;
  class WriteHandler;
  class FileChangeHandler;

  struct IntptrShortPairHash {
    inline bool operator()(const std::pair<intptr_t, short>& p) const {
      return p.first * 65537 + p.second;
    }
  };

  int kqueueFd;

  std::deque<AsyncCallbackHandler*> asyncCallbacks;
  std::deque<KEvent> fakeEvents;
  int handlerCount;

  bool handleEvent();

  void updateKqueue(const KEvent& event);
  void updateKqueue(uintptr_t ident, short filter, u_short flags,
                    KEventHandler* handler = NULL, u_int fflags = 0, intptr_t data = 0);
};

}  // namespace ekam

#endif  // EKAM_KQUEUEEVENTMANAGER_H_
