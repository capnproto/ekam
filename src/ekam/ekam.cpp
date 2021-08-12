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

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "Driver.h"
#include "base/Debug.h"
#include "os/DiskFile.h"
#include "Action.h"
#include "SimpleDashboard.h"
#include "ConsoleDashboard.h"
#include "CppActionFactory.h"
#include "ExecPluginActionFactory.h"
#include "os/OsHandle.h"

namespace ekam {

class ExtractTypeAction : public Action {
public:
  ExtractTypeAction(File* file) : file(file->clone()) {}
  ~ExtractTypeAction() {}

  // implements Action -------------------------------------------------------------------
  bool isSilent() { return true; }
  std::string getVerb() { return "scan"; }

  Promise<void> start(EventManager* eventManager, BuildContext* context) {
    std::vector<Tag> tags;

    std::string name = file->canonicalName();

    tags.push_back(Tag::fromName("canonical:" + name));

    while (true) {
      tags.push_back(Tag::fromFile(name));

      std::string::size_type slashPos = name.find_first_of('/');
      if (slashPos == std::string::npos) {
        break;
      }

      name.erase(0, slashPos + 1);
    }

    if (file->isDirectory()) {
      tags.push_back(Tag::fromName("directory:*"));
    } else {
      std::string base, ext;
      splitExtension(name, &base, &ext);
      if (!ext.empty()) tags.push_back(Tag::fromName("filetype:" + ext));
    }

    context->provide(file.get(), tags);

    return newFulfilledPromise();
  }

private:
  OwnedPtr<File> file;
};

class ExtractTypeActionFactory : public ActionFactory {
public:
  ExtractTypeActionFactory() {}
  ~ExtractTypeActionFactory() {}

  // implements ActionFactory ------------------------------------------------------------
  void enumerateTriggerTags(std::back_insert_iterator<std::vector<Tag> > iter) {
    *iter++ = Tag::DEFAULT_TAG;
  }
  OwnedPtr<Action> tryMakeAction(const Tag& id, File* file) {
    return newOwned<ExtractTypeAction>(file);
  }
};

void usage(const char* command, FILE* out) {
  fprintf(out,
    "usage: %s [-hvc] [-j <jobcount>] [-n [<addr>]:<port>] [-l <count>]\n"
    "\n"
    "Build code with Ekam. See https://github.io/sandstorm-io/ekam for details.\n"
    "\n"
    "options:\n"
    "  -c            Run in continuous mode: when there is nothing left to build,\n"
    "                don't exit, but instead watch the source files for changes\n"
    "                and rebuild as necessary.\n"
    "  -j <jobcount> Run up to <jobcount> actions in parallel.\n"
    "  -n [<addr>]:<port>  Accept network connections on the given address/port\n"
    "                and give real-time build status and logs to anyone who\n"
    "                connects. This enables e.g. `ekam-client` and various IDE\n"
    "                plugins.\n"
    "  -l <count>    Set max number of log lines to display per action. This is\n"
    "                kept relatively short by default because it makes the build\n"
    "                output noisy, but you may need to increase it if you need\n"
    "                to see more of a particular error log. NOTE: If you just\n"
    "                need a one-off, you can use `ekam-client` rather than\n"
    "                restarting Ekam.\n"
    "  -h            See this help\n"
    "  -v            Show debug logs.\n",
    command);
}

// =======================================================================================
// TODO:  Move file-watching code to another module.

class Watcher {
public:
  Watcher(OwnedPtr<File> file, EventManager* eventManager, Driver* driver,
          bool isDirectory)
      : eventManager(eventManager), driver(driver), isDirectory(isDirectory), file(file.release()) {
    resetWatch();
  }

  virtual ~Watcher() {}

  EventManager* const eventManager;
  Driver* const driver;
  const bool isDirectory;
  OwnedPtr<File> file;

  void resetWatch() {
    asyncOp.release();
    diskRef = file->getOnDisk(File::READ);
    watcher = eventManager->watchFile(diskRef->path());
    waitForEvent();
  }

  void waitForEvent() {
    asyncOp = eventManager->when(watcher->onChange())(
      [this](EventManager::FileChangeType changeType) {
        switch (changeType) {
          case EventManager::FileChangeType::MODIFIED:
            modified();
            break;
          case EventManager::FileChangeType::DELETED:
            deleted();
            break;
        }
        waitForEvent();
      });
  }

  void clearWatch() {
    asyncOp.release();
  }

  bool isDeleted() {
    return asyncOp == nullptr;
  }

