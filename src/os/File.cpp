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

#include "File.h"
#include <string>

namespace ekam {

File::~File() {}
File::DiskRef::~DiskRef() {};

void splitExtension(const std::string& name, std::string* base, std::string* ext) {
  std::string::size_type pos = name.find_last_of('.');
  std::string::size_type slashpos = name.find_last_of('/');
  if (pos == std::string::npos || (slashpos != std::string::npos && pos < slashpos)) {
    base->assign(name);
    ext->clear();
  } else {
    base->assign(name, 0, pos);
    ext->assign(name, pos, std::string::npos);
  }
}

void recursivelyCreateDirectory(File* location) {
  if (!location->isDirectory()) {
    recursivelyCreateDirectory(location->parent().get());
    location->createDirectory();
  }
}

}  // namespace ekam
