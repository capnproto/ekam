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

#include "CppActionFactory.h"

#include <tr1/unordered_set>
#include <stdlib.h>

#include "Debug.h"
#include "FileDescriptor.h"
#include "Subprocess.h"
#include "ActionUtil.h"

namespace ekam {

// compile action:  produces object file, tags for all symbols declared therein.
//   (Compile action is now implemented in compile.ekam-rule.)
// link action:  triggers on "main" tag.

namespace {

void getDepsFile(File* objectFile, OwnedPtr<File>* output) {
  OwnedPtr<File> dir;
  objectFile->parent(&dir);
  dir->relative(objectFile->basename() + ".deps", output);
}

bool isTestName(const std::string& name) {
  std::string::size_type pos = name.find_last_of('_');
  if (pos == std::string::npos) {
    return false;
  } else {
    std::string suffix = name.substr(pos + 1);
    return suffix == "test" || suffix == "unittest" || suffix == "regtest";
  }
}

}  // namespace

class LinkAction : public Action {
public:
  enum Mode {
    NORMAL,
    GTEST
  };

  LinkAction(File* file, Mode mode);
  ~LinkAction();

  // implements Action -------------------------------------------------------------------
  std::string getVerb();
  void start(EventManager* eventManager, BuildContext* context, OwnedPtr<AsyncOperation>* output);

private:
  class LinkProcess;

  struct FileHashFunc {
    inline bool operator()(File* file) const {
      return std::tr1::hash<std::string>()(file->basename());
    }
  };
  struct FileEqualFunc {
    inline bool operator()(File* a, File* b) const {
      return a->equals(b);
    }
  };

  class DepsSet {
  public:
    DepsSet() {}
    ~DepsSet() {}

    void addObject(BuildContext* context, File* objectFile);
    void enumerate(OwnedPtrVector<File>::Appender output) {
      deps.releaseAll(output);
    }

  private:
    OwnedPtrMap<File*, File, FileHashFunc, FileEqualFunc> deps;
  };

  static const Tag GTEST_MAIN;
  static const Tag TEST_EXECUTABLE;

  OwnedPtr<File> file;
  Mode mode;
};

const Tag LinkAction::GTEST_MAIN = Tag::fromName("gtest:main");
const Tag LinkAction::TEST_EXECUTABLE = Tag::fromName("test:executable");

LinkAction::LinkAction(File* file, Mode mode) : mode(mode) {
  file->clone(&this->file);
}

LinkAction::~LinkAction() {}

std::string LinkAction::getVerb() {
  return "link";
}

void LinkAction::DepsSet::addObject(BuildContext* context, File* objectFile) {
  if (deps.contains(objectFile)) {
    return;
  }

  OwnedPtr<File> ptr;
  objectFile->clone(&ptr);
  deps.adopt(ptr.get(), &ptr);

  OwnedPtr<File> depsFile;
  getDepsFile(objectFile, &depsFile);
  if (depsFile->exists()) {
    std::string data = depsFile->readAll();

    std::string::size_type prevPos = 0;
    std::string::size_type pos = data.find_first_of('\n');

    while (pos != std::string::npos) {
      std::string symbolName(data, prevPos, pos - prevPos);

      File* file = context->findProvider(Tag::fromName("c++symbol:" + symbolName));
      if (file != NULL) {
        addObject(context, file);
      }

      prevPos = pos + 1;
      pos = data.find_first_of('\n', prevPos);
    }
  }
}

// ---------------------------------------------------------------------------------------

class LinkAction::LinkProcess : public AsyncOperation, public EventManager::ProcessExitCallback {
public:
  LinkProcess(LinkAction* action, EventManager* eventManager, BuildContext* context,
              OwnedPtrVector<File>* depsToAdopt)
      : action(action), context(context) {
    deps.swap(depsToAdopt);

    std::string base, ext;
    splitExtension(action->file->canonicalName(), &base, &ext);

    const char* cxx = getenv("CXX");

    subprocess.addArgument(cxx == NULL ? "c++" : cxx);
    subprocess.addArgument("-o");

    context->newOutput(base, &executableFile);
    subprocess.addArgument(executableFile.get(), File::WRITE);

    if (isTestName(base)) {
      std::vector<Tag> tags;
      tags.push_back(TEST_EXECUTABLE);
      context->provide(executableFile.get(), tags);
    }

    for (int i = 0; i < deps.size(); i++) {
      subprocess.addArgument(deps.get(i), File::READ);
    }

    const char* libs = getenv("LIBS");
    if (libs != NULL) {
      while (const char* spacepos = strchr(libs, ' ')) {
        subprocess.addArgument(std::string(libs, spacepos));
        libs = spacepos + 1;
      }
      subprocess.addArgument(libs);
    }

    subprocess.captureStdoutAndStderr(&logStream);

    subprocess.start(eventManager, this);

    logger.allocate(context);
    logStream->readAll(eventManager, logger.get(), &logOp);
  }
  ~LinkProcess() {}

  // implements ProcessExitCallback ----------------------------------------------------
  void exited(int exitCode) {
    if (exitCode != 0) {
      context->failed();
    }
  }
  void signaled(int signalNumber) {
    context->failed();
  }

private:
  LinkAction* action;
  BuildContext* context;

  OwnedPtrVector<File> deps;
  OwnedPtr<File> executableFile;

  Subprocess subprocess;
  OwnedPtr<FileDescriptor> logStream;
  OwnedPtr<Logger> logger;
  OwnedPtr<AsyncOperation> logOp;
};

void LinkAction::start(EventManager* eventManager, BuildContext* context,
                       OwnedPtr<AsyncOperation>* output) {
  DepsSet deps;

  if (mode == GTEST) {
    File* gtestMain = context->findProvider(GTEST_MAIN);
    if (gtestMain == NULL) {
      context->log("Cannot find gtest_main.o.");
      context->failed();
      return;
    }

    deps.addObject(context, gtestMain);
  }

  deps.addObject(context, file.get());

  OwnedPtrVector<File> flatDeps;
  deps.enumerate(flatDeps.appender());

  output->allocateSubclass<LinkProcess>(this, eventManager, context, &flatDeps);
}

// =======================================================================================

const Tag CppActionFactory::MAIN_SYMBOLS[] = {
  Tag::fromName("c++symbol:main"),
  Tag::fromName("c++symbol:_main")
};

const Tag CppActionFactory::GTEST_TEST = Tag::fromName("gtest:test");

CppActionFactory::CppActionFactory() {}
CppActionFactory::~CppActionFactory() {}

void CppActionFactory::enumerateTriggerTags(
    std::back_insert_iterator<std::vector<Tag> > iter) {
  for (unsigned int i = 0; i < (sizeof(MAIN_SYMBOLS) / sizeof(MAIN_SYMBOLS[0])); i++) {
    *iter++ = MAIN_SYMBOLS[i];
  }
  *iter++ = GTEST_TEST;
}

bool CppActionFactory::tryMakeAction(const Tag& id, File* file, OwnedPtr<Action>* output) {
  for (unsigned int i = 0; i < (sizeof(MAIN_SYMBOLS) / sizeof(MAIN_SYMBOLS[0])); i++) {
    if (id == MAIN_SYMBOLS[i]) {
      output->allocateSubclass<LinkAction>(file, LinkAction::NORMAL);
      return true;
    }
  }
  if (id == GTEST_TEST) {
    output->allocateSubclass<LinkAction>(file, LinkAction::GTEST);
    return true;
  }
  return false;
}

}  // namespace ekam
