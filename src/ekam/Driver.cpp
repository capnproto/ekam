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

#include "Driver.h"

#include <queue>
#include <memory>
#include <stdexcept>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "base/Debug.h"
#include "os/EventGroup.h"

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
  ActionDriver(Driver* driver, OwnedPtr<Action> action,
               File* srcfile, Hash srcHash, OwnedPtr<Dashboard::Task> task);
  ~ActionDriver();

  void start();

  // implements BuildContext -------------------------------------------------------------
  File* findProvider(Tag id);
  File* findInput(const std::string& path);

  void provide(File* file, const std::vector<Tag>& tags);
  void install(File* file, InstallLocation location, const std::string& name);
  void log(const std::string& text);

  OwnedPtr<File> newOutput(const std::string& path);

  void addActionType(OwnedPtr<ActionFactory> factory);

  void passed();
  void failed();

  // implements ExceptionHandler ---------------------------------------------------------
  void threwException(const std::exception& e);
  void threwUnknownException();
  void noMoreEvents();

private:
  Driver* driver;
  OwnedPtr<Action> action;
  OwnedPtr<File> srcfile;
  Hash srcHash;
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

  Promise<void> asyncCallbackOp;

  bool isRunning;
  Promise<void> runningAction;

  OwnedPtrVector<File> outputs;

  struct Installation {
    File* file;
    InstallLocation location;
    std::string name;
  };
  std::vector<Installation> installations;

  OwnedPtrVector<Provision> provisions;
  OwnedPtrVector<std::vector<Tag> > providedTags;
  OwnedPtrVector<ActionFactory> providedFactories;

  // True if returned() is currently on the stack.  Causes destructor to abort.  Used for
  // debugging.
  bool currentlyExecutingReturned = false;

  void ensureRunning();
  void queueDoneCallback();
  void returned();
  void reset();
  Provision* choosePreferredProvider(const Tag& tag);
  File* provideInternal(File* file, const std::vector<Tag>& tags);

  friend class Driver;
};

Driver::ActionDriver::ActionDriver(Driver* driver, OwnedPtr<Action> action,
                                   File* srcfile, Hash srcHash,
                                   OwnedPtr<Dashboard::Task> task)
    : driver(driver), action(action.release()), srcfile(srcfile->clone()), srcHash(srcHash),
      dashboardTask(task.release()), state(PENDING), eventGroup(driver->eventManager, this),
      isRunning(false) {}
Driver::ActionDriver::~ActionDriver() {
  assert(!currentlyExecutingReturned);
}

void Driver::ActionDriver::start() {
  assert(state == PENDING);
  assert(!driver->dependencyTable.has<DependencyTable::ACTION>(this));
  assert(outputs.empty());
  assert(provisions.empty());
  assert(installations.empty());
  assert(providedFactories.empty());
  assert(!isRunning);

  state = RUNNING;
  isRunning = true;
  dashboardTask->setState(Dashboard::RUNNING);

  asyncCallbackOp = eventGroup.when()(
    [this]() {
      asyncCallbackOp.release();
      runningAction = action->start(&eventGroup, this);
    });
}

File* Driver::ActionDriver::findProvider(Tag tag) {
  ensureRunning();

  Provision* provision = choosePreferredProvider(tag);

  driver->dependencyTable.add(tag, this, provision);
  if (provision == NULL) {
    return NULL;
  } else {
    return provision->file.get();
  }
}

File* Driver::ActionDriver::findInput(const std::string& path) {
  ensureRunning();

  return findProvider(Tag::fromFile(path));
}

void Driver::ActionDriver::provide(File* file, const std::vector<Tag>& tags) {
  provideInternal(file, tags);
}

File* Driver::ActionDriver::provideInternal(File* file, const std::vector<Tag>& tags) {
  ensureRunning();

  // Find existing provision for this file, if any.
  // TODO:  Convert provisions into a map?
  Provision* provision = NULL;
  for (int i = 0; i < provisions.size(); i++) {
    if (provisions.get(i)->file->equals(file)) {
      provision = provisions.get(i);
      providedTags.get(i)->insert(providedTags.get(i)->end(), tags.begin(), tags.end());
      break;
    }
  }

  if (provision == NULL) {
    auto ownedProvision = newOwned<Provision>();
    provision = ownedProvision.get();
    provision->creator = this;
    provisions.add(ownedProvision.release());
    providedTags.add(newOwned<std::vector<Tag>>(tags));
  }

  provision->file = file->clone();
  return provision->file.get();
}

