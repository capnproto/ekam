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

#include "EventGroup.h"

#include "base/Debug.h"

namespace ekam {

EventGroup::ExceptionHandler::~ExceptionHandler() noexcept(false) {}

class EventGroup::PendingEvent {
public:
  PendingEvent(EventGroup* group): group(group) {
    ++group->eventCount;
  }
  ~PendingEvent() {
    if (--group->eventCount == 0) {
      group->callNoMoreEventsLater();
    }
  }

private:
  EventGroup* group;
};


#define HANDLE_EXCEPTIONS(STATEMENT)                             \
  EventGroup* group = this->group;                               \
  try {                                                          \
    STATEMENT;                                                   \
  } catch (const std::exception& exception) {                    \
    group->exceptionHandler->threwException(exception);          \
  } catch (...) {                                                \
    group->exceptionHandler->threwUnknownException();            \
  }

class EventGroup::RunnableWrapper : public Runnable {
public:
  RunnableWrapper(EventGroup* group, OwnedPtr<Runnable> wrapped)
      : group(group), pendingEvent(group), wrapped(wrapped.release()) {}
  ~RunnableWrapper() {}

  // implements Runnable -----------------------------------------------------------------
  void run() { HANDLE_EXCEPTIONS(wrapped->run()); }

private:
  EventGroup* group;
  PendingEvent pendingEvent;
  OwnedPtr<Runnable> wrapped;
};

#undef HANDLE_EXCEPTIONS

// =======================================================================================

EventGroup::EventGroup(EventManager* inner, ExceptionHandler* exceptionHandler)
    : inner(inner), exceptionHandler(exceptionHandler), eventCount(0) {}

EventGroup::~EventGroup() {}

OwnedPtr<PendingRunnable> EventGroup::runLater(OwnedPtr<Runnable> runnable) {
  auto wrappedCallback = newOwned<RunnableWrapper>(this, runnable.release());
  return inner->runLater(wrappedCallback.release());
}

Promise<ProcessExitCode> EventGroup::onProcessExit(pid_t pid) {
  Promise<ProcessExitCode> innerPromise = inner->onProcessExit(pid);
  return when(innerPromise, newPendingEvent())(
    [this](ProcessExitCode exitCode, OwnedPtr<PendingEvent>) -> ProcessExitCode {
      return exitCode;
    });
}

class EventGroup::IoWatcherWrapper: public EventManager::IoWatcher {
public:
  IoWatcherWrapper(EventGroup* group, OwnedPtr<IoWatcher> inner)
      : group(group), inner(inner.release()) {}
  ~IoWatcherWrapper() {}

  // implements IoWatcher ----------------------------------------------------------------
  Promise<void> onReadable() {
    Promise<void> innerPromise = inner->onReadable();
    return group->when(innerPromise, group->newPendingEvent())(
      [this](Void, OwnedPtr<PendingEvent>) {
        // Let PendingEvent die.
      });
  }
  Promise<void> onWritable() {
    Promise<void> innerPromise = inner->onWritable();
    return group->when(innerPromise, group->newPendingEvent())(
      [this](Void, OwnedPtr<PendingEvent>) {
        // Let PendingEvent die.
      });
  }

private:
  EventGroup* group;
  OwnedPtr<IoWatcher> inner;
};

OwnedPtr<EventManager::IoWatcher> EventGroup::watchFd(int fd) {
  return newOwned<IoWatcherWrapper>(this, inner->watchFd(fd));
}

class EventGroup::FileWatcherWrapper: public EventManager::FileWatcher {
public:
  FileWatcherWrapper(EventGroup* group, OwnedPtr<FileWatcher> inner)
      : group(group), inner(inner.release()) {}
  ~FileWatcherWrapper() {}

  // implements IoWatcher ----------------------------------------------------------------
  Promise<FileChangeType> onChange() {
    Promise<FileChangeType> innerPromise = inner->onChange();
    return group->when(innerPromise, group->newPendingEvent())(
      [this](FileChangeType changeType, OwnedPtr<PendingEvent>) -> FileChangeType {
        // Let PendingEvent die.
        return changeType;
      });
  }

private:
  EventGroup* group;
  OwnedPtr<FileWatcher> inner;
};

OwnedPtr<EventManager::FileWatcher> EventGroup::watchFile(const std::string& filename) {
  return newOwned<FileWatcherWrapper>(this, inner->watchFile(filename));
}

OwnedPtr<EventGroup::PendingEvent> EventGroup::newPendingEvent() {
  return newOwned<PendingEvent>(this);
}

void EventGroup::callNoMoreEventsLater() {
  pendingNoMoreEvents = inner->when()(
    [this]() {
      pendingNoMoreEvents.release();
      if (eventCount == 0) {
        DEBUG_INFO << "No more events on EventGroup.";
        exceptionHandler->noMoreEvents();
      }
    });
}

}  // namespace ekam
