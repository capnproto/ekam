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

#ifndef EKAM_CONSOLEDASHBOARD_H_
#define EKAM_CONSOLEDASHBOARD_H_

#include <vector>
#include <stdio.h>
#include "Dashboard.h"

namespace ekam {

class ConsoleDashboard : public Dashboard {
public:
  ConsoleDashboard(FILE* output);
  ~ConsoleDashboard();

  // implements Dashboard ----------------------------------------------------------------
  void beginTask(const std::string& verb, const std::string& noun, OwnedPtr<Task>* output);

private:
  class TaskImpl;

  int fd;
  FILE* out;

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

  static const Color SUCCESS_COLOR;
  static const Color PASSED_COLOR;
  static const Color FAILED_COLOR;
  static const Color RUNNING_COLOR;

  void clearRunning();
  void drawRunning();
};

}  // namespace ekam

#endif  // EKAM_CONSOLEDASHBOARD_H_
