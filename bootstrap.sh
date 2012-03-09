#! /bin/sh

# Kenton's Code Playground -- http://code.google.com/p/kentons-code
# Author: Kenton Varda (temporal@gmail.com)
# Copyright (c) 2010 Google, Inc. and contributors.
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

echo "This script builds a basic Ekam binary using a single massive compiler"
echo "invocation, then rebuilds Ekam using Ekam itself."

SOURCES=$(find src/base src/os src/ekam -name '*.cpp' |
    grep -v KqueueEventManager | grep -v PollEventManager |
    grep -v ProtoDashboard | grep -v ekam-client | grep -v _test)

set -e

echo "*************************************************"
echo "Building using one massive compile..."
echo "*************************************************"

echo \$ g++ -Isrc -std=gnu++0x $SOURCES -o bootstrap-ekam
g++ -Isrc -std=gnu++0x $SOURCES -o bootstrap-ekam

echo "*************************************************"
echo "Building again using Ekam..."
echo "*************************************************"

if test -e bin/ekam; then
  rm -f bin/ekam
fi

echo \$ CXXFLAGS=-std=gnu++0x ./bootstrap-ekam -j4
CXXFLAGS=-std=gnu++0x ./bootstrap-ekam -j4

echo "*************************************************"
if test -e bin/ekam; then
  echo "SUCCESS: output is at: bin/ekam"
  echo "IGNORE ALL ERRORS ABOVE.  Some errors are expected depending"
  echo "on your OS and whether or not you have the protobuf source code"
  echo "in your source tree."
	rm bootstrap-ekam
else
  echo "FAILED"
  exit 1
fi
echo "*************************************************"
