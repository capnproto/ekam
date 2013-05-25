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

#ifndef KENTONSCODE_EKAM_TAG_H_
#define KENTONSCODE_EKAM_TAG_H_

#include <inttypes.h>
#include <string.h>
#include <string>
#include <vector>

#include "base/Hash.h"

namespace ekam {

class File;

class Tag {
public:
  Tag() {}

  // Every file has this tag.
  static const Tag DEFAULT_TAG;

  static inline Tag fromName(const std::string& name) {
    return Tag(name);
  }

  static Tag fromFile(const std::string& path);

  inline std::string toString() { return hash.toString(); }

  inline bool operator==(const Tag& other) const { return hash == other.hash; }
  inline bool operator!=(const Tag& other) const { return hash != other.hash; }
  inline bool operator< (const Tag& other) const { return hash <  other.hash; }
  inline bool operator> (const Tag& other) const { return hash >  other.hash; }
  inline bool operator<=(const Tag& other) const { return hash <= other.hash; }
  inline bool operator>=(const Tag& other) const { return hash >= other.hash; }

  class HashFunc {
  public:
    inline size_t operator()(const Tag& id) const {
      return inner(id.hash);
    }

  private:
    Hash::StlHashFunc inner;
  };

private:
  Hash hash;

#ifdef EXTRA_DEBUG
  std::string name;
  inline explicit Tag(const std::string& name) : hash(Hash::of(name)), name(name) {}
#else
  inline explicit Tag(const std::string& name) : hash(Hash::of(name)) {}
#endif
};

}  // namespace ekam

#endif  // KENTONSCODE_EKAM_TAG_H_
