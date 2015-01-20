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

#include "MuxDashboard.h"

#include <stdexcept>

#include "base/Debug.h"

namespace ekam {

class MuxDashboard::TaskImpl : public Dashboard::Task {
public:
  TaskImpl(MuxDashboard* mux, const std::string& verb, const std::string& noun, Silence silence);
  ~TaskImpl();

  void attach(Dashboard* dashboard);
  void detach(Dashboard* dashboard);

  // implements Task ---------------------------------------------------------------------
  void setState(TaskState state);
  void addOutput(const std::string& text);

private:
  MuxDashboard* mux;
  TaskState state;
  Silence silence;
  std::string verb;
  std::string noun;
  std::string outputText;

  typedef OwnedPtrMap<Dashboard*, Task> WrappedTasksMap;
  WrappedTasksMap wrappedTasks;

  static const size_t OUTPUT_BUFER_LIMIT = 4096 - sizeof("\n...(log truncated)...");
};

const size_t MuxDashboard::TaskImpl::OUTPUT_BUFER_LIMIT;

MuxDashboard::TaskImpl::TaskImpl(MuxDashboard* mux, const std::string& verb,
                                 const std::string& noun, Silence silence)
    : mux(mux), state(PENDING), silence(silence), verb(verb), noun(noun) {
  mux->tasks.insert(this);

  for (std::tr1::unordered_set<Dashboard*>::iterator iter = mux->wrappedDashboards.begin();
       iter != mux->wrappedDashboards.end(); ++iter) {
    wrappedTasks.add(*iter, (*iter)->beginTask(verb, noun, silence));
  }
}
MuxDashboard::TaskImpl::~TaskImpl() {
  mux->tasks.erase(this);
}

void MuxDashboard::TaskImpl::attach(Dashboard* dashboard) {
  OwnedPtr<Task> wrappedTask = dashboard->beginTask(verb, noun, silence);
  if (!outputText.empty()) {
    wrappedTask->addOutput(outputText);
  }
  if (state != PENDING) {
    wrappedTask->setState(state);
  }

  if (!wrappedTasks.addIfNew(dashboard, wrappedTask.release())) {
    DEBUG_ERROR << "Tried to attach task to a dashboard to which the task was already attached.";
  }
}

void MuxDashboard::TaskImpl::detach(Dashboard* dashboard) {
  if (wrappedTasks.erase(dashboard) == 0) {
    DEBUG_ERROR << "Tried to detach task from dashboard to which it was not attached.";
  }
}

void MuxDashboard::TaskImpl::setState(TaskState state) {
  if (state == PENDING || state == RUNNING) {
    outputText.clear();
  }

  this->state = state;
  for (WrappedTasksMap::Iterator iter(wrappedTasks); iter.next();) {
    iter.value()->setState(state);
  }
}

void MuxDashboard::TaskImpl::addOutput(const std::string& text) {
  if (outputText.size() < OUTPUT_BUFER_LIMIT) {
    if (outputText.size() + text.size() < OUTPUT_BUFER_LIMIT) {
      outputText.append(text);
    } else {
      outputText.append(text, 0, OUTPUT_BUFER_LIMIT - outputText.size());
      outputText.append("\n...(log truncated)...");
    }
  }

  for (WrappedTasksMap::Iterator iter(wrappedTasks); iter.next();) {
    iter.value()->addOutput(text);
  }
}

// =======================================================================================

MuxDashboard::MuxDashboard() {}
MuxDashboard::~MuxDashboard() {}

OwnedPtr<Dashboard::Task> MuxDashboard::beginTask(const std::string& verb, const std::string& noun,
                                                  Silence silence) {
  return newOwned<TaskImpl>(this, verb, noun, silence);
}

MuxDashboard::Connector::Connector(MuxDashboard* mux, Dashboard* dashboard)
    : mux(mux), dashboard(dashboard) {
  if (!mux->wrappedDashboards.insert(dashboard).second) {
    throw std::invalid_argument("Mux is already attached to this dashboard.");
  }

  for (std::tr1::unordered_set<TaskImpl*>::iterator iter = mux->tasks.begin();
       iter != mux->tasks.end(); ++iter) {
    (*iter)->attach(dashboard);
  }
}

MuxDashboard::Connector::~Connector() {
  if (mux->wrappedDashboards.erase(dashboard) == 0) {
    DEBUG_ERROR << "Deleting MuxDashboard connection that was never made?";
  }

  for (std::tr1::unordered_set<TaskImpl*>::iterator iter = mux->tasks.begin();
       iter != mux->tasks.end(); ++iter) {
    (*iter)->detach(dashboard);
  }
}

}  // namespace ekam