void Driver::ActionDriver::install(File* file, InstallLocation location, const std::string& name) {
  ensureRunning();

  std::string tagName(INSTALL_LOCATION_NAMES[location]);
  tagName.push_back(':');
  tagName.append(name);
  std::vector<Tag> tags;
  tags.push_back(Tag::fromName(tagName));
  File* ownedFile = provideInternal(file, tags);

  Installation installation = { ownedFile, location, name };
  installations.push_back(installation);
}

void Driver::ActionDriver::log(const std::string& text) {
  ensureRunning();
  dashboardTask->addOutput(text);
}

OwnedPtr<File> Driver::ActionDriver::newOutput(const std::string& path) {
  ensureRunning();
  OwnedPtr<File> file = driver->tmp->relative(path);

  recursivelyCreateDirectory(file->parent().get());

  OwnedPtr<File> result = file->clone();

  std::vector<Tag> tags;
  tags.push_back(Tag::DEFAULT_TAG);
  provide(file.get(), tags);

  outputs.add(file.release());

  return result;
}

void Driver::ActionDriver::addActionType(OwnedPtr<ActionFactory> factory) {
  ensureRunning();
  providedFactories.add(factory.release());
}

void Driver::ActionDriver::noMoreEvents() {
  if (isRunning) {
    if (state == RUNNING) {
      state = DONE;
      queueDoneCallback();
    }
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
  asyncCallbackOp = driver->eventManager->when()(
    [this]() {
      asyncCallbackOp.release();
      Driver* driver = this->driver;
      returned();  // may delete this
      driver->startSomeActions();
    });
}

void Driver::ActionDriver::threwException(const std::exception& e) {
  ensureRunning();
  dashboardTask->addOutput(std::string("uncaught exception: ") + e.what() + "\n");
  asyncCallbackOp.release();
  state = FAILED;
  returned();
}

void Driver::ActionDriver::threwUnknownException() {
  ensureRunning();
  dashboardTask->addOutput("uncaught exception of unknown type\n");
  asyncCallbackOp.release();
  state = FAILED;
  returned();
}

// Poor man's kj::defer.
template<typename Func>
class Deferred {
public:
  inline Deferred(Func&& func): func(func), canceled(false) {}
  inline ~Deferred() noexcept(false) { if (!canceled) func(); }

  // This move constructor is usually optimized away by the compiler.
  inline Deferred(Deferred&& other): func(other.func), canceled(false) {
    other.canceled = true;
  }
private:
  Func func;
  bool canceled;
};

template <typename Func>
Deferred<Func> defer(Func&& func) {
  // Returns an object which will invoke the given functor in its destructor. The object is not
  // copyable but is movable with the semantics you'd expect. Since the return type is private,
  // you need to assign to an `auto` variable.
  return Deferred<Func>(std::forward<Func>(func));
}


void Driver::ActionDriver::returned() {
  ensureRunning();

  currentlyExecutingReturned = true;
  auto _ = defer([this]() {
    currentlyExecutingReturned = false;
  });

  // Cancel anything still running.
  runningAction.release();
  isRunning = false;

  // Pull self out of driver->activeActions.
  OwnedPtr<ActionDriver> self;
  for (int i = 0; i < driver->activeActions.size(); i++) {
    if (driver->activeActions.get(i) == this) {
      self = driver->activeActions.releaseAndShift(i);
      break;
    }
  }

  driver->completedActionPtrs.add(this, self.release());

  if (state == FAILED) {
    // Failed, possibly due to missing dependencies.
    provisions.clear();
    installations.clear();
    providedTags.clear();
    providedFactories.clear();
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
        provisions.add(provisionsToFilter.release(i));
      }
    }

    // Register providers.  But, don't allow our own dependencies to depend on them.
    std::unordered_set<ActionDriver*> deps;
    driver->getTransitiveDependencies(this, &deps);
    for (int i = 0; i < provisions.size(); i++) {
      driver->registerProvider(provisions.get(i), *providedTags.get(i), deps);
    }
    providedTags.clear();  // Not needed anymore.

    // Register factories.
    for (int i = 0; i < providedFactories.size(); i++) {
      driver->addActionFactory(providedFactories.get(i));
      driver->rescanForNewFactory(providedFactories.get(i));
    }

    // Install files.
    for (size_t i = 0; i < installations.size(); i++) {
      File* installDir = driver->installDirs[installations[i].location];
      OwnedPtr<File> target = installDir->relative(installations[i].name);
      if (target->exists()) {
        target->unlink();
      } else {
        if (!installDir->isDirectory()) {
          // Can't rely on recursivelyCreateDirectory() for the root directory because it will
          // call parent() on it which will throw.
          installDir->createDirectory();
        }
        recursivelyCreateDirectory(target->parent().get());
      }
      target->link(installations[i].file);
    }
  }
}

