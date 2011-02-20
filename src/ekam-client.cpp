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

int main(int argc, char* argv[]) {
  using google::protobuf::io::FileInputStream;
  using google::protobuf::io::FileOutputStream;
  using google::protobuf::io::CodedInputStream;
  using google::protobuf::uint32;
  using google::protobuf::TextFormat;

  ConsoleDashboard dashboard(stdout);
  OwnedPtrMap<int, Dashboard::Task> tasks;

  FileInputStream rawInput(STDIN_FILENO);

  while (true) {
    CodedInputStream input(&rawInput);

    uint32 size;
    if (!input.ReadVarint32(&size)) {
      return 0;
    }

    CodedInputStream::Limit limit = input.PushLimit(size);

    proto::TaskUpdate message;
    if (!message.MergePartialFromCodedStream(&input) ||
        !input.ConsumedEntireMessage()) {
      fprintf(stderr, "Read error.\n");
      return 1;
    }

    input.PopLimit(limit);

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
