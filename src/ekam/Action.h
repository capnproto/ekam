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

#ifndef KENTONSCODE_EKAM_ACTION_H_
#define KENTONSCODE_EKAM_ACTION_H_

#include <string>
#include <vector>
#include <sys/types.h>

#include "base/OwnedPtr.h"
#include "os/File.h"
#include "Tag.h"
#include "os/EventManager.h"

namespace ekam {

class ActionFactory;

class ProcessExitCallback {
public:
  virtual ~ProcessExitCallback();

  // Negative = signal number.
  virtual void done(int exit_status) = 0;
};

class BuildContext {
public:
  virtual ~BuildContext() noexcept(false);

  virtual File* findProvider(Tag id) = 0;
  virtual File* findInput(const std::string& path) = 0;

  enum InstallLocation {
    BIN,
    LIB
  };
  static const int INSTALL_LOCATION_COUNT = 2;

  static const char* const INSTALL_LOCATION_NAMES[INSTALL_LOCATION_COUNT];

  virtual void provide(File* file, const std::vector<Tag>& tags) = 0;
  virtual void install(File* file, InstallLocation location, const std::string& name) = 0;
  virtual void log(const std::string& text) = 0;

  virtual OwnedPtr<File> newOutput(const std::string& path) = 0;

  virtual void addActionType(OwnedPtr<ActionFactory> factory) = 0;

  virtual void passed() = 0;
  virtual void failed() = 0;
};

class Action {
public:
  virtual ~Action();

  virtual bool isSilent() { return false; }
  virtual std::string getVerb() = 0;
  virtual Promise<void> start(EventManager* eventManager, BuildContext* context) = 0;
};

class ActionFactory {
public:
  virtual ~ActionFactory();

  virtual void enumerateTriggerTags(std::back_insert_iterator<std::vector<Tag> > iter) = 0;
  virtual OwnedPtr<Action> tryMakeAction(const Tag& id, File* file) = 0;
};

}  // namespace ekam

#endif  // KENTONSCODE_EKAM_ACTION_H_
