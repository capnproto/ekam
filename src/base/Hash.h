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
