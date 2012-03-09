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

#ifndef EKAM_BASE_DEBUGLOG_H_
#define EKAM_BASE_DEBUGLOG_H_

#include <string>

namespace ekam {

class DebugMessage {
public:
  enum Severity {
    INFO,
    WARNING,
    ERROR
  };

  DebugMessage(Severity severity, const char* filename, int line);
  ~DebugMessage();

  DebugMessage& operator<<(const char* value);
  DebugMessage& operator<<(const std::string& value);
  DebugMessage& operator<<(char value);
  DebugMessage& operator<<(signed char value);
  DebugMessage& operator<<(unsigned char value);
  DebugMessage& operator<<(short value);
  DebugMessage& operator<<(unsigned short value);
  DebugMessage& operator<<(int value);
  DebugMessage& operator<<(unsigned int value);
  DebugMessage& operator<<(long value);
  DebugMessage& operator<<(unsigned long value);
  DebugMessage& operator<<(long long value);
  DebugMessage& operator<<(unsigned long long value);
  DebugMessage& operator<<(float value);
  DebugMessage& operator<<(double value);
  DebugMessage& operator<<(const void* ptr);

  inline static bool shouldLog(Severity severity, const char*, int) {
    return severity >= logLevel;
  }

  inline static void setLogLevel(Severity severity) {
    logLevel = severity;
  }

  // Useful for detecting if any log messages have been printed, e.g. to avoid clobbering them
  // with terminal manipulations.
  inline static int getMessageCount() { return counter; }

private:
  static Severity logLevel;
  static int counter;
};

#define DEBUG_LOG(SEVERITY) \
  if (!DebugMessage::shouldLog(::ekam::DebugMessage::SEVERITY, __FILE__, __LINE__)) {} else \
  ::ekam::DebugMessage(::ekam::DebugMessage::SEVERITY, __FILE__, __LINE__)

#define DEBUG_INFO DEBUG_LOG(INFO)
#define DEBUG_WARNING DEBUG_LOG(WARNING)
#define DEBUG_ERROR DEBUG_LOG(ERROR)

}  // namespace ekam

#endif  // EKAM_BASE_DEBUGLOG_H_