  virtual void created() = 0;
  virtual void modified() = 0;
  virtual void deleted() = 0;
  virtual void reallyDeleted() = 0;

private:
  OwnedPtr<File::DiskRef> diskRef;
  OwnedPtr<EventManager::FileWatcher> watcher;
  Promise<void> asyncOp;
};

class FileWatcher : public Watcher {
public:
  FileWatcher(OwnedPtr<File> file, EventManager* eventManager, Driver* driver)
      : Watcher(file.release(), eventManager, driver, false) {}
  ~FileWatcher() {}

  // implements FileChangeCallback -------------------------------------------------------
  void created() {
    DEBUG_INFO << "Source file created: " << file->canonicalName();
    modified();
  }
  void modified() {
    DEBUG_INFO << "Source file modified: " << file->canonicalName();

    driver->addSourceFile(file.get());
  }
  void deleted() {
    if (file->isFile()) {
      // A new file was created in place of the old.  Reset the watch.
      DEBUG_INFO << "Source file replaced: " << file->canonicalName();
      resetWatch();
      modified();
    } else {
      reallyDeleted();
    }
  }

  // implements Watcher ------------------------------------------------------------------
  void reallyDeleted() {
    DEBUG_INFO << "Source file deleted: " << file->canonicalName();

    clearWatch();
    driver->removeSourceFile(file.get());
  }
};

class DirectoryWatcher : public Watcher {
  typedef OwnedPtrMap<File*, Watcher, File::HashFunc, File::EqualFunc> ChildMap;
public:
  DirectoryWatcher(OwnedPtr<File> file, EventManager* eventManager, Driver* driver)
      : Watcher(file.release(), eventManager, driver, true) {}
  ~DirectoryWatcher() {}

  // implements FileChangeCallback -------------------------------------------------------
  void created() {
    driver->addSourceFile(file.get());
    modified();
  }
  void modified() {
    DEBUG_INFO << "Directory modified: " << file->canonicalName();

    OwnedPtrVector<File> list;
    try {
      file->list(list.appender());
    } catch (OsError& e) {
      // Probably the directory has been deleted but we weren't yet notified.
      reallyDeleted();
      return;
    }

    ChildMap newChildren;

    // Build new child list, copying over child watchers where possible.
    for (int i = 0; i < list.size(); i++) {
      OwnedPtr<File> childFile = list.release(i);

      OwnedPtr<Watcher> child;
      bool childIsDirectory = childFile->isDirectory();

      // When a file is deleted and replaced with a new one of the same type, we run into a lot
      // of awkward race conditions.  There are three things that can happen in any order:
      // 1) Notification of file deletion.
      // 2) Notification of directory change.
      // 3) New file is created.
      //
      // Here is how we handle each possible ordering:
      // 1, 2, 3)  File will not show up in directory list, so we won't transfer the watcher or
      //   create a new one.  It will be destroyed.
      // 1, 3, 2)  child->isDeleted will be true so we'll create a new watcher to replace it.
      // 2, 1, 3)  Like 1, 2, 3 except we directly call deleted() on the old child watcher from
      //   this function (see below).  We actually never receive the file deletion event from
      //   the EventManager in this case.
      // 2, 3, 1)  Same as 2, 1, 3.
      // 3, 1, 2)  File watcher notices new file already exists and simply resumes watching.
      //   Parent watcher thinks nothing happened.
      // 3, 2, 1)  Same as 3, 1, 2.
      //
      // The last two are different if a file was replaced with a directory or vice versa:
      // 3, 1, 2)  Child watcher notices replacement is a different type and so does not resume
      //   watching.  The parent notices child->isDeleted is true and replaces it.
      // 3, 2, 1)  The parent notices that child->isDirectory does not match the type of the new
      //   file, and so deletes the child watcher explicitly.
      if (!children.release(childFile.get(), &child) ||
          child->isDeleted() || child->isDirectory != childIsDirectory) {
        if (childIsDirectory) {
          child = newOwned<DirectoryWatcher>(childFile.release(), eventManager, driver);
        } else {
          child = newOwned<FileWatcher>(childFile.release(), eventManager, driver);
        }
        child->created();
      }

      File* key = child->file.get();  // cannot inline due to undefined evaluation order
      newChildren.add(key, child.release());
    }

    // Make sure remaining children have been notified of deletion before we destroy the objects.
    for (ChildMap::Iterator iter(children); iter.next();) {
      if (!iter.value()->isDeleted()) {
        iter.value()->reallyDeleted();
      }
    }

    // Swap in new children.
    children.swap(&newChildren);
  }

