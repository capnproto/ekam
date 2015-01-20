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

#ifndef KENTONSCODE_OS_EVENTGROUP_H_
#define KENTONSCODE_OS_EVENTGROUP_H_

#include <tr1/unordered_set>

#include "EventManager.h"

namespace ekam {

// A wrapper around an EventManager which keeps track of all the events being waited on
// through it and calls a callback when there is nothing left to do.  Additionally, exceptions
// thrown by callbacks are caught and reported.
//
// TODO:  Better name?
class EventGroup: public EventManager {
public:
  class ExceptionHandler {
  public:
    virtual ~ExceptionHandler();

    // An event callback threw an exception.  The ExceptionHandler is expected to immediately
    // cancel the event group such that no further events are received by it.
    virtual void threwException(const std::exception& e) = 0;
    virtual void threwUnknownException() = 0;

    // Indicates that this group completed successfully -- it is no longer waiting for anything
    // and no exception was thrown.
    virtual void noMoreEvents() = 0;
  };

  EventGroup(EventManager* inner, ExceptionHandler* exceptionHandler);
  ~EventGroup();

  // implements Executor -----------------------------------------------------------------
  OwnedPtr<PendingRunnable> runLater(OwnedPtr<Runnable> runnable);

  // implements EventManager -------------------------------------------------------------
  Promise<ProcessExitCode> onProcessExit(pid_t pid);
  OwnedPtr<IoWatcher> watchFd(int fd);
  OwnedPtr<FileWatcher> watchFile(const std::string& filename);

private:
  class PendingEvent;

  class RunnableWrapper;
  class IoWatcherWrapper;
  class FileWatcherWrapper;

  EventManager* inner;
  ExceptionHandler* exceptionHandler;
  int eventCount;
  Promise<void> pendingNoMoreEvents;

  OwnedPtr<PendingEvent> newPendingEvent();
  void callNoMoreEventsLater();
};

}  // namespace ekam

#endif  // KENTONSCODE_OS_EVENTGROUP_H_
