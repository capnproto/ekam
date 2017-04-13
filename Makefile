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

.SUFFIXES:
.PHONY: all install clean deps

# You may override the following vars on the command line to suit
# your config.
CXX=g++
CXXFLAGS=-O2 -Wall
PARALLEL=$(shell nproc)

define color
  @printf '\033[0;34m==== $1 ====\033[0m\n'
endef

all: bin/ekam-bootstrap deps
	$(call color,building ekam with ekam)
	@rm -f bin/ekam
	@CXX="$(CXX)" CXXFLAGS="-std=c++14 $(CXXFLAGS) -pthread" LIBS="-pthread" bin/ekam-bootstrap -j$(PARALLEL)
	@test -e bin/ekam && printf "=====================================================\nSUCCESS\nOutput is at bin/ekam\n=====================================================\n"

deps: deps/capnproto

deps/capnproto:
	$(call color,downloading capnproto)
	@mkdir -p deps
	git clone https://github.com/sandstorm-io/capnproto.git deps/capnproto

SOURCES=$(shell find src/base src/os src/ekam -name '*.cpp' | \
    grep -v KqueueEventManager | grep -v PollEventManager | \
    grep -v ProtoDashboard | grep -v ekam-client | grep -v _test)

bin/ekam-bootstrap: $(SOURCES)
	$(call color,compiling bootstrap ekam)
	@mkdir -p bin
	$(CXX) -Isrc -std=c++11 $(SOURCES) -pthread -o $@

clean:
	rm -rf bin lib tmp