  void deleted() {
    if (file->isDirectory()) {
      // A new directory was created in place of the old.  Reset the watch.
      DEBUG_INFO << "Directory replaced: " << file->canonicalName();
      resetWatch();
      modified();
    } else {
      reallyDeleted();
    }
  }

  // implements Watcher ------------------------------------------------------------------
  void reallyDeleted() {
    DEBUG_INFO << "Directory deleted: " << file->canonicalName();

    clearWatch();
    driver->removeSourceFile(file.get());

    // Delete all children.
    for (ChildMap::Iterator iter(children); iter.next();) {
      if (!iter.value()->isDeleted()) {
        iter.value()->reallyDeleted();
      }
    }
    children.clear();
  }

private:
  ChildMap children;
};

// =======================================================================================

class EkamLocks final: public Driver::ActivityObserver {
public:
  EkamLocks(File* tmp)
      : mainLock("tmp/.ekam-lock",
            openLockfile(tmp->relative(".ekam-lock").get())),
        activeLock("tmp/.ekam-lock-active",
            openLockfile(tmp->relative(".ekam-lock-active").get())) {}

  bool tryTakeMainLock() {
    while (flock(mainLock.get(), LOCK_EX | LOCK_NB) < 0) {
      if (errno == EWOULDBLOCK) {
        return false;
      } else if (errno == ENOLCK) {
        // Filesystem doesn't support locking.
        fprintf(stderr, "WARNING: Filesystem doesn't support locking. Do not run two Ekam instances concurrently.\n");
        noLocking = true;
        return true;
      } else if (errno != EAGAIN) {
        throw OsError("flock(mainLock)", errno);
      }
    }

    return true;
  }

  void waitForOther() {
    wrapSyscall("flock(activeLock)", flock, activeLock.get(), LOCK_SH);

    char c = 0;
    ssize_t n = wrapSyscall("read(activeLock)", read, activeLock, &c, 1);
    if (n == 1 && c == 'p') {
      failed = false;
    } else if (n == 1 && c == 'f') {
      failed = true;
    } else {
      throw std::logic_error("lock file contents invalid");
    }

    wrapSyscall("flock(activeLock)", flock, activeLock.get(), LOCK_UN);
  }

  bool hasFailures() {
    if (running) throw std::logic_error("can't check failures while still running");
    return failed;
  }

  void startingAction() override {
    if (!running) {
      running = true;
      if (!noLocking) flock(activeLock.get(), LOCK_EX);
    }
  }

  void idle(bool hasFailures) override {
    if (running) {
      running = false;
      wrapSyscall("write(activeLock)", write, activeLock.get(), hasFailures ? "fail" : "pass", 4);
      if (!noLocking) wrapSyscall("flock(activeLock, LOCK_UN)", flock, activeLock.get(), LOCK_UN);
    }
    failed = hasFailures;
  }

private:
  OsHandle mainLock;
  OsHandle activeLock;
  bool running = false;
  bool failed = false;
  bool noLocking = false;