void Driver::ActionDriver::reset() {
  assert(!currentlyExecutingReturned);

  if (state == PENDING) {
    // Nothing to do.
    return;
  }

  OwnedPtr<ActionDriver> self;

  if (isRunning) {
    dashboardTask->setState(Dashboard::BLOCKED);
    runningAction.release();
    asyncCallbackOp.release();

    for (int i = 0; i < driver->activeActions.size(); i++) {
      if (driver->activeActions.get(i) == this) {
        self = driver->activeActions.releaseAndShift(i);
        break;
      }
    }

    isRunning = false;
  } else {
    if (!driver->completedActionPtrs.release(this, &self)) {
      throw std::logic_error("Action not running or pending, but not in completedActionPtrs?");
    }
  }

  state = PENDING;

  // Put on back of queue (as opposed to front) so that actions which are frequently reset
  // don't get redundantly rebuilt too much.  We add the action to the queue before resetting
  // dependents so that this action gets re-run before its dependents.
  // TODO:  The second point probably doesn't help much when multiprocessing.  Maybe the
  //   action queue should really be a graph that remembers what depended on what the last
  //   time we ran them, and avoids re-running any action before re-running actions on which it
  //   depended last time.
  driver->pendingActions.pushBack(self.release());

  // Reset dependents.
  for (int i = 0; i < provisions.size(); i++) {
    driver->resetDependentActions(provisions.get(i));
  }

  // Actions created by any provided ActionFactories must be deleted.
  for (int i = 0; i < providedFactories.size(); i++) {
    ActionFactory* factory = providedFactories.get(i);

    std::vector<ActionDriver*> actionsToDelete;
    for (ActionTriggersTable::SearchIterator<ActionTriggersTable::FACTORY>
         iter(driver->actionTriggersTable, factory); iter.next();) {
      // Can't call reset() directly here because it may invalidate our iterator.
      actionsToDelete.push_back(iter.cell<ActionTriggersTable::ACTION>());
    }

    for (size_t j = 0; j < actionsToDelete.size(); j++) {
      actionsToDelete[j]->reset();

      // TODO:  Use better data structure for pendingActions.  For now we have to iterate
      //   through the whole thing to find the action we're deleting.  We iterate from the back
      //   since it's likely the action was just added there.
      for (int k = driver->pendingActions.size() - 1; k >= 0; k--) {
        if (driver->pendingActions.get(k) == actionsToDelete[j]) {
          driver->pendingActions.releaseAndShift(k);
          break;
        }
      }
    }

    driver->actionTriggersTable.erase<ActionTriggersTable::FACTORY>(factory);
    driver->triggers.erase<TriggerTable::FACTORY>(factory);
  }

  // Remove all entries in dependencyTable pointing at this action.
  driver->dependencyTable.erase<DependencyTable::ACTION>(this);

  provisions.clear();
  installations.clear();
  providedTags.clear();
  providedFactories.clear();
  outputs.clear();
}

Driver::Provision* Driver::ActionDriver::choosePreferredProvider(const Tag& tag) {
  TagTable::SearchIterator<TagTable::TAG> iter(driver->tagTable, tag);

  if (!iter.next()) {
    return NULL;
  } else {
    std::string srcName = srcfile->canonicalName();
    Provision* bestMatch = iter.cell<TagTable::PROVISION>();

    if (iter.next()) {
      // There are multiple files with this tag.  We must choose which one we like best.
      std::string bestMatchName = bestMatch->file->canonicalName();
      int bestMatchDepth = fileDepth(bestMatchName);
      int bestMatchCommonPrefix = commonPrefixLength(srcName, bestMatchName);

      do {
        Provision* candidate = iter.cell<TagTable::PROVISION>();
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
      } while(iter.next());
    }

    return bestMatch;
  }
}

// =======================================================================================

Driver::Driver(EventManager* eventManager, Dashboard* dashboard, File* tmp,
               File* installDirs[BuildContext::INSTALL_LOCATION_COUNT], int maxConcurrentActions,
               ActivityObserver* activityObserver)
    : eventManager(eventManager), dashboard(dashboard), tmp(tmp),
      maxConcurrentActions(maxConcurrentActions), activityObserver(activityObserver) {
  if (!tmp->isDirectory()) {
    tmp->createDirectory();
  }

  for (int i = 0; i < BuildContext::INSTALL_LOCATION_COUNT; i++) {
    this->installDirs[i] = installDirs[i];
  }
}

