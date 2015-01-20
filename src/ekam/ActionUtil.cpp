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

#include "ActionUtil.h"
#include <string.h>

namespace ekam {

Logger::Logger(BuildContext* context, OwnedPtr<ByteStream> stream)
    : context(context), stream(stream.release()) {}
Logger::~Logger() {}

Promise<void> Logger::run(EventManager* eventManager) {
  return eventManager->when(stream->readAsync(eventManager, buffer, sizeof(buffer)))(
    [=](size_t size) -> Promise<void> {
      if (size == 0) {
        return newFulfilledPromise();
      }
      context->log(std::string(buffer, size));
      return run(eventManager);
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
