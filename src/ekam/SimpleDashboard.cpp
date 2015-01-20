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

#include "SimpleDashboard.h"

namespace ekam {

class SimpleDashboard::TaskImpl : public Dashboard::Task {
public:
  TaskImpl(const std::string& verb, const std::string& noun, Silence silence, FILE* outputStream);
  ~TaskImpl();

  // implements Task ---------------------------------------------------------------------
  void setState(TaskState state);
  void addOutput(const std::string& text);

private:
  TaskState state;
  Silence silence;
  std::string verb;
  std::string noun;
  std::string outputText;
  FILE* outputStream;

  static const char* const STATE_NAMES[];
};

const char* const SimpleDashboard::TaskImpl::STATE_NAMES[] = {
  "PENDING",
  "RUNNING",
  "DONE   ",
  "PASSED ",
  "FAILED ",
  "BLOCKED"
};

SimpleDashboard::TaskImpl::TaskImpl(const std::string& verb, const std::string& noun,
                                    Silence silence, FILE* outputStream)
    : state(PENDING), silence(silence), verb(verb), noun(noun), outputStream(outputStream) {}
SimpleDashboard::TaskImpl::~TaskImpl() {}

void SimpleDashboard::TaskImpl::setState(TaskState state) {
  // If state was previously BLOCKED, and we managed to un-block, then we don't care about the
  // reason why we were blocked, so clear the text.
  if (this->state == BLOCKED && (state == PENDING || state == RUNNING)) {
    outputText.clear();
  }
  this->state = state;

  bool writeOutput = !outputText.empty() && state != BLOCKED;

  if (silence != SILENT || writeOutput) {
    // Write status update.
    fprintf(outputStream, "[%s] %s: %s\n", STATE_NAMES[state], verb.c_str(), noun.c_str());

    // Write any output we have buffered, unless new state is BLOCKED in which case we save the
    // output for later.
    if (writeOutput) {
      fwrite(outputText.c_str(), sizeof(char), outputText.size(), outputStream);
      if (outputText[outputText.size() - 1] != '\n') {
        fputc('\n', outputStream);
      }
      outputText.clear();
    }
  }
}

void SimpleDashboard::TaskImpl::addOutput(const std::string& text) {
  outputText.append(text);
}

// =======================================================================================

SimpleDashboard::SimpleDashboard(FILE* outputStream) : outputStream(outputStream) {}
SimpleDashboard::~SimpleDashboard() {}

OwnedPtr<Dashboard::Task> SimpleDashboard::beginTask(
    const std::string& verb, const std::string& noun, Silence silence) {
  return newOwned<TaskImpl>(verb, noun, silence, outputStream);
}

}  // namespace ekam
