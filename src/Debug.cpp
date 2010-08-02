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

#include "Debug.h"

#include <stdio.h>

namespace kake2 {

DebugMessage::Severity DebugMessage::logLevel = WARNING;

static const char* SEVERITY_NAMES[] = {
  "INFO", "WARNING", "ERROR"
};

DebugMessage::DebugMessage(Severity severity, const char* filename, int line) {
  *this << "kake2 debug: " << SEVERITY_NAMES[severity] << ": "
        << filename << ":" << line << ": ";
}
DebugMessage::~DebugMessage() {
  // TODO:  We really need to buffer the message and write it all at once to avoid interleaved
  //   text when multiprocessing.
  fputs("\n", stderr);
  fflush(stderr);
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

}  // namespace kake2