  static int openLockfile(File* lockfile) {
    auto diskfile = lockfile->getOnDisk(File::UPDATE);
    return wrapSyscall("open(lockfile)", open,
        diskfile->path().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
  }
};

// =======================================================================================

void scanSourceTree(File* src, Driver* driver) {
  OwnedPtrVector<File> fileQueue;

  {
    fileQueue.add(src->clone());
  }

  while (!fileQueue.empty()) {
    OwnedPtr<File> current = fileQueue.releaseBack();

    if (current->isDirectory()) {
      OwnedPtrVector<File> list;
      current->list(list.appender());
      for (int i = 0; i < list.size(); i++) {
        fileQueue.add(list.release(i));
      }
    }

    driver->addSourceFile(current.get());
  }
}

OwnedPtr<Dashboard> getDashboard(int maxDisplayedLogLines) {
  if (!isatty(STDOUT_FILENO)) {
    return newOwned<SimpleDashboard>(stdout);
  }

  // Sanity check: make sure the window size is non-zero; otherwise something
  // is messed up about the terminal, and we should assume it doesn't work
  // correctly.
  //
  // See issue #29
  struct winsize windowSize;
  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &windowSize) < 0) {
    const char *msg = strerror(errno);
    DEBUG_WARNING
      << "Error querying terminal size: " << msg << "; "
      << "falling back to simple output.";
    return newOwned<SimpleDashboard>(stdout);
  }
  if(windowSize.ws_row == 0 || windowSize.ws_col == 0) {
    DEBUG_WARNING
      << "Terminal size looks suspicious "
      << "(rows = " << windowSize.ws_row << ", columns = " << windowSize.ws_col << "); "
      << "falling back to simple output.";
    return newOwned<SimpleDashboard>(stdout);
  }
  return newOwned<ConsoleDashboard>(stdout, maxDisplayedLogLines);
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);

  int maxDisplayedLogLines = 30;
  const char* command = argv[0];
  int maxConcurrentActions = 1;
  bool continuous = false;
  std::string networkDashboardAddress;

  while (true) {
    int opt = getopt(argc, argv, "chvj:n:l:");
    if (opt == -1) break;

    switch (opt) {
      case 'v':
        DebugMessage::setLogLevel(DebugMessage::INFO);
        break;
      case 'j': {
        char* endptr;
        maxConcurrentActions = strtoul(optarg, &endptr, 10);
        if (*endptr != '\0') {
          fprintf(stderr, "Expected number after -j.\n");
          return 1;
        }
        break;
      }
      case 'h':
        usage(command, stdout);
        return 0;
      case 'c':
        continuous = true;
        break;
      case 'n':
        networkDashboardAddress = optarg;
        break;
      case 'l': {
        char* endptr;
        maxDisplayedLogLines = strtoul(optarg, &endptr, 0);
        if (*endptr != '\0') {
          fprintf(stderr, "Expected number after -l.\n");
          return 1;
        }
        break;
      }
      default:
        usage(command, stderr);
        return 1;
    }
  }

  argc -= optind;
  argv += optind;

  if (argc > 0) {
    fprintf(stderr, "%s: unknown argument -- %s\n", command, argv[0]);
    return 1;
  }

  DiskFile src("src", NULL);
  DiskFile tmp("tmp", NULL);
  DiskFile bin("bin", NULL);
  DiskFile lib("lib", NULL);
  DiskFile nodeModules("node_modules", NULL);
  File* installDirs[BuildContext::INSTALL_LOCATION_COUNT] = { &bin, &lib, &nodeModules };

  if (!tmp.isDirectory()) {
    tmp.createDirectory();
  }

  EkamLocks locks(&tmp);
  if (!locks.tryTakeMainLock()) {
    if (continuous) {
      fprintf(stderr, "ERROR: Ekam is already running in this directory.\n");
      return 1;
    } else {
      fprintf(stderr, "Another Ekam is already running in this directory.\n"
                      "Waiting for build to complete...\n");
      locks.waitForOther();
      return locks.hasFailures() ? 1 : 0;
    }
  }

  OwnedPtr<RunnableEventManager> eventManager = newPreferredEventManager();

  OwnedPtr<Dashboard> dashboard = getDashboard(maxDisplayedLogLines);
  if (!networkDashboardAddress.empty()) {
    dashboard = initNetworkDashboard(eventManager.get(), networkDashboardAddress,
                                     dashboard.release());
  }

  Driver driver(eventManager.get(), dashboard.get(), &tmp, installDirs, maxConcurrentActions,
                &locks);

  ExtractTypeActionFactory extractTypeActionFactcory;
  driver.addActionFactory(&extractTypeActionFactcory);

  CppActionFactory cppActionFactory;
  driver.addActionFactory(&cppActionFactory);

  ExecPluginActionFactory execPluginActionFactory;
  driver.addActionFactory(&execPluginActionFactory);

  OwnedPtr<DirectoryWatcher> rootWatcher;
  if (continuous) {
    rootWatcher = newOwned<DirectoryWatcher>(src.clone(), eventManager.get(), &driver);
    rootWatcher->modified();
  } else {
    scanSourceTree(&src, &driver);
  }
  eventManager->loop();

  // For debugging purposes, check for zombie processes.
  int zombieCount = 0;
  while (true) {
    int status;
    pid_t pid = wait(&status);
    if (pid < 0) {
      if (errno == ECHILD) {
        // No more children.
        break;
      } else {
        DEBUG_ERROR << "wait: " << strerror(errno);
      }
    } else {
      ++zombieCount;
    }
  }

  if (zombieCount > 0) {
    DEBUG_ERROR << "There were " << zombieCount
        << " zombie processes after the event loop stopped.";
    return 1;
  } else {
    DEBUG_INFO << "No zombie processes detected.  Hooray.";
    return locks.hasFailures() ? 1 : 0;
  }
}

}  // namespace ekam

int main(int argc, char* argv[]) {
  return ekam::main(argc, argv);
}
