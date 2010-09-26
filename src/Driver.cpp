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

#include "Driver.h"

#include <queue>
#include <tr1/memory>
#include <stdexcept>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "Debug.h"
#include "EventGroup.h"

namespace ekam {

namespace {

int fileDepth(const std::string& name) {
  int result = 0;
  for (unsigned int i = 0; i < name.size(); i++) {
    if (name[i] == '/') {
      ++result;
    }
  }
  return result;
}

int commonPrefixLength(const std::string& srcName, const std::string& bestMatchName) {
  std::string::size_type n = std::min(srcName.size(), bestMatchName.size());
  for (unsigned int i = 0; i < n; i++) {
    if (srcName[i] != bestMatchName[i]) {
      return i;
    }
  }
  return n;
}

}  // namespace

class Driver::ActionDriver : public BuildContext, public EventGroup::ExceptionHandler {
public:
  ActionDriver(Driver* driver, OwnedPtr<Action>* actionToAdopt,
               File* srcfile, Hash srcHash, OwnedPtr<Dashboard::Task>* taskToAdopt);
  ~ActionDriver();

  void start();

  // implements BuildContext -------------------------------------------------------------
  File* findProvider(Tag id);
  File* findInput(const std::string& basename);

  void provide(File* file, const std::vector<Tag>& tags);
  void log(const std::string& text);

  void newOutput(const std::string& basename, OwnedPtr<File>* output);

  void addActionType(OwnedPtr<ActionFactory>* factoryToAdopt);

  void passed();
  void failed();

  // implements ExceptionHandler ---------------------------------------------------------
  void threwException(const std::exception& e);
  void threwUnknownException();
  void noMoreEvents();

private:
  class StartCallback : public EventManager::Callback {
  public:
    StartCallback(ActionDriver* actionDriver) : actionDriver(actionDriver) {}
    ~StartCallback() {}

    // implements Callback ---------------------------------------------------------------
    void run() {
      actionDriver->asyncCallbackOp.clear();
      actionDriver->action->start(&actionDriver->eventGroup, actionDriver,
                                  &actionDriver->runningAction);
    }

  private:
    ActionDriver* actionDriver;
  };

  class DoneCallback : public EventManager::Callback {
  public:
    DoneCallback(ActionDriver* actionDriver) : actionDriver(actionDriver) {}
    ~DoneCallback() {}

    // implements Callback ---------------------------------------------------------------
    void run() {
      actionDriver->asyncCallbackOp.clear();
      Driver* driver = actionDriver->driver;
      actionDriver->returned();  // may delete actionDriver
      driver->startSomeActions();
    }

  private:
    ActionDriver* actionDriver;
  };


  Driver* driver;
  OwnedPtr<Action> action;
  OwnedPtr<File> srcfile;
  Hash srcHash;
  OwnedPtr<File> tmpdir;
  OwnedPtr<Dashboard::Task> dashboardTask;

  // TODO:  Get rid of "state".  Maybe replace with "status" or something, but don't try to
  //   track both whether we're running and what the status was at the same time.  (I already
  //   had to split isRunning into a separate boolean due to issues with this.)
  enum {
    PENDING,
    RUNNING,
    DONE,
    PASSED,
    FAILED
  } state;

  EventGroup eventGroup;

  StartCallback startCallback;
  DoneCallback doneCallback;
  OwnedPtr<AsyncOperation> asyncCallbackOp;

  bool isRunning;
  OwnedPtr<AsyncOperation> runningAction;

  typedef std::tr1::unordered_map<Tag, Hash, Tag::HashFunc> DependencySet;
  DependencySet dependencies;

  OwnedPtrVector<File> outputs;

  OwnedPtrVector<Provision> provisions;

  void ensureRunning();
  void queueDoneCallback();
  void returned();
  void reset(std::tr1::unordered_set<Tag, Tag::HashFunc>* tagsToReset,
             std::vector<ActionDriver*>* actionsToDelete);
  Provision* choosePreferredProvider(const Tag& tag);
  bool hasPreferredDependencyChanged(const Tag& tag);

