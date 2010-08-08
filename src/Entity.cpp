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


#include "Entity.h"

#include <sha256.h>
#include <stdexcept>

namespace ekam {

namespace {

char HexDigit(unsigned int value) {
  value &= 0x0F;
  if (value < 10) {
    return '0' + value;
  } else {
    return 'a' + value - 10;
  }
}

} // anonymous namespace

EntityId EntityId::fromName(const std::string& name) {
  EntityId result;
  SHA256_CTX context;
  SHA256_Init(&context);
  SHA256_Update(&context, name.data(), name.size());
  SHA256_Final(result.hash, &context);
  return result;
}

EntityId EntityId::fromBytes(const std::string& data) {
  EntityId result;
  if (data.size() != sizeof(result.hash)) {
    throw std::invalid_argument("Hash passed to EntityId::fromBytes() had incorrect size.");
  }

  memcpy(result.hash, data.data(), sizeof(result.hash));
  return result;
}

std::string toString(const EntityId& id) {
  std::string result;
  result.reserve(sizeof(id.hash) * 2);
  for (unsigned int i = 0; i < sizeof(id.hash); i++) {
    result.push_back(HexDigit(id.hash[i] >> 4));
    result.push_back(HexDigit(id.hash[i]));
  }
  return result;
}

}  // namespace ekam
