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

#ifndef KENTONSCODE_OS_OSHANDLE_H_
#define KENTONSCODE_OS_OSHANDLE_H_

#include <stdexcept>
#include <errno.h>

#include "base/OwnedPtr.h"

namespace ekam {

class EventManager;

class OsHandle {
public:
  OsHandle(const std::string& name, int fd);
  ~OsHandle();

  const std::string& getName() const { return name; }
  int get() const { return fd; }

private:
  const std::string name;
  const int fd;
};

class OsError : public std::exception {
public:
  OsError(const std::string& path, const char* function, int errorNumber);
  OsError(const char* function, int errorNumber);
  virtual ~OsError() throw();

  virtual const char* what() const throw();

  inline int getErrorNumber() const { return errorNumber; }

private:
  std::string description;
  int errorNumber;
};

// TODO(kenton):  Factor out toString() module.
std::string toString(const char* arg);
std::string toString(int arg);
std::string toString(const OsHandle& arg);

template <typename T>
inline const T& toSyscallArg(const T& value) { return value; }
inline int toSyscallArg(const OsHandle& value) { return value.get(); }

template <typename Func>
long wrapSyscall(const char* name, const Func& func) {
  long result;
  do {
    result = func();
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    throw OsError(name, errno);
  }
  return result;
}

template <typename Func, typename Arg1, typename... Args>
long wrapSyscall(const char* name, const Func& func, Arg1&& arg1, Args&&... args) {
  long result;
  do {
    result = func(toSyscallArg(arg1), toSyscallArg(args)...);
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    throw OsError(toString(arg1), name, errno);
  }
  return result;
}

// The ## tells GCC to omit the preceding comma if __VA_ARGS__ is empty.  This is non-standard,
// but apparently MSVC will do the same.
#define WRAP_SYSCALL(FUNC, ...) (::ekam::wrapSyscall(#FUNC, ::FUNC, ##__VA_ARGS__))

}  // namespace ekam

#endif  // KENTONSCODE_OS_OSHANDLE_H_
