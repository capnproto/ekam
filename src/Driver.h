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

#ifndef EKAM_DRIVER_H_
#define EKAM_DRIVER_H_

#include <tr1/unordered_map>
#include <tr1/memory>
#include <list>

#include "OwnedPtr.h"
#include "File.h"
#include "Action.h"
#include "Entity.h"
#include "Dashboard.h"

namespace ekam {

class Driver {
public:
  Driver(EventManager* eventManager, Dashboard* dashboard, File* src, File* tmp,
         int maxConcurrentActions);
  ~Driver();

  void addActionFactory(ActionFactory* factory);

  void start();

private:
  class ActionDriver;

  EventManager* eventManager;
  Dashboard* dashboard;

  File* src;
  File* tmp;

  int maxConcurrentActions;

  std::vector<ActionFactory*> actionFactories;
  OwnedPtrVector<ActionFactory> ownedFactories;

  typedef std::tr1::unordered_multimap<EntityId, ActionFactory*, EntityId::HashFunc> TriggerMap;
  TriggerMap triggers;

  struct EntityInfo {
    File* provider;
    Hash contentHash;  // = provider->contentHash(), so that we don't have to recompute it.
  };
  typedef std::tr1::unordered_map<EntityId, EntityInfo, EntityId::HashFunc> EntityMap;
  EntityMap entityMap;
  OwnedPtrMap<File*, File> filePtrs;

  OwnedPtrVector<ActionDriver> activeActions;
  OwnedPtrVector<ActionDriver> pendingActions;

  typedef std::tr1::unordered_multimap<EntityId, ActionDriver*, EntityId::HashFunc>
      CompletedActionMap;
  CompletedActionMap completedActions;
  OwnedPtrMap<ActionDriver*, ActionDriver> completedActionPtrs;

  typedef std::tr1::unordered_map<pid_t, ActionDriver*> ProcessMap;
  ProcessMap processMap;

  struct SrcTmpPair {
    OwnedPtr<File> srcFile;
    Hash srcFileHash;
    OwnedPtr<File> tmpLocation;
  };
  OwnedPtrVector<SrcTmpPair> allScannedFiles;

  void startSomeActions();

  enum ScanType {
    ORIGINAL_INPUT,
    DERIVED_INPUT
  };
  void scanForActions(File* src, File* tmp, ScanType type);
  void rescanForNewFactory(ActionFactory* factory);

  void queueNewAction(OwnedPtr<Action>* actionToAdopt, File* file,
                      const Hash& fileHash, File* tmpLocation);

  void registerProvider(OwnedPtr<File>* fileToAdopt, const std::vector<EntityId>& entities);
  void resetDependentActions(const EntityId& entity);
  void fireTriggers(const EntityId& entity, File* file, const Hash& fileHash);
};

}  // namespace ekam

#endif  // EKAM_DRIVER_H_
