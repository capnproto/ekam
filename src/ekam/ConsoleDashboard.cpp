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

#include "ConsoleDashboard.h"

#include <sys/ioctl.h>
#include <termios.h>
#include <string.h>
#include <ctype.h>

#include "base/Debug.h"

namespace ekam {

class ConsoleDashboard::LogFormatter {
public:
  LogFormatter(const std::string& text)
      : text(text.data()), end(text.data() + text.size()), wrapped(false) {
    eatWhitespace();
  }

  inline bool atEnd() {
    return text == end;
  }

  std::string getLine(int startColumn, int windowWidth) {
    std::string result;
    int column = startColumn;
    result.reserve((windowWidth - column) * 2);

    if (wrapped) {
      result.append("  ");
      column += 2;
      wrapped = false;
    }

    while (text < end && *text != '\n' && column < windowWidth) {
      if (isalnum(*text)) {
        const char* wordEnd = text;
        do {
          ++wordEnd;
        } while (wordEnd < end && isalnum(*wordEnd));

        int length = wordEnd - text;

        if (column + length <= windowWidth) {
          // Word fits on this line.
          bool isColored = false;
          if (strncasecmp(text, "error", length) == 0 ||
              strncasecmp(text, "fail", length) == 0 ||
              strncasecmp(text, "failed", length) == 0 ||
              strncasecmp(text, "fatal", length) == 0) {
            result.append(ANSI_COLOR_CODES[RED]);
            isColored = true;
          } else if (strncasecmp(text, "warning", length) == 0) {
            result.append(ANSI_COLOR_CODES[YELLOW]);
            isColored = true;
          }

          result.append(text, wordEnd);
          text = wordEnd;
          column += length;

          if (isColored) {
            result.append(ANSI_CLEAR_COLOR);
          }
        } else if (length >= 20 || column == startColumn) {
          // Really long word.  Break it.
          int spaceAvailable = windowWidth - column;
          result.append(text, spaceAvailable);
          text += spaceAvailable;
          column = windowWidth;
        } else {
          // Word doesn't fit in remaining space.  End the line.
          break;
        }
      } else {
        switch (*text) {
          case '\t':
            column = (column & ~0x7) + 8;
            result.push_back(*text);
            break;
          case '\033':  // escape
            // ignore -- could be harmful
            // TODO:  Parse and remove the subsequent terminal instruction to make the output not
            //   suck.  Or better yet, parse the sequence and interpret it.  If it is an SGR code
            //   (e.g. color), pass it through, and just make sure to reset later.
            break;
          default:
            if (*text >= '\0' && *text < ' ') {
              // Ignore control character or weird whitespace.
            } else {
              result.push_back(*text);
              ++column;
            }
            break;
        }
        ++text;
      }
    }

    wrapped = !eatWhitespace();

    return result;
  }

private:
  const char* text;
  const char* end;
  bool wrapped;

  // Eat whitespace up to and including a newline.  Returns true if a newline was eaten.
  bool eatWhitespace() {
    while (text < end && isspace(*text) && *text != '\n') {
      ++text;
    }
    if (text < end && *text == '\n') {
      ++text;
      return true;
    } else {
      return false;
    }
  }

  bool tryHighlight(const char* word, Color color, int windowWidth,
                    int* column, std::string* output) {
    int length = strlen(word);

    if (windowWidth - *column < length) {
      // Don't highlight words broken at line wrapping.
      return false;
    }

    if (end - text < length) {
      // Not enough characters left to match word.
      return false;
    }

    if (strncasecmp(text, word, length) != 0) {
      // Does not match.
      return false;
    }

    if (end - text > length && isalpha(text[length])) {
      // Character after word is a letter.  Don't highlight.
      return false;
    }

    if (!output->empty() && isalpha((*output)[output->size() - 1])) {
      // Character before word was a letter.  Don't highlight.
      return false;
    }

    output->append(ANSI_COLOR_CODES[color]);
    output->append(text, text + length);
    output->append(ANSI_CLEAR_COLOR);
    text += length;
    *column += length;
    return true;
  }
};

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
      LogFormatter formatter(outputText);
      struct winsize windowSize;
      ioctl(dashboard->fd, TIOCGWINSZ, &windowSize);

      for (int i = 0; i < 30 && !formatter.atEnd(); i++) {
        std::string line = formatter.getLine(2, windowSize.ws_col);
        fprintf(dashboard->out, "  %s\n", line.c_str());
      }

      if (!formatter.atEnd()) {
        fprintf(dashboard->out, "  ...(log truncated)...\n");
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

OwnedPtr<Dashboard::Task> ConsoleDashboard::beginTask(
    const std::string& verb, const std::string& noun, Silence silence) {
  return newOwned<TaskImpl>(this, verb, noun, silence);
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
