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

#include "DiskFile.h"

#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <tr1/unordered_map>

#include "base/Debug.h"
#include "os/OsHandle.h"
#include "os/ByteStream.h"
#include "base/Hash.h"

namespace ekam {

// TODO:  Use new openat() and friends?  Very capability-like!

namespace {

class DirectoryReader {
public:
  DirectoryReader(const std::string& path)
      : path(path),
        dir(opendir(path.c_str())) {
    if (dir == NULL) {
      // TODO:  Better exception type.
      throw OsError(path, "opendir", ENOTDIR);
    }
  }

  ~DirectoryReader() {
    if (closedir(dir) < 0) {
      DEBUG_ERROR << path << ": closedir: " << strerror(errno);
    }
  }

  bool next(std::string* output) {
    struct dirent entry, *entryPointer;
    int error = readdir_r(dir, &entry, &entryPointer);
    if (error != 0) {
      throw OsError(path, "readdir", error);
    }

    if (entryPointer == NULL) {
      return false;
    } else {
      *output = entryPointer->d_name;
      return true;
    }
  }

private:
  const std::string& path;
  DIR* dir;
};

bool statIfExists(const std::string& path, struct stat* output) {
  int result;
  do {
    result = stat(path.c_str(), output);
  } while (result < 0 && errno == EINTR);

  if (result == 0) {
    return true;
  } else if (errno == ENOENT) {
    return false;
  } else {
    throw OsError(path, "stat", errno);
  }
}

}  // anonymous namespace

DiskFile::DiskFile(const std::string& path, File* parent) : path(path) {
  if (parent != NULL) {
    this->parentRef = parent->clone();
  }
}
DiskFile::~DiskFile() {}

std::string DiskFile::basename() {
  if (path.empty()) {
    return ".";
  }

  std::string::size_type slashPos = path.find_last_of('/');
  if (slashPos == std::string::npos) {
    return path;
  } else {
    return path.substr(slashPos + 1);
  }
}

std::string DiskFile::canonicalName() {
  if (parentRef == NULL) {
    return ".";
  }

  std::string result = parentRef->canonicalName();
  if (result == ".") {
    result.clear();
  } else {
    result.push_back('/');
  }
  result.append(basename());

  return result;
}

OwnedPtr<File> DiskFile::clone() {
  return newOwned<DiskFile>(path, parentRef.get());
}

bool DiskFile::hasParent() {
  return parentRef != NULL;
}

OwnedPtr<File> DiskFile::parent() {
  if (parentRef == NULL) {
    throw std::runtime_error("Tried to get parent of top-level directory: " + canonicalName());
  }
  return parentRef->clone();
}

bool DiskFile::equals(File* other) {
  DiskFile* otherDiskFile = dynamic_cast<DiskFile*>(other);
  return otherDiskFile != NULL && otherDiskFile->path == path;
}

size_t DiskFile::identityHash() {
  return std::tr1::hash<std::string>()(path);
}

class DiskFile::DiskRefImpl : public File::DiskRef {
public:
  DiskRefImpl(const std::string& path) : pathName(path) {}
  ~DiskRefImpl() {}

