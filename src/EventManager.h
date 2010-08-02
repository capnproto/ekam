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

#ifndef KAKE2_EVENTMANAGER_H_
#define KAKE2_EVENTMANAGER_H_

#include <stddef.h>
#include "OwnedPtr.h"

namespace kake2 {

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

  virtual void runAsynchronously(OwnedPtr<Callback>* callbackToAdopt) = 0;

  class ProcessExitCallback {
  public:
    virtual ~ProcessExitCallback();

    virtual void exited(int exitCode) = 0;
    virtual void signaled(int signalNumber) = 0;
  };

  virtual void waitPid(pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt,
                       OwnedPtr<Canceler>* output = NULL) = 0;

  class IoCallback {
  public:
    virtual ~IoCallback();

    virtual void done(int bytesTransferred) = 0;
    virtual void error(int number) = 0;
  };

  // Wait until data is available on the fd, then read it and call the callback.
  virtual void read(int fd, void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt,
                    OwnedPtr<Canceler>* output = NULL) = 0;

  // Like read(), but if the initial read does not fill the buffer, read again into the unfilled
  // portion, and keep doing this until either the buffer is full or EOF.  The only way the
  // callback's done() method will be passed a value other than |size| is if EOF was reached.
  virtual void readAll(int fd, void* buffer, int size, OwnedPtr<IoCallback>* callbackToAdopt,
                       OwnedPtr<Canceler>* output = NULL) = 0;

  // Wait until there is space in the fd's buffer to write some data, then write from the given
  // buffer, and call the callback.
  virtual void write(int fd, const void* buffer, int size,
                     OwnedPtr<IoCallback>* callbackToAdopt,
                     OwnedPtr<Canceler>* output = NULL) = 0;

  // Like write(), but if the initial write does not consume the entire buffer, write again from
  // the unconsumed portion, and keep doing this until the whole buffer is written.  The callback's
  // done() method will always be passed |size| if called (though it of course won't be called in
  // case of error).
  virtual void writeAll(int fd, const void* buffer, int size,
                        OwnedPtr<IoCallback>* callbackToAdopt,
                        OwnedPtr<Canceler>* output = NULL) = 0;

  class ContinuousReadCallback {
  public:
    virtual ~ContinuousReadCallback();

    // Will be called repeatedly whenever data is available.
    virtual void data(const void* buffer, int size) = 0;

    // After eof() or error() is called, no more calls will be made.
    virtual void eof() = 0;
    virtual void error(int number) = 0;
  };

  // Repeatedly read from the fd and call the callback's data() method with each chunk of data
  // read.
  virtual void readContinuously(int fd, OwnedPtr<ContinuousReadCallback>* callbackToAdopt,
                                OwnedPtr<Canceler>* output = NULL) = 0;
};

}  // namespace kake2

#endif  // KAKE2_EVENTMANAGER_H_
