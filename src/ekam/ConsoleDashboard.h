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

#ifndef KENTONSCODE_EKAM_CONSOLEDASHBOARD_H_
#define KENTONSCODE_EKAM_CONSOLEDASHBOARD_H_

#include <vector>
#include <stdio.h>
#include "Dashboard.h"

namespace ekam {

class ConsoleDashboard : public Dashboard {
public:
  ConsoleDashboard(FILE* output, int maxDisplayedLogLines, bool onlyPrintFailures = false);
  ~ConsoleDashboard();

  // implements Dashboard ----------------------------------------------------------------
  OwnedPtr<Task> beginTask(const std::string& verb, const std::string& noun, Silence silence);

private:
  class TaskImpl;
  class LogFormatter;

  int fd;
  FILE* out;
  int maxDisplayedLogLines;
  bool onlyPrintFailures;

  std::vector<TaskImpl*> runningTasks;
  int runningTasksLineCount;
  int lastDebugMessageCount;

  enum Color {
    BLACK,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    FUCHSIA,
    CYAN,
    WHITE,
    GRAY,
    BRIGHT_RED,
    BRIGHT_GREEN,
    BRIGHT_YELLOW,
    BRIGHT_BLUE,
    BRIGHT_FUCHSIA,
    BRIGHT_CYAN,
    BRIGHT_WHITE,
  };

  static const char* const ANSI_COLOR_CODES[];
  static const char* const ANSI_CLEAR_COLOR;
  static const char* const ANSI_MOVE_CURSOR_UP;
  static const char* const ANSI_CLEAR_BELOW_CURSOR;

  static const Color DONE_COLOR;
  static const Color PASSED_COLOR;
  static const Color FAILED_COLOR;
  static const Color RUNNING_COLOR;
  static const Color BLOCKED_COLOR;

  void clearRunning();
  void drawRunning();
};

}  // namespace ekam

#endif  // KENTONSCODE_EKAM_CONSOLEDASHBOARD_H_
