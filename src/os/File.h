// Ekam Build System
// Author: Kenton Varda (kenton@sandstorm.io)
// Copyright (c) 2010-2015 Kenton Varda, Google Inc., and contributors.
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

#ifndef KENTONSCODE_OS_FILE_H_
#define KENTONSCODE_OS_FILE_H_

#include <vector>
#include <iterator>

#include "base/OwnedPtr.h"

namespace ekam {

class Hash;

class File {
public:
  virtual ~File();

  virtual std::string basename() = 0;
  virtual std::string canonicalName() = 0;
  virtual OwnedPtr<File> clone() = 0;
  virtual bool hasParent() = 0;
  virtual OwnedPtr<File> parent() = 0;

  virtual bool equals(File* other) = 0;
  virtual size_t identityHash() = 0;

  class HashFunc {
  public:
    inline size_t operator()(File* file) const {
      return file->identityHash();
    }
  };
  struct EqualFunc {
    inline bool operator()(File* a, File* b) const {
      return a->equals(b);
    }
  };

  class DiskRef {
  public:
    virtual ~DiskRef();

    virtual const std::string& path() = 0;
  };

  enum Usage {
    READ,
    WRITE,
    UPDATE
  };

  virtual OwnedPtr<DiskRef> getOnDisk(Usage usage) = 0;

  virtual bool exists() = 0;
  virtual bool isFile() = 0;
  virtual bool isDirectory() = 0;

  // File only.
  virtual Hash contentHash() = 0;
  virtual std::string readAll() = 0;
  virtual void writeAll(const std::string& content) = 0;
  virtual void writeAll(const void* data, int size) = 0;

  // Directory only.
  virtual void list(OwnedPtrVector<File>::Appender output) = 0;
  virtual OwnedPtr<File> relative(const std::string& path) = 0;

  // Methods that create or delete objects.
  virtual void createDirectory() = 0;
  virtual void link(File* target) = 0;
  virtual void unlink() = 0;
};

void splitExtension(const std::string& name, std::string* base, std::string* ext);
void recursivelyCreateDirectory(File* location);

}  // namespace ekam

#endif  // KENTONSCODE_OS_FILE_H_
