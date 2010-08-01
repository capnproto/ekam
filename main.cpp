// kake2 -- http://code.google.com/p/kake2
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
//     * Neither the name of the kake2 project nor the names of its
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

#include <string>
#include <stdio.h>

#include "Driver.h"
#include "DiskFile.h"
#include "Action.h"
#include "SimpleDashboard.h"

namespace kake2 {

class MockAction : public Action {
public:
  MockAction(bool result) : result(result) {}
  ~MockAction() {}

  // implements Action -------------------------------------------------------------------
  std::string getVerb() { return "mock"; }
  void start(BuildContext* context) {
    if (result) {
      context->success();
    } else {
      context->failed();
    }
  }

private:
  bool result;
};

class MockActionFactory : public ActionFactory {
public:
  MockActionFactory() {}
  ~MockActionFactory() {}

  // implements ActionFactory ------------------------------------------------------------
  void tryMakeAction(File* file, OwnedPtr<Action>* output) {
    std::string basename = file->basename();
    if (basename.size() > 4 && basename.substr(basename.size() - 4) == ".cpp") {
      output->allocateSubclass<MockAction>(basename != "main.cpp");
    }
  }
};

int main(int argc, char* argv) {
  DiskFile src("src", NULL);
  DiskFile tmp("tmp", NULL);
  SimpleDashboard dashboard(stdout);

  Driver driver(&dashboard, &src, &tmp);

  MockActionFactory mockFactory;
  driver.addActionFactory("mock", &mockFactory);

  driver.run(1);

  return 0;
}

}  // namespace kake2

int main(int argc, char* argv) {
  kake2::main(argc, argv);
}
