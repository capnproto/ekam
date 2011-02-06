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

#include "MuxDashboard.h"

#include <stdexcept>

#include "Debug.h"

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

MuxDashboard::Connector::~Connector() {}

}  // namespace ekam
