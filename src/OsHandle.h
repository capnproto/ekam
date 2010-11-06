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

#ifndef EKAM_OSHANDLE_H_
#define EKAM_OSHANDLE_H_

#include <stdexcept>
#include <errno.h>

#include "OwnedPtr.h"

namespace ekam {

class EventManager;

class OsHandle {
public:
  OsHandle(const std::string& name, int fd);
  ~OsHandle();

  const std::string& getName() const { return name; }
  int get() const { return fd; }

private:
  std::string name;
  int fd;
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

template <typename Func, typename Arg1>
long wrapSyscall(const char* name, const Func& func, const Arg1& arg1) {
  long result;
  do {
    result = func(toSyscallArg(arg1));
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    throw OsError(toString(arg1), name, errno);
  }
  return result;
}

template <typename Func, typename Arg1, typename Arg2>
long wrapSyscall(const char* name, const Func& func, const Arg1& arg1, const Arg2& arg2) {
  long result;
  do {
    result = func(toSyscallArg(arg1), toSyscallArg(arg2));
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    throw OsError(toString(arg1), name, errno);
  }
  return result;
}

template <typename Func, typename Arg1, typename Arg2, typename Arg3>
long wrapSyscall(const char* name, const Func& func, const Arg1& arg1,
                 const Arg2& arg2, const Arg3& arg3) {
  long result;
  do {
    result = func(toSyscallArg(arg1), toSyscallArg(arg2), toSyscallArg(arg3));
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    throw OsError(toString(arg1), name, errno);
  }
  return result;
}

#define WRAP_SYSCALL(FUNC, ...) (::ekam::wrapSyscall(#FUNC, ::FUNC, __VA_ARGS__))

}  // namespace ekam

#endif  // EKAM_OSHANDLE_H_
