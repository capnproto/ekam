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

#include "dashboard.capnp.h"
#include <capnp/schema.h>
#include <capnp/serialize.h>
#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <stdexcept>
#include "ConsoleDashboard.h"
#include "base/OwnedPtr.h"

namespace ekam {

void dump(proto::TaskUpdate::Reader message) {
  using std::cerr;
  using std::cout;
  using std::endl;

  cout << "================================================================================\n";

  cout << message.getId() << ":";
  if (message.getState() != proto::TaskUpdate::State::UNCHANGED) {
    cout << " " << kj::str(message.getState()).cStr();
  }
  if (message.hasVerb()) {
    cout << " " << message.getVerb().cStr();
  }
  if (message.hasNoun()) {
    cout << " " << message.getNoun().cStr();
  }
  if (message.getSilent()) {
    cout << " (silent)";
  }
  cout << '\n';

  if (message.hasLog()) {
    auto log = message.getLog();
    cout << log.cStr();
    if (!log.endsWith("\n")) {
      cout << '\n';
    }
  }

  cout.flush();
}

Dashboard::TaskState toDashboardState(proto::TaskUpdate::State state) {
  switch (state) {
    case proto::TaskUpdate::State::PENDING:
      return Dashboard::PENDING;
    case proto::TaskUpdate::State::RUNNING:
      return Dashboard::RUNNING;
    case proto::TaskUpdate::State::DONE:
      return Dashboard::DONE;
    case proto::TaskUpdate::State::PASSED:
      return Dashboard::PASSED;
    case proto::TaskUpdate::State::FAILED:
      return Dashboard::FAILED;
    case proto::TaskUpdate::State::BLOCKED:
      return Dashboard::BLOCKED;
    default:
      throw std::invalid_argument(kj::str("Invalid state: ", state).cStr());
  }
}

int main(int argc, char* argv[]) {
  int maxDisplayedLogLines = 30;
  
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-l") == 0) {
      char* endptr;
      if (i + 1 >= argc ||
          (maxDisplayedLogLines = strtoul(argv[++i], &endptr, 0), *endptr != '\0')) {
        fprintf(stderr, "Expected number after -l.\n");
        return 1;
      }
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf(
          "usage: nc <host> <port> | %s [-l <count>]\n"
          "\n"
          "Connect to Ekam process at <host> <port> and display build status.\n"
          "\n"
          "options:\n"
          "  -l <count>    Set max number of log lines to display per action. This is\n"
          "                kept relatively short by default because it makes the build\n"
          "                output noisy, but you may need to increase it if you need\n"
          "                to see more of a particular error log.\n",
          argv[0]);
      return 0;
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      return 1;
    }
  }

  kj::FdInputStream rawInput(STDIN_FILENO);
  kj::BufferedInputStreamWrapper bufferedInput(rawInput);

  {
    capnp::InputStreamMessageReader message(bufferedInput);
    proto::Header::Reader header = message.getRoot<proto::Header>();
    printf("Project root: %s\n", header.getProjectRoot().cStr());
  }

  ConsoleDashboard dashboard(stdout, maxDisplayedLogLines);
  OwnedPtrMap<int, Dashboard::Task> tasks;

  while (bufferedInput.tryGetReadBuffer() != nullptr) {
    capnp::InputStreamMessageReader messageReader(bufferedInput);
    proto::TaskUpdate::Reader message = messageReader.getRoot<proto::TaskUpdate>();

    if (message.getState() == proto::TaskUpdate::State::DELETED) {
      tasks.erase(message.getId());
    } else if (Dashboard::Task* task = tasks.get(message.getId())) {
      if (message.hasLog()) {
        task->addOutput(message.getLog());
      }
      if (message.getState() != proto::TaskUpdate::State::UNCHANGED) {
        task->setState(toDashboardState(message.getState()));
      }
    } else {
      OwnedPtr<Dashboard::Task> newTask = dashboard.beginTask(
          message.getVerb(), message.getNoun(),
          message.getSilent() ? Dashboard::SILENT : Dashboard::NORMAL);
      if (message.hasLog()) {
        newTask->addOutput(message.getLog());
      }
      if (message.getState() != proto::TaskUpdate::State::UNCHANGED &&
          message.getState() != proto::TaskUpdate::State::PENDING) {
        newTask->setState(toDashboardState(message.getState()));
      }
      tasks.add(message.getId(), newTask.release());
    }
  }

  return 0;
}

}  // namespace ekam

int main(int argc, char* argv[]) {
  return ekam::main(argc, argv);
}