Driver::~Driver() {}

void Driver::addActionFactory(ActionFactory* factory) {
  std::vector<Tag> triggerTags;
  factory->enumerateTriggerTags(std::back_inserter(triggerTags));
  for (unsigned int i = 0; i < triggerTags.size(); i++) {
    triggers.add(triggerTags[i], factory);
  }
}

void Driver::addSourceFile(File* file) {
  OwnedPtr<Provision> provision;
  if (rootProvisions.release(file, &provision)) {
    // Source file was modified.  Reset all actions dependent on the old version.
    resetDependentActions(provision.get());
  }

  // Apply default tag.
  std::vector<Tag> tags;
  tags.push_back(Tag::DEFAULT_TAG);

  provision = newOwned<Provision>();
  provision->creator = nullptr;
  provision->file = file->clone();
  registerProvider(provision.get(), tags, std::unordered_set<ActionDriver*>());
  File* key = provision->file.get();  // cannot inline due to undefined evaluation order
  rootProvisions.add(key, provision.release());

  startSomeActions();
}

void Driver::removeSourceFile(File* file) {
  OwnedPtr<Provision> provision;
  if (rootProvisions.release(file, &provision)) {
    resetDependentActions(provision.get());

    // In case some active actions were canceled.
    startSomeActions();
  } else {
    DEBUG_ERROR << "Tried to remove source file that wasn't ever added: " << file->canonicalName();
  }
}

void Driver::startSomeActions() {
  while (activeActions.size() < maxConcurrentActions && !pendingActions.empty()) {
    if (activityObserver != nullptr) activityObserver->startingAction();
    OwnedPtr<ActionDriver> actionDriver = pendingActions.popFront();
    ActionDriver* ptr = actionDriver.get();
    activeActions.add(actionDriver.release());
    try {
      ptr->start();
    } catch (const std::exception& e) {
      ptr->threwException(e);
    } catch (...) {
      ptr->threwUnknownException();
    }
  }

  if (activeActions.size() == 0) {
    bool hasFailures = dumpErrors();
    if (activityObserver != nullptr) activityObserver->idle(hasFailures);
  }
}

void Driver::rescanForNewFactory(ActionFactory* factory) {
  // Apply triggers.
  std::vector<Tag> triggerTags;
  factory->enumerateTriggerTags(std::back_inserter(triggerTags));
  for (unsigned int i = 0; i < triggerTags.size(); i++) {
    for (TagTable::SearchIterator<TagTable::TAG> iter(tagTable, triggerTags[i]); iter.next();) {
      Provision* provision = iter.cell<TagTable::PROVISION>();
      OwnedPtr<Action> action = factory->tryMakeAction(triggerTags[i], provision->file.get());
      if (action != NULL) {
        queueNewAction(factory, action.release(), provision);
      }
    }
  }
}

void Driver::queueNewAction(ActionFactory* factory, OwnedPtr<Action> action,
                            Provision* provision) {
  OwnedPtr<Dashboard::Task> task = dashboard->beginTask(
      action->getVerb(), provision->file->canonicalName(),
      action->isSilent() ? Dashboard::SILENT : Dashboard::NORMAL);

  OwnedPtr<ActionDriver> actionDriver =
      newOwned<ActionDriver>(this, action.release(), provision->file.get(), provision->contentHash,
                             task.release());
  actionTriggersTable.add(factory, provision, actionDriver.get());

  // Put new action on front of queue because it was probably triggered by another action that
  // just completed, and it's good to run related actions together to improve cache locality.
  pendingActions.pushFront(actionDriver.release());
}

void Driver::getTransitiveDependencies(
    ActionDriver* action, std::unordered_set<ActionDriver*>* deps) {
  if (action != nullptr && deps->insert(action).second) {
    for (ActionTriggersTable::SearchIterator<ActionTriggersTable::ACTION>
         iter(actionTriggersTable, action); iter.next();) {
      getTransitiveDependencies(iter.cell<ActionTriggersTable::PROVISION>()->creator, deps);
    }
    for (DependencyTable::SearchIterator<DependencyTable::ACTION>
         iter(dependencyTable, action); iter.next();) {
      Provision* provision = iter.cell<DependencyTable::PROVISION>();
      if (provision != nullptr) {
        getTransitiveDependencies(provision->creator, deps);
      }
    }
  }
}

void Driver::registerProvider(Provision* provision, const std::vector<Tag>& tags,
                              const std::unordered_set<ActionDriver*>& dependencies) {
  provision->contentHash = provision->file->contentHash();

  for (std::vector<Tag>::const_iterator iter = tags.begin(); iter != tags.end(); ++iter) {
    const Tag& tag = *iter;
    tagTable.add(tag, provision);

    resetDependentActions(tag, dependencies);

    fireTriggers(tag, provision);
  }
}

