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

#ifndef EKAM_HASH_H_
#define EKAM_HASH_H_

#include <inttypes.h>
#include <string.h>
#include <string>

#include "sha256.h"

namespace ekam {

class Hash {
public:
  inline Hash() {}

  class Builder {
  public:
    Builder();
    Builder& add(const std::string& data);
    Builder& add(void* data, size_t size);
    Hash build();

  private:
    SHA256Context context;
  };

  static Hash of(const std::string& data);
  static Hash of(void* data, size_t size);
  static const Hash NULL_HASH;

  std::string toString() const;

  inline bool operator==(const Hash& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) == 0;
  }
  inline bool operator!=(const Hash& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) != 0;
  }
  inline bool operator<(const Hash& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) < 0;
  }
  inline bool operator>(const Hash& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) > 0;
  }
  inline bool operator<=(const Hash& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) <= 0;
  }
  inline bool operator>=(const Hash& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) >= 0;
  }

  class StlHashFunc {
  public:
    inline size_t operator()(const Hash& h) const {
      return h.shortHash;
    }
  };

private:
  union {
    unsigned char hash[32];
    size_t shortHash;
  };
};

}  // namespace ekam

#endif  // EKAM_HASH_H_
