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

#ifndef KENTONSCODE_OS_EPOLLEVENTMANAGER_H_
#define KENTONSCODE_OS_EPOLLEVENTMANAGER_H_

#include <sys/types.h>
#include <stdint.h>
#include <signal.h>
#include <deque>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "EventManager.h"
#include "base/OwnedPtr.h"
#include "OsHandle.h"
#include "ByteStream.h"

typedef struct pollfd PollFd;

namespace ekam {

class EpollEventManager : public RunnableEventManager {
public:
  EpollEventManager();
  ~EpollEventManager();

  // implements RunnableEventManager -----------------------------------------------------
  void loop();

  // implements Executor -----------------------------------------------------------------
  OwnedPtr<PendingRunnable> runLater(OwnedPtr<Runnable> runnable);

  // implements EventManager -------------------------------------------------------------
  Promise<ProcessExitCode> onProcessExit(pid_t pid);
  OwnedPtr<IoWatcher> watchFd(int fd);
  OwnedPtr<FileWatcher> watchFile(const std::string& filename);

private:
  class AsyncCallbackHandler;
  class IoWatcherImpl;

  class IoHandler {
  public:
    virtual ~IoHandler() noexcept(false) {}

    virtual void handle(uint32_t events) = 0;
  };

  class Epoller {
  public:
    Epoller();
    ~Epoller();

    bool handleEvent();

    class Watch {
    public:
      Watch(Epoller* epoller, OsHandle* handle, uint32_t events, IoHandler* handler);
      Watch(Epoller* epoller, int fd, uint32_t events, IoHandler* handler);
      ~Watch();

      // Add or remove events being watched.
      void addEvents(uint32_t eventsToAdd);
      void removeEvents(uint32_t eventsToRemove);

    private:
      friend class Epoller;

      Epoller* epoller;
      uint32_t events;
      uint32_t registeredEvents;
      int fd;
      std::string name;
      IoHandler* handler;

      void updateRegistration();
    };

  private:
    OsHandle epollHandle;
    int watchCount;

    std::unordered_set<Watch*> watchesNeedingUpdate;
  };

  class SignalHandler : public IoHandler {
  public:
    SignalHandler(Epoller* epoller);
    ~SignalHandler();

    Promise<ProcessExitCode> onProcessExit(pid_t pid);

    // implements IoHandler --------------------------------------------------------------
    void handle(uint32_t events);

  private:
    class ProcessExitHandler;

    ByteStream signalStream;
    Epoller::Watch watch;
    std::unordered_map<pid_t, ProcessExitHandler*> processExitHandlerMap;

    void handleProcessExit();
    void maybeStopExpecting();
  };

  class InotifyHandler : public IoHandler {
  public:
    InotifyHandler(Epoller* epoller);
    ~InotifyHandler();

    OwnedPtr<FileWatcher> watchFile(const std::string& filename);

    // implements IoHandler --------------------------------------------------------------
    void handle(uint32_t events);

  private:
    class WatchedDirectory;
    class FileWatcherImpl;

    ByteStream inotifyStream;
    Epoller::Watch watch;

    OwnedPtrMap<WatchedDirectory*, WatchedDirectory> ownedWatchDirectories;
    typedef std::unordered_map<int, WatchedDirectory*> WatchMap;
    WatchMap watchMap;
    typedef std::unordered_map<std::string, WatchedDirectory*> WatchByNameMap;
    WatchByNameMap watchByNameMap;
  };

  Epoller epoller;
  SignalHandler signalHandler;
  InotifyHandler inotifyHandler;

  std::deque<AsyncCallbackHandler*> asyncCallbacks;

  bool handleEvent();
};

}  // namespace ekam

#endif  // KENTONSCODE_OS_EPOLLEVENTMANAGER_H_
