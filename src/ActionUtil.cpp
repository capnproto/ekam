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

Promise<void> Logger::readAll(EventManager* eventManager, ByteStream* stream) {
  return eventManager->when(stream->readAsync(eventManager, buffer, sizeof(buffer)))(
    [=](size_t size) -> Promise<void> {
      if (size == 0) {
        return newFulfilledPromise();
      }
      context->log(std::string(buffer, size));
      return readAll(eventManager, stream);
    }, [=](MaybeException<size_t> error) {
      try {
        error.get();
      } catch (const std::exception& e) {
        context->log(e.what());
        context->failed();
        throw;
      } catch (...) {
        context->log("unknown exception");
        context->failed();
        throw;
      }
    });
}

// =======================================================================================

LineReader::LineReader(ByteStream* stream) : stream(stream) {}
LineReader::~LineReader() {}

Promise<OwnedPtr<std::string>> LineReader::readLine(EventManager* eventManager) {
  std::string::size_type endpos = leftover.find_first_of('\n');
  if (endpos != std::string::npos) {
    auto result = newOwned<std::string>(leftover, 0, endpos);
    leftover.erase(0, endpos + 1);
    return newFulfilledPromise(result.release());
  }

  return eventManager->when(stream->readAsync(eventManager, buffer, sizeof(buffer)))(
    [=](size_t size) -> Promise<OwnedPtr<std::string>> {
      if (size == 0) {
        if (leftover.empty()) {
          // No more data.
          return newFulfilledPromise(OwnedPtr<std::string>(nullptr));
        } else {
          // Still have a line of text that had no trailing newline.
          auto line = newOwned<std::string>(std::move(leftover));
          leftover.clear();
          return newFulfilledPromise(line.release());
        }
      }

      leftover.append(buffer, size);
      return readLine(eventManager);
    });
}

}  // namespace ekam
