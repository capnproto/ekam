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

#ifndef EKAM_EVENTMANAGER_H_
#define EKAM_EVENTMANAGER_H_

#include <stddef.h>
#include "OwnedPtr.h"

namespace ekam {

class EventManager {
public:
  virtual ~EventManager();

  class Canceler {
  public:
    virtual ~Canceler();

    // Cancel the event.  Note that this is just a hint -- the callback may still be called after
    // this returns.  Wait for the callback to be deleted before cleaning up.
    virtual void cancel() = 0;
  };

  class Callback {
  public:
    virtual ~Callback();

    virtual void run() = 0;
  };

  // Queue the callback to run in the event loop.
  virtual void runAsynchronously(OwnedPtr<Callback>* callbackToAdopt) = 0;

  class ProcessExitCallback {
  public:
    virtual ~ProcessExitCallback();

    virtual void exited(int exitCode) = 0;
    virtual void signaled(int signalNumber) = 0;
  };

  // Call the callback when the process exits.
  virtual void onProcessExit(pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt,
                             OwnedPtr<Canceler>* output = NULL) = 0;

  class IoCallback {
  public:
    virtual ~IoCallback();

    enum Status {
      DONE,
      REPEAT
    };

    // File descriptor is ready for I/O.  Return value indicates whether to continue waiting for
    // more I/O on the same descriptor using the same callback.
    virtual Status ready() = 0;
  };

  // Call the callback whenever the file descriptor is readable.
  virtual void onReadable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                          OwnedPtr<Canceler>* output = NULL) = 0;

  // Call the callback whenever the file descriptor is writable.
  virtual void onWritable(int fd, OwnedPtr<IoCallback>* callbackToAdopt,
                          OwnedPtr<Canceler>* output = NULL) = 0;
};

}  // namespace ekam

#endif  // EKAM_EVENTMANAGER_H_
