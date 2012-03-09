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

#include "Subprocess.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "OsHandle.h"
#include "base/Debug.h"

namespace ekam {

Subprocess::Subprocess() : doPathLookup(false), pid(-1) {}

Subprocess::~Subprocess() {
  if (pid >= 0) {
    DEBUG_INFO << "Killing pid: " << pid;
    kill(pid, SIGKILL);
    int dummy;
    waitpid(pid, &dummy, 0);
  }
}

void Subprocess::addArgument(const std::string& arg) {
  if (args.empty()) {
    executableName = arg;
    doPathLookup = true;
  }
  args.push_back(arg);
}

File::DiskRef* Subprocess::addArgument(File* file, File::Usage usage) {
  OwnedPtr<File::DiskRef> diskRef = file->getOnDisk(usage);

  if (args.empty()) {
    executableName = diskRef->path();
    doPathLookup = false;
  }
  args.push_back(diskRef->path());

  File::DiskRef* result = diskRef.get();
  diskRefs.add(diskRef.release());
  return result;
}

OwnedPtr<ByteStream> Subprocess::captureStdin() {
  stdinPipe = newOwned<Pipe>();
  return stdinPipe->releaseWriteEnd();
}

OwnedPtr<ByteStream> Subprocess::captureStdout() {
  stdoutPipe = newOwned<Pipe>();
  stdoutAndStderrPipe.clear();
  return stdoutPipe->releaseReadEnd();
}

OwnedPtr<ByteStream> Subprocess::captureStderr() {
  stderrPipe = newOwned<Pipe>();
  stdoutAndStderrPipe.clear();
  return stderrPipe->releaseReadEnd();
}

OwnedPtr<ByteStream> Subprocess::captureStdoutAndStderr() {
  stdoutAndStderrPipe = newOwned<Pipe>();
  stdoutPipe.clear();
  stderrPipe.clear();
  return stdoutAndStderrPipe->releaseReadEnd();
}

Promise<ProcessExitCode> Subprocess::start(EventManager* eventManager) {
  pid = fork();

  if (pid < 0) {
    throw OsError("", "fork", errno);
  } else if (pid == 0) {
    // In child.

    std::vector<char*> argv;
    std::string command;

    for (unsigned int i = 0; i < args.size(); i++) {
      argv.push_back(strdup(args[i].c_str()));

      if (i > 0) command.push_back(' ');
      command.append(args[i]);
    }

    argv.push_back(NULL);

    DEBUG_INFO << "exec: " << command;

    if (stdinPipe != NULL) {
      stdinPipe->attachReadEndForExec(STDIN_FILENO);
    }
    if (stdoutPipe != NULL) {
      stdoutPipe->attachWriteEndForExec(STDOUT_FILENO);
    }
    if (stderrPipe != NULL) {
      stderrPipe->attachWriteEndForExec(STDERR_FILENO);
    }
    if (stdoutAndStderrPipe != NULL) {
      stdoutAndStderrPipe->attachWriteEndForExec(STDOUT_FILENO);
      dup2(STDOUT_FILENO, STDERR_FILENO);
    }

    if (doPathLookup) {
      execvp(executableName.c_str(), &argv[0]);
    } else {
      execv(executableName.c_str(), &argv[0]);
    }

    perror("exec");
    exit(1);
  } else {
    if (stdoutPipe != NULL) {
      stdoutPipe.clear();
    }
    if (stdinPipe != NULL) {
      stdinPipe.clear();
    }
    if (stderrPipe != NULL) {
      stderrPipe.clear();
    }
    if (stdoutAndStderrPipe != NULL) {
      stdoutAndStderrPipe.clear();
    }

    return eventManager->when(eventManager->onProcessExit(pid))(
      [this](ProcessExitCode exitCode) -> ProcessExitCode {
        pid = -1;
        return exitCode;
      });
  }
}

}  // namespace ekam
