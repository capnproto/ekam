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

#include "dashboard.pb.h"
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/text_format.h>
#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <stdexcept>
#include "ConsoleDashboard.h"
#include "base/OwnedPtr.h"

namespace ekam {

using google::protobuf::io::ZeroCopyInputStream;
using google::protobuf::io::FileInputStream;
using google::protobuf::io::FileOutputStream;
using google::protobuf::io::CodedInputStream;
using google::protobuf::uint32;
using google::protobuf::TextFormat;
using google::protobuf::Message;

void dump(const proto::TaskUpdate& message) {
  using std::cerr;
  using std::cout;
  using std::endl;

  cout << "================================================================================\n";

  cout << message.id() << ":";
  if (message.has_state()) {
    cout << " " << proto::TaskUpdate::State_Name(message.state());
  }
  if (message.has_verb()) {
    cout << " " << message.verb();
  }
  if (message.has_noun()) {
    cout << " " << message.noun();
  }
  if (message.silent()) {
    cout << " (silent)";
  }
  cout << '\n';

  if (message.has_log()) {
    cout << message.log();
    if (message.log()[message.log().size() - 1] != '\n') {
      cout << '\n';
    }
  }

  cout.flush();
}

Dashboard::TaskState toDashboardState(proto::TaskUpdate::State state) {
  switch (state) {
    case proto::TaskUpdate::PENDING:
      return Dashboard::PENDING;
    case proto::TaskUpdate::RUNNING:
      return Dashboard::RUNNING;
    case proto::TaskUpdate::DONE:
      return Dashboard::DONE;
    case proto::TaskUpdate::PASSED:
      return Dashboard::PASSED;
    case proto::TaskUpdate::FAILED:
      return Dashboard::FAILED;
    case proto::TaskUpdate::BLOCKED:
      return Dashboard::BLOCKED;
    default:
      throw std::invalid_argument("Invalid state: " + proto::TaskUpdate::State_Name(state));
  }
}

bool readDelimited(ZeroCopyInputStream* rawInput, Message* message) {
  CodedInputStream input(rawInput);

  uint32 size;
  if (!input.ReadVarint32(&size)) {
    return false;
  }

  CodedInputStream::Limit limit = input.PushLimit(size);

  if (!message->MergePartialFromCodedStream(&input) ||
      !input.ConsumedEntireMessage()) {
    fprintf(stderr, "Read error.\n");
    exit(1);
  }

  input.PopLimit(limit);

  return true;
}

int main(int argc, char* argv[]) {
  FileInputStream rawInput(STDIN_FILENO);

  proto::Header header;
  if (!readDelimited(&rawInput, &header)) {
    return 0;
  }

  printf("Project root: %s\n", header.project_root().c_str());

  ConsoleDashboard dashboard(stdout);
  OwnedPtrMap<int, Dashboard::Task> tasks;

  while (true) {
    proto::TaskUpdate message;
    if (!readDelimited(&rawInput, &message)) {
      return 0;
    }

    if (message.has_state() && message.state() == proto::TaskUpdate::DELETED) {
      tasks.erase(message.id());
    } else if (Dashboard::Task* task = tasks.get(message.id())) {
      if (message.has_log()) {
        task->addOutput(message.log());
      }
      if (message.has_state()) {
        task->setState(toDashboardState(message.state()));
      }
    } else {
      OwnedPtr<Dashboard::Task> newTask = dashboard.beginTask(
          message.verb(), message.noun(),
          message.silent() ? Dashboard::SILENT : Dashboard::NORMAL);
      if (message.has_log()) {
        newTask->addOutput(message.log());
      }
      if (message.has_state() && message.state() != proto::TaskUpdate::PENDING) {
        newTask->setState(toDashboardState(message.state()));
      }
      tasks.add(message.id(), newTask.release());
    }
  }
}

}  // namespace ekam

int main(int argc, char* argv[]) {
  return ekam::main(argc, argv);
}
