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

#ifndef EKAM_POLLEVENTMANAGER_H_
#define EKAM_POLLEVENTMANAGER_H_

#include <sys/types.h>
#include <stdint.h>
#include <signal.h>

#include "EventManager.h"
#include "EventHandler.h"
#include "OwnedPtr.h"

typedef struct pollfd PollFd;

namespace ekam {

class PollEventManager: public EventManager {
public:
  PollEventManager();
  ~PollEventManager();

  void loop();

  // implements EventManager -------------------------------------------------------------
  void runAsynchronously(OwnedPtr<Callback>* callbackToAdopt);
  void onProcessExit(pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt,
                     OwnedPtr<Canceler>* output = NULL);
  void onReadable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                  OwnedPtr<Canceler>* output = NULL);
  void onWritable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                  OwnedPtr<Canceler>* output = NULL);

private:
  class ProcessExitHandler;
  class ReadHandler;
  class WriteHandler;

  struct ProcessExitEvent {
    int waitStatus;
  };
  struct IoEvent {
    short pollFlags;
  };

  OwnedPtrMap<EventHandler<ProcessExitEvent>*, EventHandler<ProcessExitEvent> > processExitHandlers;
  OwnedPtrMap<EventHandler<IoEvent>*, EventHandler<IoEvent> > ioHandlers;
  EventHandlerRegistrarImpl<ProcessExitEvent> processExitHandlerRegistrar;
  EventHandlerRegistrarImpl<IoEvent> ioHandlerRegistrar;

  OwnedPtrQueue<Callback> asyncCallbacks;
  std::tr1::unordered_map<pid_t, EventHandler<ProcessExitEvent>*> processExitHandlerMap;
  std::tr1::unordered_map<int, EventHandler<IoEvent>*> readHandlerMap;
  std::tr1::unordered_map<int, EventHandler<IoEvent>*> writeHandlerMap;

  bool handleEvent();
  void handleSignal(const siginfo_t& siginfo);
};

}  // namespace ekam

#endif  // EKAM_POLLEVENTMANAGER_H_
