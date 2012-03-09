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

#include "Debug.h"

#include <stdio.h>

namespace ekam {

DebugMessage::Severity DebugMessage::logLevel = WARNING;
int DebugMessage::counter = 0;

static const char* SEVERITY_NAMES[] = {
  "INFO", "WARNING", "ERROR"
};

DebugMessage::DebugMessage(Severity severity, const char* filename, int line) {
  *this << "ekam debug: " << SEVERITY_NAMES[severity] << ": "
        << filename << ":" << line << ": ";
}
DebugMessage::~DebugMessage() {
  // TODO:  We really need to buffer the message and write it all at once to avoid interleaved
  //   text when multiprocessing.
  fputs("\n", stderr);
  fflush(stderr);
  ++counter;
}

DebugMessage& DebugMessage::operator<<(const char* value) {
  fputs(value, stderr);
  return *this;
}

DebugMessage& DebugMessage::operator<<(const std::string& value) {
  fwrite(value.data(), sizeof(char), value.size(), stderr);
  return *this;
}

#define HANDLE_TYPE(TYPE, FORMAT)                      \
DebugMessage& DebugMessage::operator<<(TYPE value) {   \
  fprintf(stderr, FORMAT, value);                      \
  return *this;                                        \
}

HANDLE_TYPE(char, "%c");
HANDLE_TYPE(signed char, "%hhd");
HANDLE_TYPE(unsigned char, "%hhu");
HANDLE_TYPE(short, "%hd");
HANDLE_TYPE(unsigned short, "%hu");
HANDLE_TYPE(int, "%d");
HANDLE_TYPE(unsigned int, "%u");
HANDLE_TYPE(long, "%ld");
HANDLE_TYPE(unsigned long, "%lu");
HANDLE_TYPE(long long, "%lld");
HANDLE_TYPE(unsigned long long, "%llu");
HANDLE_TYPE(float, "%g");
HANDLE_TYPE(double, "%g");
HANDLE_TYPE(const void*, "%p");

}  // namespace ekam
