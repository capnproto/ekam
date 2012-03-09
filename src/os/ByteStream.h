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

#ifndef KENTONSCODE_OS_BYTESTREAM_H_
#define KENTONSCODE_OS_BYTESTREAM_H_

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
  Promise<size_t> readAsync(EventManager* eventManager, void* buffer, size_t size);
  size_t write(const void* buffer, size_t size);
  void writeAll(const void* buffer, size_t size);
  void stat(struct stat* stats);

private:
  class ReadEventCallback;

  OsHandle handle;
  OwnedPtr<EventManager::IoWatcher> watcher;
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

#endif  // KENTONSCODE_OS_BYTESTREAM_H_
