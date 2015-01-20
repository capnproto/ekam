# Ekam Build System
# Author: Kenton Varda (kenton@sandstorm.io)
# Copyright (c) 2010-2015 Kenton Varda, Google Inc., and contributors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

@0xa610fcb94ceea9cc;

$import "/capnp/c++.capnp".namespace("ekam::proto");

struct Header {
  # The first message sent over the stream is the header.

  projectRoot @0 :Text;
  # The directory where ekam was run, containing "src", "tmp", etc.
}

struct TaskUpdate {
  # All subsequent messages are TaskUpdates.

  id @0 :UInt32;

  state @1 :State = unchanged;
  enum State {
    unchanged @0;
    deleted @1;
    pending @2;
    running @3;
    done @4;
    passed @5;
    failed @6;
    blocked @7;
  }

  verb @2 :Text;
  noun @3 :Text;
  silent @4 :Bool;
  log @5 :Text;
}
