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

#ifndef KENTONSCODE_EKAM_DRIVER_H_
#define KENTONSCODE_EKAM_DRIVER_H_

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <set>

#include "base/OwnedPtr.h"
#include "os/File.h"
#include "Action.h"
#include "Tag.h"
#include "Dashboard.h"
#include "base/Table.h"

namespace ekam {

class Driver {
public:
  Driver(EventManager* eventManager, Dashboard* dashboard, File* tmp,
         File* installDirs[BuildContext::INSTALL_LOCATION_COUNT], int maxConcurrentActions);
  ~Driver();

  void addActionFactory(ActionFactory* factory);

  void addSourceFile(File* file);
  void removeSourceFile(File* file);

private:
  class ActionDriver;

  EventManager* eventManager;
  Dashboard* dashboard;

  File* tmp;
  File* installDirs[BuildContext::INSTALL_LOCATION_COUNT];

  int maxConcurrentActions;

  class TriggerTable : public Table<IndexedColumn<Tag, Tag::HashFunc>,
                                    IndexedColumn<ActionFactory*> > {
  public:
    static const int TAG = 0;
    static const int FACTORY = 1;
  };
  TriggerTable triggers;

  struct Provision {
    ActionDriver* creator;  // possibly null
    OwnedPtr<File> file;
    Hash contentHash;
  };

  class TagTable : public Table<IndexedColumn<Tag, Tag::HashFunc>, IndexedColumn<Provision*> > {
  public:
    static const int TAG = 0;
    static const int PROVISION = 1;
  };
  TagTable tagTable;

  OwnedPtrVector<ActionDriver> activeActions;
  OwnedPtrDeque<ActionDriver> pendingActions;
  OwnedPtrMap<ActionDriver*, ActionDriver> completedActionPtrs;

  class DependencyTable : public Table<IndexedColumn<Tag, Tag::HashFunc>,
                                       IndexedColumn<ActionDriver*>,
                                       IndexedColumn<Provision*> > {
  public:
    static const int TAG = 0;
    static const int ACTION = 1;
    static const int PROVISION = 2;
  };
  DependencyTable dependencyTable;

  class ActionTriggersTable : public Table<IndexedColumn<ActionFactory*>,
                                           IndexedColumn<Provision*>,
                                           IndexedColumn<ActionDriver*> > {
  public:
    static const int FACTORY = 0;
    static const int PROVISION = 1;
    static const int ACTION = 2;
  };
  ActionTriggersTable actionTriggersTable;

  OwnedPtrMap<File*, Provision, File::HashFunc, File::EqualFunc> rootProvisions;

  void startSomeActions();

  void rescanForNewFactory(ActionFactory* factory);

  void queueNewAction(ActionFactory* factory, OwnedPtr<Action> action,
                      Provision* provision);

  void getTransitiveDependencies(ActionDriver* action, std::unordered_set<ActionDriver*>* deps);

  void registerProvider(Provision* provision, const std::vector<Tag>& tags,
                        const std::unordered_set<ActionDriver*>& dependencies);
  void resetDependentActions(const Tag& tag,
                             const std::unordered_set<ActionDriver*>& dependencies);
  void resetDependentActions(Provision* provision);
  void fireTriggers(const Tag& tag, Provision* provision);

  void dumpErrors();
};

}  // namespace ekam

#endif  // KENTONSCODE_EKAM_DRIVER_H_
