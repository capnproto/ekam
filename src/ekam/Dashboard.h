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

#ifndef KENTONSCODE_EKAM_DASHBOARD_H_
#define KENTONSCODE_EKAM_DASHBOARD_H_

#include <string>
#include "base/OwnedPtr.h"

namespace ekam {

class Dashboard {
public:
  virtual ~Dashboard();

  enum TaskState {
    PENDING,  // Default state.
    RUNNING,
    DONE,
    PASSED,   // Like DONE, but should be displayed prominently (hint: test result).
    FAILED,
    BLOCKED
  };

  class Task {
  public:
    virtual ~Task();

    virtual void setState(TaskState state) = 0;
    virtual void addOutput(const std::string& text) = 0;
  };

  enum Silence {
    SILENT,
    NORMAL
  };

  virtual OwnedPtr<Task> beginTask(const std::string& verb, const std::string& noun,
                                   Silence silence) = 0;
};

class EventManager;

OwnedPtr<Dashboard> initNetworkDashboard(EventManager* eventManager, const std::string& address,
                                         OwnedPtr<Dashboard> dashboardToWrap);

}  // namespace ekam

#endif  // KENTONSCODE_EKAM_DASHBOARD_H_
