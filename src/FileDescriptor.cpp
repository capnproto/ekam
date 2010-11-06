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

#include "FileDescriptor.h"

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "Debug.h"

namespace ekam {

FileDescriptor::ReadAllCallback::~ReadAllCallback() {}

FileDescriptor::FileDescriptor(const std::string& path, int flags)
    : path(path) {
  do {
    fd = ::open(path.c_str(), flags, 0666);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0) {
    throw OsError(path, "open", errno);
  }

  fcntl(fd, F_SETFD, FD_CLOEXEC);
}

FileDescriptor::FileDescriptor(const std::string& path, int flags, int mode)
    : path(path) {
  do {
    fd = ::open(path.c_str(), flags, mode);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0) {
    throw OsError(path, "open", errno);
  }

  fcntl(fd, F_SETFD, FD_CLOEXEC);
}

FileDescriptor::FileDescriptor(int fd, const std::string& name) : path(name), fd(fd) {
  fcntl(fd, F_SETFD, FD_CLOEXEC);
}

FileDescriptor::~FileDescriptor() {
  if (close(fd) < 0) {
    DEBUG_ERROR << "close(" << path << "): " << strerror(errno);
  }
}

size_t FileDescriptor::read(void* buffer, size_t size) {
  while (true) {
    ssize_t result = ::read(fd, buffer, size);
    if (result >= 0) {
      return result;
    } else if (errno != EINTR) {
      throw OsError(path, "read", errno);
    }
  }
}

size_t FileDescriptor::write(const void* buffer, size_t size) {
  while (true) {
    ssize_t result = ::write(fd, buffer, size);
    if (result >= 0) {
      return result;
    } else if (errno != EINTR) {
      throw OsError(path, "write", errno);
    }
  }
}

void FileDescriptor::writeAll(const void* buffer, size_t size) {
  const char* cbuffer = reinterpret_cast<const char*>(buffer);

  while (size > 0) {
    ssize_t n = write(cbuffer, size);
    cbuffer += n;
    size -= n;
  }
}

void FileDescriptor::stat(struct stat* stats) {
  while (true) {
    if (fstat(fd, stats) >= 0) {
      return;
    } else if (errno != EINTR) {
      throw OsError(path, "fstat", errno);
    }
  }
}

class FileDescriptor::ReadEventCallback : public AsyncOperation, public EventManager::IoCallback {
public:
  ReadEventCallback(int fd, ReadAllCallback* callback)
      : fd(fd), callback(callback) {}
  ~ReadEventCallback() {}

  OwnedPtr<AsyncOperation> inner;

  // implements IoCallback ---------------------------------------------------------------
  void ready() {
    char buffer[4096];
    ssize_t size;
    do {
      size = ::read(fd, buffer, sizeof(buffer));
    } while (size == -1 && errno == EINTR);

    if (size > 0) {
      callback->consume(buffer, size);
    } else if (size == 0) {
      callback->eof();
      inner.clear();
    } else {
      callback->error(errno);
      inner.clear();
    }
  }

private:
  int fd;
  ReadAllCallback* callback;
};

void FileDescriptor::readAll(EventManager* eventManager,
                             ReadAllCallback* callback,
                             OwnedPtr<AsyncOperation>* output) {
  OwnedPtr<ReadEventCallback> eventCallback;
  eventCallback.allocate(fd, callback);
  eventManager->onReadable(fd, eventCallback.get(), &eventCallback->inner);
  output->adopt(&eventCallback);
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

void Pipe::releaseReadEnd(OwnedPtr<FileDescriptor>* output) {
  output->allocate(fds[0], "pipe.readEnd");
  fds[0] = -1;
}

void Pipe::releaseWriteEnd(OwnedPtr<FileDescriptor>* output) {
  output->allocate(fds[1], "pipe.writeEnd");
  fds[1] = -1;
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

// =======================================================================================

OsError::OsError(const std::string& path, const char* function, int errorNumber)
    : errorNumber(errorNumber) {
  if (function != NULL && *function != '\0') {
    description.append(function);
    if (!path.empty()) {
      description.append("(");
      description.append(path);
      description.append(")");
    }
    description.append(": ");
  } else if (!path.empty()) {
    description.append(path);
    description.append(": ");
  }
  description.append(strerror(errorNumber));
}

OsError::~OsError() throw() {}

const char* OsError::what() const throw() {
  return description.c_str();
}

}  // namespace ekam
