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

#include "Tag.h"
#include "Debug.h"

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
