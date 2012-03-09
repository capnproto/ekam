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

#include "Hash.h"

#include <stdexcept>

#include "sha256.h"

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

Hash Hash::of(const std::string& data) {
  return Builder().add(data).build();
}

Hash Hash::of(void* data, size_t size) {
  return Builder().add(data, size).build();
}

// Note:  Since this is in static space it will be automatically initialized to zero.
const Hash Hash::NULL_HASH;

std::string Hash::toString() const {
  std::string result;
  result.reserve(sizeof(hash) * 2);
  for (unsigned int i = 0; i < sizeof(hash); i++) {
    result.push_back(HexDigit(hash[i] >> 4));
    result.push_back(HexDigit(hash[i]));
  }
  return result;
}

Hash::Builder::Builder() {
  SHA256_Init(&context);
}

Hash::Builder& Hash::Builder::add(const std::string& data) {
  SHA256_Update(&context, data.data(), data.size());
  return *this;
}

Hash::Builder& Hash::Builder::add(void* data, size_t size) {
  SHA256_Update(&context, data, size);
  return *this;
}

Hash Hash::Builder::build() {
  Hash result;
  SHA256_Final(result.hash, &context);
  return result;
}

}  // namespace ekam
