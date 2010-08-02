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

#ifndef KAKE2_KQUEUEEVENTMANAGER_H_
#define KAKE2_KQUEUEEVENTMANAGER_H_

#include <sys/types.h>

#include "EventManager.h"
#include "OwnedPtr.h"

typedef struct kevent KEvent;

namespace kake2 {

class KqueueEventManager: public EventManager {
public:
  KqueueEventManager();
  ~KqueueEventManager();

  void loop();

  // implements EventManager -------------------------------------------------------------
  void runAsynchronously(OwnedPtr<Callback>* callbackToAdopt);
  void waitPid(pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt,
               OwnedPtr<Canceler>* output = NULL);
  void read(int fd, void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt,
            OwnedPtr<Canceler>* output = NULL);
  void readAll(int fd, void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt,
               OwnedPtr<Canceler>* output = NULL);
  void write(int fd, const void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt,
             OwnedPtr<Canceler>* output = NULL);
  void writeAll(int fd, const void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt,
                OwnedPtr<Canceler>* output = NULL);
  void readContinuously(int fd, OwnedPtr<ContinuousReadCallback>* callbackToAdopt,
                        OwnedPtr<Canceler>* output = NULL);

private:
  struct ReadContext {
    char* buffer;
    int size;
    int pos;
    OwnedPtr<IoCallback> callback;

    inline ReadContext(void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt)
        : buffer(reinterpret_cast<char*>(buffer)), size(size), pos(0) {
      callback.adopt(callbackToAdopt);
    }
  };

  struct WriteContext {
    const char* buffer;
    int size;
    int pos;
    OwnedPtr<IoCallback> callback;

    inline WriteContext(const void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt)
        : buffer(reinterpret_cast<const char*>(buffer)), size(size), pos(0) {
      callback.adopt(callbackToAdopt);
    }
  };

  class ProcessExitCanceler;
  class ReadCanceler;
  class WriteCanceler;
  class ReadContinuouslyCanceler;

  int kqueueFd;

  OwnedPtrQueue<Callback> asyncCallbacks;
  OwnedPtrMap<pid_t, ProcessExitCallback> processExitCallbacks;
  OwnedPtrMap<int, ReadContext> readCallbacks;
  OwnedPtrMap<int, WriteContext> writeCallbacks;
  OwnedPtrMap<int, ContinuousReadCallback> continuousReadCallbacks;

  bool handleEvent();

  typedef void EventHandlerFunc(KqueueEventManager* self, const KEvent& event);
  static void updateKqueue(int kqueueFd, uintptr_t ident, short filter, u_short flags,
                           u_int fflags, intptr_t data, EventHandlerFunc* handler);

  static void handleProcessExit(KqueueEventManager* self, const KEvent& event);
  static void handleRead(KqueueEventManager* self, const KEvent& event);
  static void handleReadAll(KqueueEventManager* self, const KEvent& event);
  static void handleWrite(KqueueEventManager* self, const KEvent& event);
  static void handleWriteAll(KqueueEventManager* self, const KEvent& event);
  static void handleContinuousRead(KqueueEventManager* self, const KEvent& event);
};

}  // namespace kake2

#endif  // KAKE2_KQUEUEEVENTMANAGER_H_
