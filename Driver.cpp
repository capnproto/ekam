// kake2 -- http://code.google.com/p/kake2
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
//     * Neither the name of the kake2 project nor the names of its
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

// kqueue
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <sys/time.h>

#include "Debug.h"

namespace kake2 {

class Driver::ActionDriver : public BuildContext {
public:
  ActionDriver(Driver* driver, OwnedPtr<Action>* actionToAdopt, File* tmpdir,
               OwnedPtr<Dashboard::Task>* taskToAdopt);
  ~ActionDriver();

  void start();

  // implements BuildContext -------------------------------------------------------------
  File* findProvider(EntityId id, const std::string& title);
  File* findOptionalProvider(EntityId id);

  void provide(File* file, const std::vector<EntityId>& entities);
  void log(const std::string& text);

  void newOutput(const std::string& basename, OwnedPtr<File>* output);

  void success();
  void passed();
  void failed();

  void onProcessExit(pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt);

private:
  Driver* driver;
  OwnedPtr<Action> action;
  OwnedPtr<File> tmpdir;
  OwnedPtr<Dashboard::Task> dashboardTask;

  enum {
    PENDING,
    RUNNING,
    SUCCEEDED,
    PASSED,
    FAILED
  } state;

  typedef std::tr1::unordered_map<EntityId, std::string, EntityId::HashFunc> MissingDependencyMap;
  MissingDependencyMap missingDependencies;

  OwnedPtrMap<pid_t, ProcessExitCallback> processExitCallbacks;

  OwnedPtrVector<File> outputs;

  struct Provision {
    OwnedPtr<File> file;
    std::vector<EntityId> entities;
  };

  OwnedPtrVector<Provision> provisions;

  void ensureRunning();
  void cancelPendingEvents();
  void threwException(const std::exception& e);
  void threwUnknownException();
  void returned();

