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

#ifndef KENTONSCODE_EKAM_MUXDASHBOARD_H_
#define KENTONSCODE_EKAM_MUXDASHBOARD_H_

#include <unordered_set>

#include "Dashboard.h"

namespace ekam {

class MuxDashboard : public Dashboard {
public:
  MuxDashboard();
  ~MuxDashboard();

  class Connector {
  public:
    Connector(MuxDashboard* mux, Dashboard* dashboard);
    ~Connector();

  private:
    MuxDashboard* mux;
    Dashboard* dashboard;
  };

  // implements Dashboard ----------------------------------------------------------------
  OwnedPtr<Task> beginTask(const std::string& verb, const std::string& noun, Silence silence);

private:
  class TaskImpl;

  std::unordered_set<TaskImpl*> tasks;
  std::unordered_set<Dashboard*> wrappedDashboards;
};

}  // namespace ekam

#endif  // KENTONSCODE_EKAM_MUXDASHBOARD_H_
