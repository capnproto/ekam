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

#include "ByteStream.h"

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "base/Debug.h"

namespace ekam {

ByteStream::ReadAllCallback::~ReadAllCallback() {}

ByteStream::ByteStream(const std::string& path, int flags)
    : handle(path, WRAP_SYSCALL(open, path.c_str(), flags, 0666)) {}

ByteStream::ByteStream(const std::string& path, int flags, int mode)
    : handle(path, WRAP_SYSCALL(open, path.c_str(), flags, mode)) {}

ByteStream::ByteStream(int fd, const std::string& name)
    : handle(name, fd) {}

ByteStream::~ByteStream() {}

size_t ByteStream::read(void* buffer, size_t size) {
  return WRAP_SYSCALL(read, handle, buffer, size);
}

size_t ByteStream::write(const void* buffer, size_t size) {
  return WRAP_SYSCALL(write, handle, buffer, size);
}

void ByteStream::writeAll(const void* buffer, size_t size) {
  const char* cbuffer = reinterpret_cast<const char*>(buffer);

  while (size > 0) {
    ssize_t n = write(cbuffer, size);
    cbuffer += n;
    size -= n;
  }
}

void ByteStream::stat(struct stat* stats) {
  WRAP_SYSCALL(fstat, handle, stats);
}

class ByteStream::ReadEventCallback : public AsyncOperation {
public:
  ReadEventCallback(EventManager* eventManager, int fd, ReadAllCallback* callback)
      : eventManager(eventManager),
        watcher(eventManager->watchFd(fd)),
        fd(fd),
        callback(callback) {
    waitForReady();
  }
  ~ReadEventCallback() {}

private:
  EventManager* eventManager;
  OwnedPtr<EventManager::IoWatcher> watcher;
  int fd;
  ReadAllCallback* callback;
  Promise<void> readPromise;

  void waitForReady() {
    readPromise = eventManager->when(watcher->onReadable())(
      [this](Void) {
        ready();
      });
  }

  void ready() {
    char buffer[4096];
    ssize_t size;
    do {
      size = ::read(fd, buffer, sizeof(buffer));
    } while (size == -1 && errno == EINTR);

    if (size > 0) {
      callback->consume(buffer, size);
      waitForReady();
    } else if (size == 0) {
      watcher.clear();
      callback->eof();
    } else {
      int error = errno;
      watcher.clear();
      callback->error(error);
    }
  }
};

OwnedPtr<AsyncOperation> ByteStream::readAll(EventManager* eventManager,
                                             ReadAllCallback* callback) {
  return newOwned<ReadEventCallback>(eventManager, handle.get(), callback);
}

// =======================================================================================

Pipe::Pipe() {
  if (pipe(fds) != 0) {
    throw OsError("", "pipe", errno);
  }

  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
}

Pipe::~Pipe() {
  closeReadEnd();
  closeWriteEnd();
}

OwnedPtr<ByteStream> Pipe::releaseReadEnd() {
  auto result = newOwned<ByteStream>(fds[0], "pipe.readEnd");
  fds[0] = -1;
  return result.release();
}

OwnedPtr<ByteStream> Pipe::releaseWriteEnd() {
  auto result = newOwned<ByteStream>(fds[1], "pipe.writeEnd");
  fds[1] = -1;
  return result.release();
}

void Pipe::attachReadEndForExec(int target) {
  dup2(fds[0], target);
  closeReadEnd();
  closeWriteEnd();
}

void Pipe::attachWriteEndForExec(int target) {
  dup2(fds[1], target);
  closeReadEnd();
  closeWriteEnd();
}

void Pipe::closeReadEnd() {
  if (fds[0] != -1) {
    if (close(fds[0]) != 0) {
      DEBUG_ERROR << "close(pipe): " << strerror(errno);
    }
    fds[0] = -1;
  }
}

void Pipe::closeWriteEnd() {
  if (fds[1] != -1) {
    if (close(fds[1]) != 0) {
      DEBUG_ERROR << "close(pipe): " << strerror(errno);
    }
    fds[1] = -1;
  }
}

}  // namespace ekam
