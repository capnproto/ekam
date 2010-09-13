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

#include "ConsoleDashboard.h"

#include <sys/ioctl.h>
#include <termios.h>

#include "Debug.h"

namespace ekam {

class ConsoleDashboard::TaskImpl : public Dashboard::Task {
public:
  TaskImpl(ConsoleDashboard* dashboard, const std::string& verb,
           const std::string& noun, Silence silence);
  ~TaskImpl();

  // implements Task ---------------------------------------------------------------------
  void setState(TaskState state);
  void addOutput(const std::string& text);

private:
  ConsoleDashboard* dashboard;
  TaskState state;
  Silence silence;
  std::string verb;
  std::string noun;
  std::string outputText;

  void removeFromRunning();
  void writeFinalLog(Color verbColor);

  friend class ConsoleDashboard;
};

ConsoleDashboard::TaskImpl::TaskImpl(ConsoleDashboard* dashboard,
                                     const std::string& verb, const std::string& noun,
                                     Silence silence)
    : dashboard(dashboard), state(PENDING), silence(silence), verb(verb), noun(noun) {}
ConsoleDashboard::TaskImpl::~TaskImpl() {
  if (state == RUNNING) {
    dashboard->clearRunning();
    removeFromRunning();
    dashboard->drawRunning();
  }
}

void ConsoleDashboard::TaskImpl::setState(TaskState state) {
  // If state was previously BLOCKED, and we managed to un-block, then we don't care about the
  // reason why we were blocked, so clear the text.
  if (this->state == BLOCKED && (state == PENDING || state == RUNNING)) {
    outputText.clear();
  }

  if (this->state == RUNNING) {
    if (silence != SILENT) removeFromRunning();
  }

  this->state = state;

  dashboard->clearRunning();

  switch (state) {
    case PENDING:
      // Don't display.
      break;
    case RUNNING:
      if (silence != SILENT) dashboard->runningTasks.push_back(this);
      break;
    case DONE:
      writeFinalLog(DONE_COLOR);
      break;
    case PASSED:
      writeFinalLog(PASSED_COLOR);
      break;
    case FAILED:
      writeFinalLog(FAILED_COLOR);
      break;
    case BLOCKED:
      // Don't display.
      break;
  }

  dashboard->drawRunning();
}

void ConsoleDashboard::TaskImpl::addOutput(const std::string& text) {
  outputText.append(text);
}

void ConsoleDashboard::TaskImpl::removeFromRunning() {
  for (std::vector<TaskImpl*>::iterator iter = dashboard->runningTasks.begin();
       iter != dashboard->runningTasks.end(); ++iter) {
    if (*iter == this) {
      dashboard->runningTasks.erase(iter);
      break;
    }
  }
}

void ConsoleDashboard::TaskImpl::writeFinalLog(Color verbColor) {
  // Silent tasks should not be written to the log, unless they had error messages.
  if (silence != SILENT || !outputText.empty()) {
    fprintf(dashboard->out, "%s%s:%s %s\n",
            ANSI_COLOR_CODES[verbColor], verb.c_str(), ANSI_CLEAR_COLOR, noun.c_str());

    // Write any output we have buffered.
    // TODO:  Indent the text we write, and wrap it nicely.
    if (!outputText.empty()) {
      fwrite(outputText.c_str(), sizeof(char), outputText.size(), dashboard->out);
      if (outputText[outputText.size() - 1] != '\n') {
        fputc('\n', dashboard->out);
      }
      outputText.clear();
    }
  }
}

// =======================================================================================

const char* const ConsoleDashboard::ANSI_COLOR_CODES[] = {
  "\033[30m",
  "\033[31m",
  "\033[32m",
  "\033[33m",
  "\033[34m",
  "\033[35m",
  "\033[36m",
  "\033[37m",
  "\033[1;30m",
  "\033[1;31m",
  "\033[1;32m",
  "\033[1;33m",
  "\033[1;34m",
  "\033[1;35m",
  "\033[1;36m",
  "\033[1;37m"
};

const char* const ConsoleDashboard::ANSI_CLEAR_COLOR = "\033[0m";

// Note:  \033[%dF (move cursor up %d lines and to beginning of line) doesn't work on some
//   terminals.  \033[%dA (move cursor up %d lines) does appear to work, so tack \r on to that
//   to go to the beginning of the line.
const char* const ConsoleDashboard::ANSI_MOVE_CURSOR_UP = "\033[%dA\r";
const char* const ConsoleDashboard::ANSI_CLEAR_BELOW_CURSOR = "\033[0J";

const ConsoleDashboard::Color ConsoleDashboard::DONE_COLOR = BRIGHT_BLUE;
const ConsoleDashboard::Color ConsoleDashboard::PASSED_COLOR = BRIGHT_GREEN;
const ConsoleDashboard::Color ConsoleDashboard::FAILED_COLOR = BRIGHT_RED;
const ConsoleDashboard::Color ConsoleDashboard::RUNNING_COLOR = BRIGHT_FUCHSIA;

ConsoleDashboard::ConsoleDashboard(FILE* output)
    : fd(fileno(output)), out(output), runningTasksLineCount(0),
      lastDebugMessageCount(DebugMessage::getMessageCount()) {}
ConsoleDashboard::~ConsoleDashboard() {}

void ConsoleDashboard::beginTask(const std::string& verb, const std::string& noun,
                                 Silence silence, OwnedPtr<Task>* output) {
  output->allocateSubclass<TaskImpl>(this, verb, noun, silence);
}

void ConsoleDashboard::clearRunning() {
  if (lastDebugMessageCount != DebugMessage::getMessageCount()) {
    // Some debug messages were printed.  We don't want to clobber them.  So we can't clear.
    return;
  }

  if (runningTasksLineCount > 0) {
    fprintf(out, ANSI_MOVE_CURSOR_UP, runningTasksLineCount);
    fputs(ANSI_CLEAR_BELOW_CURSOR, out);
  }
}

void ConsoleDashboard::drawRunning() {
  struct winsize windowSize;
  ioctl(fd, TIOCGWINSZ, &windowSize);

  // Leave a few lines for completed tasks.
  int spaceForTasks = windowSize.ws_row - 4;

  bool allTasksShown = static_cast<int>(runningTasks.size()) < spaceForTasks;
  int displayCount = allTasksShown ? runningTasks.size() : spaceForTasks - 1;
  runningTasksLineCount = allTasksShown ? runningTasks.size() : spaceForTasks;

  for (int i = 0; i < displayCount; i++) {
    TaskImpl* task = runningTasks[i];
    int spaceForNoun = windowSize.ws_col - task->verb.size() - 2;

    fprintf(out, "%s%s:%s ", ANSI_COLOR_CODES[RUNNING_COLOR], task->verb.c_str(), ANSI_CLEAR_COLOR);

    if (static_cast<int>(task->noun.size()) > spaceForNoun) {
      std::string shortenedNoun = task->noun.substr(task->noun.size() - spaceForNoun + 3);
      fprintf(out, "...%s", shortenedNoun.c_str());
    } else {
      fputs(task->noun.c_str(), out);
    }

    putc('\n', out);
  }

  if (!allTasksShown) {
    fputs("...(more)...\n", out);
  }

  fflush(out);

  lastDebugMessageCount = DebugMessage::getMessageCount();
}

}  // namespace ekam