  // implements DiskRef ------------------------------------------------------------------
  virtual const std::string& path() { return pathName; }

private:
  std::string pathName;
};

OwnedPtr<File::DiskRef> DiskFile::getOnDisk(Usage usage) {
  return newOwned<DiskRefImpl>(path);
}

bool DiskFile::exists() {
  struct stat stats;
  return statIfExists(path.c_str(), &stats) && (S_ISREG(stats.st_mode) || S_ISDIR(stats.st_mode));
}

bool DiskFile::isFile() {
  struct stat stats;
  return statIfExists(path.c_str(), &stats) && S_ISREG(stats.st_mode);
}

bool DiskFile::isDirectory() {
  struct stat stats;
  return statIfExists(path.c_str(), &stats) && S_ISDIR(stats.st_mode);
}

// File only.
Hash DiskFile::contentHash() {
  try {
    Hash::Builder hasher;
    ByteStream fd(path, O_RDONLY);

    char buffer[8192];

    while (true) {
      size_t n = fd.read(buffer, sizeof(buffer));
      if (n == 0) {
        return hasher.build();
      }

      hasher.add(buffer, n);
    }
  } catch (const OsError& e) {
    if (e.getErrorNumber() == ENOENT || e.getErrorNumber() == EACCES ||
        e.getErrorNumber() == EISDIR) {
      return Hash::NULL_HASH;
    }
    throw;
  }
}

std::string DiskFile::readAll() {
  ByteStream fd(path, O_RDONLY);

  struct stat stats;
  fd.stat(&stats);
  size_t size = stats.st_size;

  std::string result;
  if (size > 0) {
    result.resize(size);
    char* buffer = &*result.begin();
    size_t bytesRead = 0;
    while (size > bytesRead) {
      ssize_t n = fd.read(buffer + bytesRead, size - bytesRead);

      if (n == 0) {
        result.resize(bytesRead);
        size = bytesRead;
      } else {
        bytesRead += n;
      }
    }
  }

  return result;
}

void DiskFile::writeAll(const std::string& content) {
  ByteStream fd(path, O_WRONLY | O_TRUNC | O_CREAT);

  std::string::size_type pos = 0;
  while (pos < content.size()) {
    pos += fd.write(content.data() + pos, content.size() - pos);
  }
}

void DiskFile::writeAll(const void* data, int size) {
  ByteStream fd(path, O_WRONLY | O_TRUNC | O_CREAT);

  const char* pos = reinterpret_cast<const char*>(data);
  while (size > 0) {
    int n = fd.write(pos, size);
    pos += n;
    size -= n;
  }
}

// Directory only.
void DiskFile::list(OwnedPtrVector<File>::Appender output) {
  std::string prefix;
  if (!path.empty()) {
    prefix = path + "/";
  }

  DirectoryReader reader(path);
  std::string filename;
  while (reader.next(&filename)) {
    if (filename.empty()) {
      DEBUG_ERROR << "DirectoryReader returned empty file name.";
    } else if (filename[0] != '.') {  // skip hidden files
      output.add(newOwned<DiskFile>(prefix + filename, this));
    }
  }
}

OwnedPtr<File> DiskFile::relative(const std::string& path) {
  if (path.empty()) {
    throw std::invalid_argument("File::relative(): path cannot be empty.");
  } else if (path[0] == '/') {
    throw std::invalid_argument("File::relative(): path cannot start with a slash.");
  }

  std::string::size_type slash_pos = path.find_first_of('/');
  std::string first_part;
  std::string rest;
  if (slash_pos == std::string::npos) {
    if (path == ".") {
      return clone();
    } else if (path == "..") {
      return parent();
    } else if (this->path.empty()) {
      return newOwned<DiskFile>(path, this);
    } else {
      return newOwned<DiskFile>(this->path + "/" + path, this);
    }

  } else {
    first_part.assign(path, 0, slash_pos);

    std::string::size_type after_slash_pos = path.find_first_not_of('/', slash_pos);

    if (after_slash_pos == std::string::npos) {
      // Trailing slash.  Bah.
      return relative(first_part);
    } else {
      rest.assign(path, after_slash_pos, std::string::npos);

      if (first_part == ".") {
        return relative(rest);
      } else if (first_part == "..") {
        if (parentRef == NULL) {
          throw std::runtime_error(
              "Tried to get parent of top-level directory: " + canonicalName());
        }
        return parentRef->relative(rest);
      } else {
        OwnedPtr<File> temp;
        if (this->path.empty()) {
          temp = newOwned<DiskFile>(first_part, this);
        } else {
          temp = newOwned<DiskFile>(this->path + "/" + first_part, this);
        }
        return temp->relative(rest);
      }
    }
  }
}

void DiskFile::createDirectory() {
  while (true) {
    if (mkdir(path.c_str(), 0777) == 0) {
      return;
    } else if (errno != EINTR) {
      throw OsError(path, "mkdir", errno);
    }
  }
}

void DiskFile::link(File* target) {
  DiskFile* diskTarget = dynamic_cast<DiskFile*>(target);
  if (diskTarget == NULL) {
    throw new std::invalid_argument("Cannot link disk file to non-disk file: " + path);
  }

  WRAP_SYSCALL(link, diskTarget->path.c_str(), path.c_str());
}

void DiskFile::unlink() {
  WRAP_SYSCALL(unlink, path.c_str());
}

}  // namespace ekam
