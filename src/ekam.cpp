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

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "Driver.h"
#include "Debug.h"
#include "DiskFile.h"
#include "Action.h"
#include "SimpleDashboard.h"
#include "ConsoleDashboard.h"
#include "CppActionFactory.h"
#include "ExecPluginActionFactory.h"
#include "OsHandle.h"

namespace ekam {

class ExtractTypeAction : public Action {
public:
  ExtractTypeAction(File* file) {
    file->clone(&this->file);
  }
  ~ExtractTypeAction() {}

  // implements Action -------------------------------------------------------------------
  bool isSilent() { return true; }
  std::string getVerb() { return "scan"; }

  void start(EventManager* eventManager, BuildContext* context,
             OwnedPtr<AsyncOperation>* output) {
    std::vector<Tag> tags;

    std::string name = file->canonicalName();

    while (true) {
      tags.push_back(Tag::fromFile(name));

      std::string::size_type slashPos = name.find_first_of('/');
      if (slashPos == std::string::npos) {
        break;
      }

      name.erase(0, slashPos + 1);
    }


    std::string base, ext;
    splitExtension(name, &base, &ext);
    if (!ext.empty()) tags.push_back(Tag::fromName("filetype:" + ext));

    context->provide(file.get(), tags);
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
  bool tryMakeAction(const Tag& id, File* file, OwnedPtr<Action>* output) {
    output->allocateSubclass<ExtractTypeAction>(file);
    return true;
  }
};

void usage(const char* command, FILE* out) {
  fprintf(out,
    "usage: %s [-hvc] [-j jobcount]\n", command);
}

// =======================================================================================
// TODO:  Move file-watching code to another module.

class Watcher : public EventManager::FileChangeCallback {
public:
  Watcher(OwnedPtr<File>* fileToAdopt, EventManager* eventManager, Driver* driver,
          bool isDirectory)
      : eventManager(eventManager), driver(driver), isDirectory(isDirectory) {
    file.adopt(fileToAdopt);
    resetWatch();
  }

  EventManager* const eventManager;
  Driver* const driver;
  const bool isDirectory;
  OwnedPtr<File> file;

  void resetWatch() {
    asyncOp.clear();
    OwnedPtr<File::DiskRef> diskRef;
    file->getOnDisk(File::READ, &diskRef);
    eventManager->onFileChange(diskRef->path(), this, &asyncOp);
  }

  void clearWatch() {
    asyncOp.clear();
  }

  bool isDeleted() {
    return asyncOp == NULL;
  }

  virtual void reallyDeleted() = 0;

private:
  OwnedPtr<AsyncOperation> asyncOp;
};

class FileWatcher : public Watcher {
public:
  FileWatcher(OwnedPtr<File>* fileToAdopt, EventManager* eventManager, Driver* driver)
      : Watcher(fileToAdopt, eventManager, driver, false) {}
  ~FileWatcher() {}

  // implements FileChangeCallback -------------------------------------------------------
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

private:
  OwnedPtr<AsyncOperation> asyncOp;
};

class DirectoryWatcher : public Watcher {
  typedef OwnedPtrMap<File*, Watcher, File::HashFunc, File::EqualFunc> ChildMap;
public:
  DirectoryWatcher(OwnedPtr<File>* fileToAdopt, EventManager* eventManager, Driver* driver)
      : Watcher(fileToAdopt, eventManager, driver, true) {}
  ~DirectoryWatcher() {}

  // implements FileChangeCallback -------------------------------------------------------
  void modified() {
    DEBUG_INFO << "Directory modified: " << file->canonicalName();

    OwnedPtrVector<File> list;
    try {
      file->list(list.appender());
    } catch (OsError e) {
      // Probably the directory has been deleted but we weren't yet notified.
      reallyDeleted();
      return;
    }

    ChildMap newChildren;

    // Build new child list, copying over child watchers where possible.
    for (int i = 0; i < list.size(); i++) {
      OwnedPtr<File> childFile;
      list.release(i, &childFile);

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
          child.allocateSubclass<DirectoryWatcher>(&childFile, eventManager, driver);
        } else {
          child.allocateSubclass<FileWatcher>(&childFile, eventManager, driver);
        }
        child->modified();
      }

      newChildren.adopt(child->file.get(), &child);
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

void scanSourceTree(File* src, Driver* driver) {
  OwnedPtrVector<File> fileQueue;

  {
    OwnedPtr<File> root;
    src->clone(&root);
    fileQueue.adoptBack(&root);
  }

  while (!fileQueue.empty()) {
    OwnedPtr<File> current;
    fileQueue.releaseBack(&current);

    if (current->isDirectory()) {
      OwnedPtrVector<File> list;
      current->list(list.appender());
      for (int i = 0; i < list.size(); i++) {
        OwnedPtr<File> child;
        list.release(i, &child);
        fileQueue.adoptBack(&child);
      }
    } else {
      driver->addSourceFile(current.get());
    }
  }
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);

  const char* command = argv[0];
  int maxConcurrentActions = 1;
  bool continuous = false;
  std::string networkDashboardAddress;

  while (true) {
    int opt = getopt(argc, argv, "chvj:n:");
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
  File* installDirs[BuildContext::INSTALL_LOCATION_COUNT] = { &bin, &lib };

  OwnedPtr<RunnableEventManager> eventManager;
  newPreferredEventManager(&eventManager);

  OwnedPtr<Dashboard> dashboard;
  if (isatty(STDOUT_FILENO)) {
    dashboard.allocateSubclass<ConsoleDashboard>(stdout);
  } else {
    dashboard.allocateSubclass<SimpleDashboard>(stdout);
  }
  if (!networkDashboardAddress.empty()) {
    initNetworkDashboard(eventManager.get(), networkDashboardAddress, &dashboard);
  }

  Driver driver(eventManager.get(), dashboard.get(), &tmp, installDirs, maxConcurrentActions);

  ExtractTypeActionFactory extractTypeActionFactcory;
  driver.addActionFactory(&extractTypeActionFactcory);

  CppActionFactory cppActionFactory;
  driver.addActionFactory(&cppActionFactory);

  ExecPluginActionFactory execPluginActionFactory;
  driver.addActionFactory(&execPluginActionFactory);

  OwnedPtr<DirectoryWatcher> rootWatcher;
  if (continuous) {
    OwnedPtr<File> srcCopy;
    src.clone(&srcCopy);
    rootWatcher.allocate(&srcCopy, eventManager.get(), &driver);
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
    return 0;
  }
}

}  // namespace ekam

int main(int argc, char* argv[]) {
  return ekam::main(argc, argv);
}
