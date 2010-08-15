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

#ifndef EKAM_ENTITY_H_
#define EKAM_ENTITY_H_

#include <inttypes.h>
#include <string.h>
#include <string>
#include <vector>

namespace ekam {

class EntityId {
public:
  EntityId() {}

  static EntityId fromName(const std::string& name);
  static EntityId fromBytes(const std::string& data);

  bool operator==(const EntityId& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) == 0;
  }
  bool operator!=(const EntityId& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) != 0;
  }
  bool operator<(const EntityId& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) < 0;
  }
  bool operator>(const EntityId& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) > 0;
  }
  bool operator<=(const EntityId& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) <= 0;
  }
  bool operator>=(const EntityId& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) >= 0;
  }

  class HashFunc {
  public:
    size_t operator()(const EntityId& id) const {
      return id.shortHash;
    }
  };

private:
  union {
    unsigned char hash[32];
    size_t shortHash;
  };

  friend std::string toString(const EntityId& id);
};

std::string toString(const EntityId& id);

}  // namespace ekam

#endif  // EKAM_ENTITY_H_