  friend class Driver;
};

Driver::ActionDriver::ActionDriver(Driver* driver, OwnedPtr<Action>* actionToAdopt, File* tmpdir,
                                   OwnedPtr<Dashboard::Task>* taskToAdopt)
    : driver(driver), state(PENDING) {
  action.adopt(actionToAdopt);
  tmpdir->clone(&this->tmpdir);
  dashboardTask.adopt(taskToAdopt);
}
Driver::ActionDriver::~ActionDriver() {
  cancelPendingEvents();
}

void Driver::ActionDriver::start() {
  if (state != PENDING) {
    DEBUG_ERROR << "State must be PENDING here.";
  }
  state = RUNNING;
  dashboardTask->setState(Dashboard::RUNNING);
  action->start(this);
}

File* Driver::ActionDriver::findProvider(EntityId id, const std::string& title) {
  ensureRunning();
  File* result = findOptionalProvider(id);
  if (result == NULL) {
    missingDependencies[id] = title;
  }
  return result;
}

File* Driver::ActionDriver::findOptionalProvider(EntityId id) {
  ensureRunning();
  EntityMap::const_iterator iter = driver->entityMap.find(id);
  if (iter == driver->entityMap.end()) {
    return NULL;
  } else {
    return iter->second;
  }
}

void Driver::ActionDriver::provide(File* file, const std::vector<EntityId>& entities) {
  ensureRunning();

  OwnedPtr<Provision> provision;
  provision.allocate();
  file->clone(&provision->file);
  provision->entities = entities;

  provisions.adoptBack(&provision);
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
  outputs.adoptBack(&file);
}

void Driver::ActionDriver::success() {
  ensureRunning();

  if (!missingDependencies.empty()) {
    throw std::runtime_error("Action reported success despite missing dependencies.");
  }

  state = SUCCEEDED;
}

void Driver::ActionDriver::passed() {
  ensureRunning();

  if (!missingDependencies.empty()) {
    throw std::runtime_error("Action reported success despite missing dependencies.");
  }

  state = PASSED;
}

void Driver::ActionDriver::failed() {
  ensureRunning();

  state = FAILED;
}

void Driver::ActionDriver::onProcessExit(
    pid_t process, OwnedPtr<ProcessExitCallback>* callbackToAdopt) {
  ensureRunning();

  if (!driver->processMap.insert(std::make_pair(process, this)).second) {
    throw std::invalid_argument("A callback is already registered for this process.");
  }
  processExitCallbacks.adopt(process, callbackToAdopt);

  struct kevent event;
  EV_SET(&event, process, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL);
  int n = kevent(driver->kqueueFd, &event, 1, NULL, 0, NULL);
  if (n < 0) {
    DEBUG_ERROR << "kevent: " << strerror(errno);
  } else if (n > 0) {
    DEBUG_ERROR << "kevent() returned events when not asked to.";
  }
}

void Driver::ActionDriver::ensureRunning() {
  if (state != RUNNING) {
    throw std::runtime_error("Action is not running.");
  }
}

void Driver::ActionDriver::cancelPendingEvents() {
  for (OwnedPtrMap<pid_t, ProcessExitCallback>::Iterator iter(processExitCallbacks);
       iter.next();) {
    struct kevent event;
    EV_SET(&event, iter.key(), EVFILT_PROC, EV_DELETE, NOTE_EXIT, 0, NULL);
    int n = kevent(driver->kqueueFd, &event, 1, NULL, 0, NULL);
    if (n < 0) {
      DEBUG_ERROR << "kevent: " << strerror(errno);
    } else if (n > 0) {
      DEBUG_ERROR << "kevent() returned events when not asked to.";
    }

    if (driver->processMap.erase(iter.key()) == 0) {
      DEBUG_ERROR << "Process was in ActionDriver's map but not in Driver's.";
    }
  }
  processExitCallbacks.clear();
}

void Driver::ActionDriver::threwException(const std::exception& e) {
  dashboardTask->addOutput(std::string("uncaught exception: ") + e.what() + "\n");
  state = FAILED;
  returned();
}

void Driver::ActionDriver::threwUnknownException() {
  dashboardTask->addOutput("uncaught exception of unknown type\n");
  state = FAILED;
  returned();
}

void Driver::ActionDriver::returned() {
  if (state == PENDING) {
    DEBUG_ERROR << "State should not be PENDING here.";
  }

  OwnedPtr<ActionDriver> self;
  for (int i = 0; i < driver->activeActions.size(); i++) {
    if (driver->activeActions.get(i) == this) {
      driver->activeActions.releaseAndShift(i, &self);
      break;
    }
  }

  if (!missingDependencies.empty()) {
    // Failed due to missing dependencies.

    // Reset state to PENDING.
    state = PENDING;
    cancelPendingEvents();
    provisions.clear();
    outputs.clear();
    dashboardTask->setState(Dashboard::BLOCKED);

    // Insert self into the blocked actions map.
    driver->blockedActionPtrs.adopt(this, &self);
    for (MissingDependencyMap::const_iterator iter = missingDependencies.begin();
         iter != missingDependencies.end(); ++iter) {
      driver->blockedActions.insert(std::make_pair(iter->first, this));
    }
  } else {
    if (state == SUCCEEDED || state == PASSED) {
      dashboardTask->setState(state == PASSED ? Dashboard::PASSED : Dashboard::SUCCESS);

      // Register providers.
      for (int i = 0; i < provisions.size(); i++) {
        File* file = provisions.get(i)->file.get();
        driver->filePtrs.adopt(file, &provisions.get(i)->file);

        for (std::vector<EntityId>::const_iterator iter = provisions.get(i)->entities.begin();
             iter != provisions.get(i)->entities.end(); ++iter) {
          driver->entityMap[*iter] = file;

          // Unblock blocked actions.
          std::pair<BlockedActionMap::const_iterator, BlockedActionMap::const_iterator>
              range = driver->blockedActions.equal_range(*iter);
          for (BlockedActionMap::const_iterator iter2 = range.first;
               iter2 != range.second; ++iter2) {
            iter2->second->missingDependencies.erase(*iter);
            if (iter2->second->missingDependencies.empty()) {
              // No more missing deps.  Promote to runnable.
              OwnedPtr<ActionDriver> action;
              if (driver->blockedActionPtrs.release(iter2->second, &action)) {
                driver->pendingActions.adoptBack(&action);
              } else {
                DEBUG_ERROR << "Action not in blockedActionPtrs?";
              }
            }
          }
          driver->blockedActions.erase(range.first, range.second);
        }
      }

      // Enqueue new actions based on output files.
      for (int i = 0; i < outputs.size(); i++) {
        driver->scanForActions(outputs.get(i), outputs.get(i));
      }
    } else {
      dashboardTask->setState(Dashboard::FAILED);
    }
  }
}

// =======================================================================================

Driver::Driver(Dashboard* dashboard, File* src, File* tmp)
    : dashboard(dashboard), src(src), tmp(tmp), kqueueFd(kqueue()) {
  if (kqueueFd < 0) {
    std::string error(strerror(errno));
    throw std::runtime_error("kqueue: " + error);
  }
}

Driver::~Driver() {
  if (close(kqueueFd) < 0) {
    perror("close(kqueue)");
  }
}

void Driver::addActionFactory(const std::string& name, ActionFactory* factory) {
  actionFactories[name] = factory;
}

void Driver::run(int maxConcurrentActions) {
  scanForActions(src, tmp);

  while (!pendingActions.empty() || !activeActions.empty()) {
    while (activeActions.size() >= maxConcurrentActions) {
      handleEvent();
    }

    OwnedPtr<ActionDriver> actionDriver;
    pendingActions.releaseBack(&actionDriver);
    ActionDriver* ptr = actionDriver.get();
    activeActions.adoptBack(&actionDriver);
    try {
      ptr->start();
      ptr->returned();
    } catch (const std::exception& e) {
      ptr->threwException(e);
    } catch (...) {
      ptr->threwUnknownException();
    }
  }

  // Error out all blocked tasks.
  for (OwnedPtrMap<ActionDriver*, ActionDriver>::Iterator iter(blockedActionPtrs); iter.next();) {
    iter.value()->dashboardTask->setState(Dashboard::FAILED);
  }
}

void Driver::handleEvent() {
  DEBUG_INFO << "Waiting for events...";

  struct kevent event;
  int n = kevent(kqueueFd, NULL, 0, &event, 1, NULL);

  DEBUG_INFO << "Received event.";

  if (n < 0) {
    DEBUG_ERROR << "kevent: " << strerror(errno);
  } else if (n == 0) {
    DEBUG_ERROR << "kevent() timed out, but timeout was infinite.";
  } else {
    if (n > 1) {
      DEBUG_ERROR << "kevent() returned more events than requested.";
    }

    // Got an event.
    switch (event.filter) {
      case EVFILT_PROC: {
        pid_t pid = event.ident;

        if (event.fflags & ~NOTE_EXIT) {
          DEBUG_ERROR << "EVFILT_PROC kevent had unexpected fflags: " << event.fflags;
        }

        if (event.fflags & NOTE_EXIT) {
          DEBUG_INFO << "Process " << pid << " exited with status: " << event.data;

          int status;

          if (WIFEXITED(event.data)) {
            status = WEXITSTATUS(event.data);
          } else if (WIFSIGNALED(event.data)) {
            status = -WTERMSIG(event.data);
          } else {
            DEBUG_ERROR << "kake2 internal error: Didn't understand process exit status.";
            status = 1;
          }

          ProcessMap::iterator iter = processMap.find(pid);
          if (iter == processMap.end()) {
            DEBUG_ERROR << "Got process exit event that no one was waiting for.";
          } else {
            ActionDriver* action = iter->second;
            processMap.erase(iter);
            OwnedPtr<ProcessExitCallback> callback;
            if (action->processExitCallbacks.release(pid, &callback)) {
              try {
                callback->done(status);
                action->returned();
              } catch (const std::exception& e) {
                action->threwException(e);
              } catch (...) {
                action->threwUnknownException();
              }
            } else {
              DEBUG_ERROR << "PID not on Action's processExitCallbacks map.";
            }
          }
        }

        break;
      }
      default:
        DEBUG_ERROR << "kevent() returned unknown filter type: %d\n" << event.filter;
        break;
    }
  }
}

namespace {
struct SrcTmpPair {
  OwnedPtr<File> srcFile;
  OwnedPtr<File> tmpLocation;
};
}  // namespace

void Driver::scanForActions(File* src, File* tmp) {
  OwnedPtrVector<SrcTmpPair> fileQueue;

  {
    OwnedPtr<SrcTmpPair> root;
    root.allocate();
    src->clone(&root->srcFile);
    tmp->clone(&root->tmpLocation);
    fileQueue.adoptBack(&root);
  }

  while (!fileQueue.empty()) {
    OwnedPtr<SrcTmpPair> current;
    fileQueue.releaseBack(&current);

    if (current->srcFile->isDirectory()) {
      if (!current->tmpLocation->isDirectory()) {
        current->tmpLocation->createDirectory();
      }

      OwnedPtrVector<File> list;
      current->srcFile->list(list.appender());
      for (int i = 0; i < list.size(); i++) {
        OwnedPtr<SrcTmpPair> newPair;
        newPair.allocate();
        list.release(i, &newPair->srcFile);
        current->tmpLocation->relative(newPair->srcFile->basename(), &newPair->tmpLocation);
        fileQueue.adoptBack(&newPair);
      }
    } else {
      for (ActionFactoryMap::const_iterator iter = actionFactories.begin();
           iter != actionFactories.end(); ++iter) {
        ActionFactory* factory = iter->second;
        OwnedPtr<Action> action;
        factory->tryMakeAction(current->srcFile.get(), &action);
        if (action != NULL) {
          OwnedPtr<Dashboard::Task> task;
          dashboard->beginTask(action->getVerb(), current->srcFile->displayName(), &task);

          OwnedPtr<ActionDriver> actionDriver;
          actionDriver.allocate(this, &action, current->tmpLocation.get(), &task);

          pendingActions.adoptBack(&actionDriver);
        }
      }
    }
  }
}

}  // namespace kake2
