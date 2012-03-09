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

#ifndef KENTONSCODE_OS_SUBPROCESS_H_
#define KENTONSCODE_OS_SUBPROCESS_H_

#include <string>
#include <vector>

#include "base/OwnedPtr.h"
#include "EventManager.h"
#include "ByteStream.h"
#include "File.h"

namespace ekam {

class Subprocess {
public:
  Subprocess();
  ~Subprocess();

  void addArgument(const std::string& arg);
  File::DiskRef* addArgument(File* file, File::Usage usage);

  OwnedPtr<ByteStream> captureStdin();
  OwnedPtr<ByteStream> captureStdout();
  OwnedPtr<ByteStream> captureStderr();
  OwnedPtr<ByteStream> captureStdoutAndStderr();

  Promise<ProcessExitCode> start(EventManager* eventManager);

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
};

}  // namespace ekam

#endif  // KENTONSCODE_OS_SUBPROCESS_H_
