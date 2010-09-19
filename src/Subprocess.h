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

#ifndef EKAM_SUBPROCESS_H_
#define EKAM_SUBPROCESS_H_

#include <string>
#include <vector>

#include "OwnedPtr.h"
#include "EventManager.h"
#include "FileDescriptor.h"
#include "File.h"

namespace ekam {

class Subprocess {
public:
  Subprocess();
  ~Subprocess();

  void addArgument(const std::string& arg);
  File::DiskRef* addArgument(File* file, File::Usage usage);

  void captureStdin(OwnedPtr<FileDescriptor>* output);
  void captureStdout(OwnedPtr<FileDescriptor>* output);
  void captureStderr(OwnedPtr<FileDescriptor>* output);
  void captureStdoutAndStderr(OwnedPtr<FileDescriptor>* output);

  void start(EventManager* eventManager,
             EventManager::ProcessExitCallback* callback);

private:
  class CallbackWrapper;

  std::string executableName;
  bool doPathLookup;

  std::vector<std::string> args;
  OwnedPtrVector<File::DiskRef> diskRefs;

  OwnedPtr<Pipe> stdinPipe;
  OwnedPtr<Pipe> stdoutPipe;
  OwnedPtr<Pipe> stderrPipe;
  OwnedPtr<Pipe> stdoutAndStderrPipe;

  pid_t pid;
  OwnedPtr<AsyncOperation> waitOperation;
  OwnedPtr<CallbackWrapper> callbackWrapper;
};

}  // namespace ekam

#endif  // EKAM_SUBPROCESS_H_
