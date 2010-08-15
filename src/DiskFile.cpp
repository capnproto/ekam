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

#include "Debug.h"

namespace ekam {

// TODO:  Handle EINTR.  :(
// TODO:  Use new openat() and friends?  Very capability-like!
// TODO:  Better exception type (currently using std::runtime_error everywhere).

namespace {

class FileDescriptor {
public:
  FileDescriptor(const std::string& path, int flags)
      : path(path) {
    while (true) {
      fd = ::open(path.c_str(), flags, 0777);
      if (fd >= 0) {
        return;
      } else if (errno != EINTR) {
        // TODO:  Better exception type.
        throw std::runtime_error(path + ": open: " + strerror(errno));
      }
    }
  }

  ~FileDescriptor() {
    if (close(fd) < 0) {
      DEBUG_ERROR << "close: " << strerror(errno);
    }
  }

  ssize_t read(void* buffer, size_t size) {
    while (true) {
      ssize_t result = ::read(fd, buffer, size);
      if (result >= 0) {
        return result;
      } else if (errno != EINTR) {
        // TODO:  Better exception type.
        throw std::runtime_error(path + ": read: " + strerror(errno));
      }
    }
  }

  ssize_t write(const void* buffer, size_t size) {
    while (true) {
      ssize_t result = ::write(fd, buffer, size);
      if (result >= 0) {
        return result;
      } else if (errno != EINTR) {
        // TODO:  Better exception type.
        throw std::runtime_error(path + ": write: " + strerror(errno));
      }
    }
  }

  void stat(struct stat* stats) {
    while (true) {
      if (fstat(fd, stats) >= 0) {
        return;
      } else if (errno != EINTR) {
        // TODO:  Better exception type.
        throw std::runtime_error(path + ": fstat: " + strerror(errno));
      }
    }
  }

private:
  const std::string& path;
  int fd;
};

class DirectoryReader {
public:
  DirectoryReader(const std::string& path)
      : path(path),
        dir(opendir(path.c_str())) {
    if (dir == NULL) {
      // TODO:  Better exception type.
      throw std::runtime_error(path + ": Not a directory.");
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
      // TODO:  Better exception type.
      throw std::runtime_error(path + ": readdir: " + strerror(error));
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

bool statOrThrow(const std::string& path, struct stat* output) {
  while (true) {
    if (stat(path.c_str(), output) == 0) {
      return true;
    } else if (errno == ENOENT) {
      return false;
    } else if (errno != EINTR) {
      // TODO:  Better exception type.
      throw std::runtime_error(path + ": stat: " + strerror(errno));
    }
  }
}

}  // anonymous namespace

DiskFile::DiskFile(const std::string& path, File* parent) : path(path) {
  if (parent != NULL) {
    parent->clone(&this->parentRef);
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

std::string DiskFile::displayName() {
  if (path.empty()) {
    return ".";
  }

  return path;
}

void DiskFile::clone(OwnedPtr<File>* output) {
  output->allocateSubclass<DiskFile>(path, parentRef.get());
}

void DiskFile::parent(OwnedPtr<File>* output) {
  if (parentRef == NULL) {
    throw std::runtime_error("Tried to get parent of top-level directory: " + displayName());
  }
  parentRef->clone(output);
}

bool DiskFile::equals(File* other) {
  DiskFile* otherDiskFile = dynamic_cast<DiskFile*>(other);
  return otherDiskFile != NULL && otherDiskFile->path == path;
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

void DiskFile::getOnDisk(OwnedPtr<DiskRef>* output) {
  output->allocateSubclass<DiskRefImpl>(path);
}

bool DiskFile::exists() {
  struct stat stats;
  return statOrThrow(path.c_str(), &stats) && (S_ISREG(stats.st_mode) || S_ISDIR(stats.st_mode));
}

bool DiskFile::isFile() {
  struct stat stats;
  return statOrThrow(path.c_str(), &stats) && S_ISREG(stats.st_mode);
}

bool DiskFile::isDirectory() {
  struct stat stats;
  return statOrThrow(path.c_str(), &stats) && S_ISDIR(stats.st_mode);
}

// File only.
std::string DiskFile::readAll() {
  FileDescriptor fd(path, O_RDONLY);

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
  FileDescriptor fd(path, O_WRONLY | O_TRUNC | O_CREAT);

  std::string::size_type pos = 0;
  while (pos < content.size()) {
    pos += fd.write(content.data() + pos, content.size() - pos);
  }
}

void DiskFile::writeAll(const void* data, int size) {
  FileDescriptor fd(path, O_WRONLY | O_TRUNC | O_CREAT);

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
    if (filename != "." && filename != "..") {
      OwnedPtr<File> file;
      file.allocateSubclass<DiskFile>(prefix + filename, this);
      output.adopt(&file);
    }
  }
}

void DiskFile::relative(const std::string& path, OwnedPtr<File>* output) {
  if (path.empty()) {
    throw std::invalid_argument("File::relative(): path cannot be empty.");
  }

  std::string::size_type slash_pos = path.find_first_of('/');
  std::string first_part;
  std::string rest;
  if (slash_pos == std::string::npos) {
    if (path == ".") {
      clone(output);
    } else if (path == "..") {
      parent(output);
    } else if (this->path.empty()) {
      output->allocateSubclass<DiskFile>(path, this);
    } else {
      output->allocateSubclass<DiskFile>(this->path + "/" + path, this);
    }

  } else {
    first_part.assign(path, 0, slash_pos);
    rest.assign(path, slash_pos + 1, std::string::npos);

    if (first_part == ".") {
      relative(rest, output);
    } else if (first_part == "..") {
      if (parentRef == NULL) {
        throw std::runtime_error("Tried to get parent of top-level directory: " + displayName());
      }
      parentRef->relative(rest, output);
    } else {
      OwnedPtr<File> temp;
      if (this->path.empty()) {
        temp.allocateSubclass<DiskFile>(path, this);
      } else {
        temp.allocateSubclass<DiskFile>(this->path + "/" + path, this);
      }
      temp->relative(rest, output);
    }
  }
}

void DiskFile::createDirectory() {
  while (true) {
    if (mkdir(path.c_str(), 0777) == 0) {
      return;
    } else if (errno != EINTR) {
      // TODO:  Better exception type.
      throw std::runtime_error(path + ": mkdir: " + strerror(errno));
    }
  }
}

}  // namespace ekam
