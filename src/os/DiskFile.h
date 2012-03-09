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

#ifndef EKAM_DISKFILE_H_
#define EKAM_DISKFILE_H_

#include "File.h"

namespace ekam {

class DiskFile: public File {
public:
  DiskFile(const std::string& path, File* parent);
  ~DiskFile();

  // implements File ---------------------------------------------------------------------
  std::string basename();
  std::string canonicalName();
  OwnedPtr<File> clone();
  bool hasParent();
  OwnedPtr<File> parent();

  bool equals(File* other);
  size_t identityHash();

  OwnedPtr<DiskRef> getOnDisk(Usage usage);

  bool exists();
  bool isFile();
  bool isDirectory();

  // File only.
  Hash contentHash();
  std::string readAll();
  void writeAll(const std::string& content);
  void writeAll(const void* data, int size);

  // Directory only.
  void list(OwnedPtrVector<File>::Appender output);
  OwnedPtr<File> relative(const std::string& path);

  // Methods that create or delete objects.
  void createDirectory();
  void link(File* target);
  void unlink();

private:
  class DiskRefImpl;

  std::string path;
  OwnedPtr<File> parentRef;
};

}  // namespace ekam

#endif  // EKAM_DISKFILE_H_
