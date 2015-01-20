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

#include "OsHandle.h"

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>

#include "base/Debug.h"

namespace ekam {

OsHandle::OsHandle(const std::string& name, int fd)
    : name(name), fd(fd) {
  if (fd < 0) {
    throw std::invalid_argument("Negative file descriptor given for: " + name);
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC);
}

OsHandle::~OsHandle() {
  if (close(fd) < 0) {
    DEBUG_ERROR << "close(" << name << "): " << strerror(errno);
  }
}

std::string toString(const char* arg) {
  return arg;
}
std::string toString(int arg) {
  std::stringstream sout(std::stringstream::out);
  sout << arg;
  return sout.str();
}
std::string toString(const OsHandle& arg) {
  return arg.getName();
}

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

OsError::OsError(const char* function, int errorNumber)
    : errorNumber(errorNumber) {
  if (function != NULL && *function != '\0') {
    description.append(function);
    description.append(": ");
  }
  description.append(strerror(errorNumber));
}

OsError::~OsError() throw() {}

const char* OsError::what() const throw() {
  return description.c_str();
}

}  // namespace ekam
