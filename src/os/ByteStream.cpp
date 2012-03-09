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

#include "ByteStream.h"

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "base/Debug.h"

namespace ekam {

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

Promise<size_t> ByteStream::readAsync(EventManager* eventManager, void* buffer, size_t size) {
  if (watcher == nullptr) {
    watcher = eventManager->watchFd(handle.get());
  }
  return eventManager->when(watcher->onReadable())(
    [this, buffer, size](Void) -> size_t {
      return read(buffer, size);
    });
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
