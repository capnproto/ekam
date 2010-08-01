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

#include "SimpleDashboard.h"

namespace kake2 {

class SimpleDashboard::TaskImpl : public Dashboard::Task {
public:
  TaskImpl(const std::string& verb, const std::string& noun, FILE* outputStream);
  ~TaskImpl();

  // implements Task ---------------------------------------------------------------------
  void setState(TaskState state);
  void addOutput(const std::string& text);

private:
  TaskState state;
  std::string verb;
  std::string noun;
  std::string outputText;
  FILE* outputStream;

  static const char* const STATE_NAMES[];
};

static const char* const SimpleDashboard::TaskImpl::STATE_NAMES[] = {
  "PENDING",
  "RUNNING",
  "SUCCESS",
  "PASSED ",
  "FAILED ",
  "BLOCKED"
};

SimpleDashboard::TaskImpl::TaskImpl(const std::string& verb, const std::string& noun,
                                    FILE* outputStream)
    : state(PENDING), verb(verb), noun(noun), outputStream(outputStream) {}
SimpleDashboard::TaskImpl::~TaskImpl() {}

void SimpleDashboard::TaskImpl::setState(TaskState state) {
  // If state was previously BLOCKED, and we managed to un-block, then we don't care about the
  // reason why we were blocked, so clear the text.
  if (this->state == BLOCKED && (state == PENDING || state == RUNNING)) {
    outputText.clear();
  }
  this->state = state;

  // Write status update.
  fprintf(outputStream, "[%s] %s: %s\n", STATE_NAMES[state], verb.c_str(), noun.c_str());

  // Write any output we have buffered, unless new state is BLOCKED in which case we save the
  // output for later.
  if (!outputText.empty() && state != BLOCKED) {
    fwrite(outputText.c_str(), sizeof(char), outputText.size(), outputStream);
    if (outputText[outputText.size() - 1] != '\n') {
      fputc('\n', outputStream);
    }
    outputText.clear();
  }
}

void SimpleDashboard::TaskImpl::addOutput(const std::string& text) {
  outputText.append(text);
}

// =======================================================================================

SimpleDashboard::SimpleDashboard(FILE* outputStream) : outputStream(outputStream) {}
SimpleDashboard::~SimpleDashboard() {}

void SimpleDashboard::beginTask(const std::string& verb, const std::string& noun,
                                OwnedPtr<Task>* output) {
  output->allocateSubclass<TaskImpl>(verb, noun, outputStream);
}

}  // namespace kake2