void Driver::resetDependentActions(const Tag& tag,
                                   const std::unordered_set<ActionDriver*>& dependencies) {
  std::unordered_set<Provision*> provisionsToReset;

  std::vector<ActionDriver*> actionsToReset;

  for (DependencyTable::SearchIterator<DependencyTable::TAG> iter(dependencyTable, tag);
       iter.next();) {
    ActionDriver* action = iter.cell<DependencyTable::ACTION>();

    // Don't reset an action that contributed to the creation of this tag in the first place, since
    // that would lead to an infinite loop of rebuilding the same action.
    if (dependencies.count(action) == 0) {
      Provision* previousProvider = iter.cell<DependencyTable::PROVISION>();

      if (action->choosePreferredProvider(tag) != previousProvider) {
        // We can't just call reset() here because it could invalidate our iterator.
        actionsToReset.push_back(action);
      }
    } else {
      DEBUG_INFO << "Action's inputs are affected by its outputs.";
    }
  }

  for (size_t i = 0; i < actionsToReset.size(); i++) {
    // Only reset the action if it is still in the dependency table.  If not, it was already
    // reset (and possibly deleted!) elsewhere.
    if (dependencyTable.find<DependencyTable::ACTION>(actionsToReset[i]) != nullptr) {
      actionsToReset[i]->reset();
    }
  }
}

void Driver::resetDependentActions(Provision* provision) {
  // Reset dependents of this provision.
  {
    std::vector<ActionDriver*> actionsToReset;
    for (DependencyTable::SearchIterator<DependencyTable::PROVISION>
         iter(dependencyTable, provision); iter.next();) {
      // Can't call reset() directly here because it may invalidate our iterator.
      actionsToReset.push_back(iter.cell<DependencyTable::ACTION>());
    }
    for (size_t j = 0; j < actionsToReset.size(); j++) {
      // Only reset the action if it is still in the dependency table.  If not, it was already
      // reset (and possibly deleted!) elsewhere.
      if (dependencyTable.find<DependencyTable::ACTION>(actionsToReset[j]) != nullptr) {
        actionsToReset[j]->reset();
      }
    }
    if (dependencyTable.erase<DependencyTable::PROVISION>(provision) > 0) {
      DEBUG_ERROR << "Resetting dependents should have removed this provision from "
                     "dependencyTable.";
    }
  }

  // Everything triggered by this provision must be deleted.
  {
    std::vector<ActionDriver*> actionsToDelete;

    for (ActionTriggersTable::SearchIterator<ActionTriggersTable::PROVISION>
         iter(actionTriggersTable, provision); iter.next();) {
      // Can't call reset() directly here because it may invalidate our iterator.
      actionsToDelete.push_back(iter.cell<ActionTriggersTable::ACTION>());
    }

    for (size_t j = 0; j < actionsToDelete.size(); j++) {
      actionsToDelete[j]->reset();

      // TODO:  Use better data structure for pendingActions.  For now we have to iterate
      //   through the whole thing to find the action we're deleting.  We iterate from the back
      //   since it's likely the action was just added there.
      for (int k = pendingActions.size() - 1; k >= 0; k--) {
        if (pendingActions.get(k) == actionsToDelete[j]) {
          pendingActions.releaseAndShift(k);
          break;
        }
      }
    }

    actionTriggersTable.erase<ActionTriggersTable::PROVISION>(provision);
  }

  tagTable.erase<TagTable::PROVISION>(provision);
}

void Driver::fireTriggers(const Tag& tag, Provision* provision) {
  for (TriggerTable::SearchIterator<TriggerTable::TAG> iter(triggers, tag); iter.next();) {
    ActionFactory* factory = iter.cell<TriggerTable::FACTORY>();
    OwnedPtr<Action> triggeredAction = factory->tryMakeAction(tag, provision->file.get());
    if (triggeredAction != NULL) {
      queueNewAction(factory, triggeredAction.release(), provision);
    }
  }
}

bool Driver::dumpErrors() {
  bool hasFailures = false;
  for (OwnedPtrMap<ActionDriver*, ActionDriver>::Iterator iter(completedActionPtrs); iter.next();) {
    if (iter.key()->state == ActionDriver::FAILED) {
      iter.value()->dashboardTask->setState(Dashboard::FAILED);
      hasFailures = true;
    }
  }
  return hasFailures;
}

}  // namespace ekam