  friend class Driver;
};

Driver::ActionDriver::ActionDriver(Driver* driver, OwnedPtr<Action>* actionToAdopt,
                                   File* srcfile, Hash srcHash,
                                   OwnedPtr<Dashboard::Task>* taskToAdopt)
    : driver(driver), srcHash(srcHash), state(PENDING),
      eventGroup(driver->eventManager, this), startCallback(this), doneCallback(this),
      isRunning(false) {
  action.adopt(actionToAdopt);
  srcfile->clone(&this->srcfile);

  OwnedPtr<File> tmpLocation;
  driver->tmp->relative(srcfile->canonicalName(), &tmpLocation);
  tmpLocation->parent(&this->tmpdir);
  recursivelyCreateDirectory(tmpdir.get());

  dashboardTask.adopt(taskToAdopt);
}
Driver::ActionDriver::~ActionDriver() {}

void Driver::ActionDriver::start() {
  if (state != PENDING) {
    DEBUG_ERROR << "State must be PENDING here.";
  }
  assert(dependencies.empty());
  assert(outputs.empty());
  assert(provisions.empty());
  assert(!isRunning);

  state = RUNNING;
  isRunning = true;
  dashboardTask->setState(Dashboard::RUNNING);

  OwnedPtr<EventManager::Callback> callback;
  eventGroup.runAsynchronously(&startCallback, &asyncCallbackOp);
}

File* Driver::ActionDriver::findProvider(Tag id) {
  ensureRunning();

  Provision* provision = choosePreferredProvider(id);

  if (provision == NULL) {
    dependencies.insert(std::make_pair(id, Hash::NULL_HASH));
    return NULL;
  } else {
    dependencies.insert(std::make_pair(id, provision->contentHash));
    return provision->file.get();
  }
}

File* Driver::ActionDriver::findInput(const std::string& basename) {
  ensureRunning();

  OwnedPtr<File> reference;
  tmpdir->relative(basename, &reference);
  return findProvider(Tag::fromFile(reference.get()));
}

void Driver::ActionDriver::provide(File* file, const std::vector<Tag>& tags) {
  ensureRunning();

  // Find existing provision for this file, if any.
  // TODO:  Convert provisions into a map?
  Provision* provision = NULL;
  for (int i = 0; i < provisions.size(); i++) {
    if (provisions.get(i)->file->equals(file)) {
      provision = provisions.get(i);
      break;
    }
  }

  if (provision == NULL) {
    OwnedPtr<Provision> ownedProvision;
    ownedProvision.allocate();
    provision = ownedProvision.get();
    provisions.adoptBack(&ownedProvision);
  }

  file->clone(&provision->file);
  provision->tags.insert(provision->tags.end(), tags.begin(), tags.end());
}

void Driver::ActionDriver::log(const std::string& text) {
  ensureRunning();
  dashboardTask->addOutput(text);
}

void Driver::ActionDriver::newOutput(const std::string& basename, OwnedPtr<File>* output) {
  ensureRunning();
  OwnedPtr<File> file;
  tmpdir->relative(basename, &file);
  file->clone(output);

  std::vector<Tag> tags;
  tags.push_back(Tag::DEFAULT_TAG);
  provide(file.get(), tags);

  outputs.adoptBack(&file);
}

void Driver::ActionDriver::addActionType(OwnedPtr<ActionFactory>* factoryToAdopt) {
  ensureRunning();
  driver->addActionFactory(factoryToAdopt->get());
  driver->rescanForNewFactory(factoryToAdopt->get());
  driver->ownedFactories.adoptBack(factoryToAdopt);
}

void Driver::ActionDriver::noMoreEvents() {
  ensureRunning();

  if (state == RUNNING) {
    state = DONE;
    queueDoneCallback();
  }
}

void Driver::ActionDriver::passed() {
  ensureRunning();

  if (state == FAILED) {
    // Ignore passed() after failed().
    return;
  }

  state = PASSED;
  queueDoneCallback();
}

void Driver::ActionDriver::failed() {
  ensureRunning();

  if (state == FAILED) {
    // Ignore redundant call to failed().
    return;
  } else if (state == DONE) {
    // (done callback should already be queued)
    throw std::runtime_error("Called failed() after success().");
  } else if (state == PASSED) {
    // (done callback should already be queued)
    throw std::runtime_error("Called failed() after passed().");
  } else {
    state = FAILED;
    queueDoneCallback();
  }
}

void Driver::ActionDriver::ensureRunning() {
  if (!isRunning) {
    throw std::runtime_error("Action is not running.");
  }
}

void Driver::ActionDriver::queueDoneCallback() {
  driver->eventManager->runAsynchronously(&doneCallback, &asyncCallbackOp);
}

void Driver::ActionDriver::threwException(const std::exception& e) {
  ensureRunning();
  dashboardTask->addOutput(std::string("uncaught exception: ") + e.what() + "\n");
  asyncCallbackOp.clear();
  state = FAILED;
  returned();
}

void Driver::ActionDriver::threwUnknownException() {
  ensureRunning();
  dashboardTask->addOutput("uncaught exception of unknown type\n");
  asyncCallbackOp.clear();
  state = FAILED;
  returned();
}

void Driver::ActionDriver::returned() {
  ensureRunning();

  // Cancel anything still running.
  runningAction.clear();
  isRunning = false;

  // Pull self out of driver->activeActions.
  OwnedPtr<ActionDriver> self;
  for (int i = 0; i < driver->activeActions.size(); i++) {
    if (driver->activeActions.get(i) == this) {
      driver->activeActions.releaseAndShift(i, &self);
      break;
    }
  }

  // Did the action use any tags that changed since the action started?
  for (DependencySet::const_iterator iter = dependencies.begin();
       iter != dependencies.end(); ++iter) {
    if (hasPreferredDependencyChanged(iter->first)) {
      // Yep.  Must do over.  Clear everything and add again to the action queue.
      state = PENDING;
      provisions.clear();
      outputs.clear();
      dependencies.clear();
      // Put on back of queue to avoid repeatedly retrying an action that has lots of
      // interference.
      driver->pendingActions.adoptBack(&self);
      dashboardTask->setState(Dashboard::BLOCKED);
      return;
    }
  }

  // Insert self into the completed actions map.
  driver->completedActionPtrs.adopt(this, &self);
  for (DependencySet::const_iterator iter = dependencies.begin();
       iter != dependencies.end(); ++iter) {
    driver->completedActions.insert(std::make_pair(iter->first, this));
  }

  if (state == FAILED) {
    // Failed, possibly due to missing dependencies.
    provisions.clear();
    outputs.clear();
    dashboardTask->setState(Dashboard::BLOCKED);
  } else {
    dashboardTask->setState(state == PASSED ? Dashboard::PASSED : Dashboard::DONE);

    // Remove outputs which were deleted before the action completed.  Some actions create
    // files and then delete them immediately.
    OwnedPtrVector<Provision> provisionsToFilter;
    provisions.swap(&provisionsToFilter);
    for (int i = 0; i < provisionsToFilter.size(); i++) {
      if (provisionsToFilter.get(i)->file->exists()) {
        OwnedPtr<Provision> temp;
        provisionsToFilter.release(i, &temp);
        provisions.adoptBack(&temp);
      }
    }

    // Register providers.
    for (int i = 0; i < provisions.size(); i++) {
      driver->registerProvider(provisions.get(i));
    }
  }
}

void Driver::ActionDriver::reset(
    std::tr1::unordered_set<Tag, Tag::HashFunc>* tagsToReset,
    std::vector<ActionDriver*>* actionsToDelete) {
  state = ActionDriver::PENDING;

  for (int i = 0; i < provisions.size(); i++) {
    Provision* provision = provisions.get(i);
    const std::vector<Tag>& subTags = provision->tags;

    // All provided tags must be reset as well.
    tagsToReset->insert(subTags.begin(), subTags.end());

    // Remove from tagMap.
    for (unsigned int j = 0; j < subTags.size(); j++) {
      const Tag& tag = subTags[j];

      ProvisionSet* provisions = driver->tagMap.get(tag);
      if (provisions == NULL || provisions->erase(provision) == 0) {
        DEBUG_ERROR << "Provision not in driver->tagMap?";
        continue;
      }

      if (provisions->empty()) {
        driver->tagMap.erase(tag);
      }
    }

    // Everything triggered by this provision must be deleted.
    std::pair<ActionsByTriggerMap::iterator, ActionsByTriggerMap::iterator> range =
        driver->actionsByTrigger.equal_range(provision);
    for (ActionsByTriggerMap::iterator iter = range.first; iter != range.second; ++iter) {
      actionsToDelete->push_back(iter->second);
    }
    driver->actionsByTrigger.erase(range.first, range.second);
  }

  // Remove all entries in CompletedActionMap pointing at this action.
  for (ActionDriver::DependencySet::const_iterator iter = dependencies.begin();
       iter != dependencies.end(); ++iter) {
    std::pair<CompletedActionMap::iterator, CompletedActionMap::iterator>
        range = driver->completedActions.equal_range(iter->first);
    for (CompletedActionMap::iterator iter2 = range.first;
         iter2 != range.second; ++iter2) {
      if (iter2->second == this) {
        driver->completedActions.erase(iter2);
        break;
      }
    }
  }

  dependencies.clear();
  provisions.clear();
  outputs.clear();
}

Driver::Provision* Driver::ActionDriver::choosePreferredProvider(const Tag& tag) {
  ProvisionSet* provisions = driver->tagMap.get(tag);
  if (provisions == NULL || provisions->empty()) {
    return NULL;
  } else {
    ProvisionSet::iterator iter = provisions->begin();
    std::string srcName = srcfile->canonicalName();
    Provision* bestMatch = *iter;
    ++iter;

    if (iter != provisions->end()) {
      // There are multiple files with this tag.  We must choose which one we like best.
      std::string bestMatchName = bestMatch->file->canonicalName();
      int bestMatchDepth = fileDepth(bestMatchName);
      int bestMatchCommonPrefix = commonPrefixLength(srcName, bestMatchName);

      for (; iter != provisions->end(); ++iter) {
        Provision* candidate = *iter;
        std::string candidateName = candidate->file->canonicalName();
        int candidateDepth = fileDepth(candidateName);
        int candidateCommonPrefix = commonPrefixLength(srcName, candidateName);
        if (candidateCommonPrefix < bestMatchCommonPrefix) {
          // Prefer provider that is closer in the directory tree.
          continue;
        } else if (candidateCommonPrefix == bestMatchCommonPrefix) {
          if (candidateDepth > bestMatchDepth) {
            // Prefer provider that is less deeply nested.
            continue;
          } else if (candidateDepth == bestMatchDepth) {
            // Arbitrarily -- but consistently -- choose one.
            int diff = bestMatchName.compare(candidateName);
            if (diff < 0) {
              // Prefer file that comes first alphabetically.
              continue;
            } else if (diff == 0) {
              // TODO:  Is this really an error?  I think it is for the moment, but someday it
              //   may not be, if multiple actions are allowed to produce outputs with the same
              //   canonical names.
              DEBUG_ERROR << "Two providers have same file name: " << bestMatchName;
              continue;
            }
          }
        }

        // If we get here, the candidate is better than the existing best match.
        bestMatch = candidate;
        bestMatchName.swap(candidateName);
        bestMatchDepth = candidateDepth;
        bestMatchCommonPrefix = candidateCommonPrefix;
      }
    }

    return bestMatch;
  }
}

bool Driver::ActionDriver::hasPreferredDependencyChanged(const Tag& tag) {
  // TODO:  Do we care if the file name changes, assuming the content hash is the same?  Here
  //   we only check for a hash change.
  Provision* preferred = choosePreferredProvider(tag);
  const Hash& currentHash = preferred == NULL ? Hash::NULL_HASH : preferred->contentHash;
  DependencySet::const_iterator actual = dependencies.find(tag);
  if (actual == dependencies.end()) {
    DEBUG_ERROR << "hasPreferredDependencyChanged() called on tag which we don't depend on.";
    return false;
  } else {
    return currentHash != actual->second;
  }
}

// =======================================================================================

Driver::Driver(EventManager* eventManager, Dashboard* dashboard, File* src, File* tmp,
               int maxConcurrentActions)
    : eventManager(eventManager), dashboard(dashboard), src(src), tmp(tmp),
      maxConcurrentActions(maxConcurrentActions) {
  if (!tmp->exists()) {
    tmp->createDirectory();
  }
}

Driver::~Driver() {
  // Error out all blocked tasks.
  for (OwnedPtrMap<ActionDriver*, ActionDriver>::Iterator iter(completedActionPtrs); iter.next();) {
    if (iter.key()->state == ActionDriver::FAILED) {
      iter.value()->dashboardTask->setState(Dashboard::FAILED);
    }
  }
}

void Driver::addActionFactory(ActionFactory* factory) {
  actionFactories.push_back(factory);

  std::vector<Tag> triggerTags;
  factory->enumerateTriggerTags(std::back_inserter(triggerTags));
  for (unsigned int i = 0; i < triggerTags.size(); i++) {
    triggers.insert(std::make_pair(triggerTags[i], factory));
  }
}

void Driver::start() {
  scanSourceTree();
  startSomeActions();
}

void Driver::startSomeActions() {
  while (activeActions.size() < maxConcurrentActions && !pendingActions.empty()) {
    OwnedPtr<ActionDriver> actionDriver;
    pendingActions.releaseFront(&actionDriver);
    ActionDriver* ptr = actionDriver.get();
    activeActions.adoptBack(&actionDriver);
    try {
      ptr->start();
    } catch (const std::exception& e) {
      ptr->threwException(e);
    } catch (...) {
      ptr->threwUnknownException();
    }
  }
}

void Driver::scanSourceTree() {
  OwnedPtrVector<File> fileQueue;

  {
    OwnedPtr<File> root;
    src->clone(&root);
    fileQueue.adoptBack(&root);
  }

  while (!fileQueue.empty()) {
    OwnedPtr<File> current;
    fileQueue.releaseBack(&current);

    if (current->isDirectory()) {
      OwnedPtrVector<File> list;
      current->list(list.appender());
      for (int i = 0; i < list.size(); i++) {
        OwnedPtr<File> child;
        list.release(i, &child);
        fileQueue.adoptBack(&child);
      }
    }

    // Don't allow actions to trigger on the root directory.
    if (current->hasParent()) {
      // Apply default tag.
      OwnedPtr<Provision> provision;
      provision.allocate();
      provision->tags.push_back(Tag::DEFAULT_TAG);
      current->clone(&provision->file);
      registerProvider(provision.get());
      rootProvisions.adoptBack(&provision);
    }
  }
}

void Driver::rescanForNewFactory(ActionFactory* factory) {
  // Apply triggers.
  std::vector<Tag> triggerTags;
  factory->enumerateTriggerTags(std::back_inserter(triggerTags));
  for (unsigned int i = 0; i < triggerTags.size(); i++) {
    ProvisionSet* provisions = tagMap.get(triggerTags[i]);
    if (provisions != NULL) {
      for (ProvisionSet::iterator iter = provisions->begin(); iter != provisions->end(); ++iter) {
        Provision* provision = *iter;
        OwnedPtr<Action> action;
        if (factory->tryMakeAction(triggerTags[i], provision->file.get(), &action)) {
          queueNewAction(&action, provision);
        }
      }
    }
  }
}

void Driver::queueNewAction(OwnedPtr<Action>* actionToAdopt, Provision* provision) {
  OwnedPtr<Dashboard::Task> task;
  dashboard->beginTask((*actionToAdopt)->getVerb(), provision->file->canonicalName(),
                       (*actionToAdopt)->isSilent() ? Dashboard::SILENT : Dashboard::NORMAL,
                       &task);

  OwnedPtr<ActionDriver> actionDriver;
  actionDriver.allocate(this, actionToAdopt, provision->file.get(), provision->contentHash, &task);
  actionsByTrigger.insert(std::make_pair(provision, actionDriver.get()));

  // Put new action on front of queue because it was probably triggered by another action that
  // just completed, and it's good to run related actions together to improve cache locality.
  pendingActions.adoptFront(&actionDriver);
}

void Driver::registerProvider(Provision* provision) {
  provision->contentHash = provision->file->contentHash();

  for (std::vector<Tag>::const_iterator iter = provision->tags.begin();
       iter != provision->tags.end(); ++iter) {
    const Tag& tag = *iter;
    ProvisionSet* provisions = tagMap.get(tag);
    if (provisions == NULL) {
      OwnedPtr<ProvisionSet> newProvisions;
      newProvisions.allocate();
      provisions = newProvisions.get();
      tagMap.adopt(tag, &newProvisions);
    }
    provisions->insert(provision);

    resetDependentActions(tag);

    fireTriggers(tag, provision);
  }
}

void Driver::resetDependentActions(const Tag& tag) {
  std::tr1::unordered_set<Tag, Tag::HashFunc> tagsToReset;
  tagsToReset.insert(tag);

  while (!tagsToReset.empty()) {
    std::vector<ActionDriver*> actionsToDelete;

    Tag currentTag = *tagsToReset.begin();
    tagsToReset.erase(tagsToReset.begin());

    std::pair<CompletedActionMap::const_iterator, CompletedActionMap::const_iterator>
        range = completedActions.equal_range(currentTag);
    int pendingActionsPos = pendingActions.size();
    for (CompletedActionMap::const_iterator iter = range.first; iter != range.second; ++iter) {
      if (iter->second->hasPreferredDependencyChanged(currentTag)) {
        // Reset action to pending.
        OwnedPtr<ActionDriver> action;
        if (completedActionPtrs.release(iter->second, &action)) {
          // Put on back of queue (as opposed to front) so that actions which are frequently reset
          // don't get redundantly rebuilt too much.  Note that we actually reset the action below.
          pendingActions.adoptBack(&action);
        } else {
          DEBUG_ERROR << "ActionDriver in completedActions but not completedActionPtrs?";
        }
      }
    }

    // Reset all the actions that we added to pendingActions.  We couldn't do this before because
    // it involves updating the CompletedActionMap.
    for (; pendingActionsPos < pendingActions.size(); pendingActionsPos++) {
      ActionDriver* action = pendingActions.get(pendingActionsPos);
      action->reset(&tagsToReset, &actionsToDelete);
    }

    for (unsigned int i = 0; i < actionsToDelete.size(); i++) {
      OwnedPtr<ActionDriver> ownedAction;

      if (actionsToDelete[i]->isRunning) {
        for (int j = 0; j < activeActions.size(); j++) {
          if (activeActions.get(j) == actionsToDelete[i]) {
            activeActions.releaseAndShift(j, &ownedAction);
            break;
          }
        }
        // Note that deleting the action will cancel whatever it's doing.
      } else if (actionsToDelete[i]->state == ActionDriver::PENDING) {
        // TODO:  Use better data structure for pendingActions.
        for (int j = 0; j < pendingActions.size(); j++) {
          if (pendingActions.get(j) == actionsToDelete[i]) {
            pendingActions.releaseAndShift(j, &ownedAction);
            break;
          }
        }
      } else {
        completedActionPtrs.release(actionsToDelete[i], &ownedAction);
        ownedAction->reset(&tagsToReset, &actionsToDelete);
      }

      // Let the action leave scope and be deleted.
    }
  }
}

void Driver::fireTriggers(const Tag& tag, Provision* provision) {
  std::pair<TriggerMap::const_iterator, TriggerMap::const_iterator>
      range = triggers.equal_range(tag);
  for (TriggerMap::const_iterator iter = range.first; iter != range.second; ++iter) {
    OwnedPtr<Action> triggeredAction;
    if (iter->second->tryMakeAction(tag, provision->file.get(), &triggeredAction)) {
      queueNewAction(&triggeredAction, provision);
    }
  }
}

}  // namespace ekam
