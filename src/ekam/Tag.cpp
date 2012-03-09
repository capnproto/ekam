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

#include "Tag.h"
#include "base/Debug.h"

namespace ekam {

namespace {

std::string canonicalizePath(const std::string& path) {
  std::vector<std::string> parts;

  std::string::size_type pos = 0;
  while (pos != std::string::npos) {
    std::string::size_type slashPos = path.find_first_of('/', pos);

    std::string part;
    if (slashPos == std::string::npos) {
      part.assign(path, pos, path.size() - pos);
      pos = slashPos;
    } else {
      part.assign(path, pos, slashPos - pos);
      pos = path.find_first_not_of('/', slashPos);
    }

    if (part.empty() || part == ".") {
      // skip
    } else if (part == "..") {
      if (parts.empty()) {
        // ignore
      } else {
        parts.pop_back();
      }
    } else {
      parts.push_back(part);
    }
  }

  std::string result;
  result.reserve(path.size());
  for (unsigned int i = 0; i < parts.size(); i++) {
    if (i > 0) {
      result.push_back('/');
    }
    result.append(parts[i]);
  }

  return result;
}

}  // namespace

const Tag Tag::DEFAULT_TAG = Tag::fromName("file:*");

Tag Tag::fromFile(const std::string& path) {
  return fromName("file:" + canonicalizePath(path));
}

}  // namespace ekam
