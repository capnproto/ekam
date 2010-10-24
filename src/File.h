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

#ifndef EKAM_FILE_H_
#define EKAM_FILE_H_

#include <vector>
#include <iterator>

#include "OwnedPtr.h"

namespace ekam {

class Hash;

class File {
public:
  virtual ~File();

  virtual std::string basename() = 0;
  virtual std::string canonicalName() = 0;
  virtual void clone(OwnedPtr<File>* output) = 0;
  virtual bool hasParent() = 0;
  virtual void parent(OwnedPtr<File>* output) = 0;

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

  virtual void getOnDisk(Usage usage, OwnedPtr<DiskRef>* output) = 0;

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
  virtual void relative(const std::string& path, OwnedPtr<File>* output) = 0;
  virtual void createDirectory() = 0;
};

void splitExtension(const std::string& name, std::string* base, std::string* ext);
void recursivelyCreateDirectory(File* location);

}  // namespace ekam

#endif  // EKAM_FILE_H_
