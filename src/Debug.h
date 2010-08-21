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

#ifndef EKAM_DEBUGLOG_H_
#define EKAM_DEBUGLOG_H_

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

#endif  // EKAM_DEBUGLOG_H_
