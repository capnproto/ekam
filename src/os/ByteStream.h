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

#ifndef EKAM_OS_BYTESTREAM_H_
#define EKAM_OS_BYTESTREAM_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <stdexcept>

#include "base/OwnedPtr.h"
#include "OsHandle.h"
#include "EventManager.h"

namespace ekam {

class EventManager;

class ByteStream {
public:
  // TODO(kenton):  Make class abstract.
  ByteStream(const std::string& path, int flags);
  ByteStream(const std::string& path, int flags, int mode);
  ByteStream(int fd, const std::string& name);
  ~ByteStream();

  inline OsHandle* getHandle() { return &handle; }

  size_t read(void* buffer, size_t size);
  size_t write(const void* buffer, size_t size);
  void writeAll(const void* buffer, size_t size);
  void stat(struct stat* stats);

  class ReadAllCallback {
  public:
    virtual ~ReadAllCallback();

    virtual void consume(const void* buffer, size_t size) = 0;
    virtual void eof() = 0;
    virtual void error(int number) = 0;
  };

  OwnedPtr<AsyncOperation> readAll(EventManager* eventManager, ReadAllCallback* callback);

private:
  class ReadEventCallback;

  OsHandle handle;
};

class Pipe {
public:
  Pipe();
  ~Pipe();

  OwnedPtr<ByteStream> releaseReadEnd();
  OwnedPtr<ByteStream> releaseWriteEnd();
  void attachReadEndForExec(int target);
  void attachWriteEndForExec(int target);

private:
  int fds[2];

  void closeReadEnd();
  void closeWriteEnd();
};

}  // namespace ekam

#endif  // EKAM_OS_BYTESTREAM_H_
