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

#include "ActionUtil.h"
#include <string.h>

namespace ekam {

Logger::Logger(BuildContext* context)
    : context(context) {}
Logger::~Logger() {}

void Logger::consume(const void* buffer, size_t size) {
  context->log(std::string(reinterpret_cast<const char*>(buffer), size));
}

void Logger::eof() {}

void Logger::error(int number) {
  context->log("read(log pipe): " + std::string(strerror(number)));
  context->failed();
}

// =======================================================================================

LineReader::Callback::~Callback() {}

LineReader::LineReader(Callback* callback) : callback(callback) {}
LineReader::~LineReader() {}

// implements ReadAllCallback ------------------------------------------------------------

void LineReader::consume(const void* buffer, size_t size) {
  const char* data = reinterpret_cast<const char*>(buffer);
  std::string line = leftover;

  while (true) {
    const char* eol = reinterpret_cast<const char*>(memchr(data, '\n', size));
    if (eol == NULL) {
      leftover.assign(data, size);
      break;
    }
    line.append(data, eol);
    callback->consume(line);
    line.clear();
    size -= eol + 1 - data;
    data = eol + 1;
  }
}

void LineReader::eof() {
  if (!leftover.empty()) {
    callback->consume(leftover);
    leftover.clear();
  }
  callback->eof();
}

void LineReader::error(int number) {
  callback->error(number);
}

}  // namespace ekam
