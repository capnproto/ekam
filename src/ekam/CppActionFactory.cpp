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

#include "CppActionFactory.h"

#include <unordered_set>
#include <stdlib.h>

#include "base/Debug.h"
#include "os/ByteStream.h"
#include "os/Subprocess.h"
#include "ActionUtil.h"

namespace ekam {

// compile action:  produces object file, tags for all symbols declared therein.
//   (Compile action is now implemented in compile.ekam-rule.)
// link action:  triggers on "main" tag.

namespace {

OwnedPtr<File> getDepsFile(File* objectFile) {
  return objectFile->parent()->relative(objectFile->basename() + ".deps");
}

bool isTestName(const std::string& name) {
  std::string::size_type pos = name.find_last_of("_-");
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
    GTEST,
    KJTEST,
    NODEJS
  };

  LinkAction(File* file, Mode mode);
  ~LinkAction();

  // implements Action -------------------------------------------------------------------
  std::string getVerb();
  Promise<void> start(EventManager* eventManager, BuildContext* context);

private:
  class DepsSet {
  public:
    DepsSet() {}
    ~DepsSet() {}

    void addObject(BuildContext* context, File* objectFile);
    void enumerate(OwnedPtrVector<File>::Appender output) {
      deps.releaseAll(output);
    }

  private:
    OwnedPtrMap<File*, File, File::HashFunc, File::EqualFunc> deps;
  };

  static const Tag GTEST_MAIN;
  static const Tag KJTEST_MAIN;
  static const Tag TEST_EXECUTABLE;

  OwnedPtr<File> file;
  Mode mode;

  Promise<void> startTarget(EventManager* eventManager, BuildContext* context,
                            const std::string& base, OwnedPtrVector<File>& flatDeps,
                            const std::string& target);
};

const Tag LinkAction::GTEST_MAIN = Tag::fromName("gtest:main");
const Tag LinkAction::KJTEST_MAIN = Tag::fromName("kjtest:main");
const Tag LinkAction::TEST_EXECUTABLE = Tag::fromName("test:executable");

LinkAction::LinkAction(File* file, Mode mode) : file(file->clone()), mode(mode) {}

LinkAction::~LinkAction() {}

std::string LinkAction::getVerb() {
  return "link";
}

void LinkAction::DepsSet::addObject(BuildContext* context, File* objectFile) {
  if (deps.contains(objectFile)) {
    return;
  }

  OwnedPtr<File> ptr = objectFile->clone();
  File* rawptr = ptr.get();  // cannot inline due to undefined evaluation order
  deps.add(rawptr, ptr.release());

  OwnedPtr<File> depsFile = getDepsFile(objectFile);
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

Promise<void> LinkAction::start(EventManager* eventManager, BuildContext* context) {
  DepsSet deps;

  if (mode == GTEST) {
    File* gtestMain = context->findProvider(GTEST_MAIN);
    if (gtestMain == NULL) {
      context->log("Cannot find gtest_main.o.");
      context->failed();
      return newFulfilledPromise();
    }

    deps.addObject(context, gtestMain);
  } else if (mode == KJTEST) {
    File* kjtestMain = context->findProvider(KJTEST_MAIN);
    if (kjtestMain == NULL) {
      context->log("Cannot find kj/test.o.");
      context->failed();
      return newFulfilledPromise();
    }

    deps.addObject(context, kjtestMain);
  }

  deps.addObject(context, file.get());

  OwnedPtrVector<File> flatDeps;
  deps.enumerate(flatDeps.appender());

  std::string base, ext;
  splitExtension(file->canonicalName(), &base, &ext);

  if (mode == NODEJS) {
    base += ".node";
  }

  auto promise = startTarget(eventManager, context, base, flatDeps, "");

  const char* targets = getenv("CROSS_TARGETS");
  if (targets != NULL) {
    auto addTarget = [&](std::string target) {
      OwnedPtrVector<File> targetDeps;
      for (int i = 0; i < flatDeps.size(); i++) {
        std::string name, ext;
        splitExtension(flatDeps.get(i)->basename(), &name, &ext);
        targetDeps.add(flatDeps.get(i)->parent()->relative(name + '.' + target + ext));
      }

      promise = eventManager->when(std::move(promise))(
          [this, target = std::move(target), targetDeps = std::move(targetDeps),
           eventManager, context, base](Void) mutable {
        return startTarget(eventManager, context, base, targetDeps, target);
      });
    };

    while (const char* spacepos = strchr(targets, ' ')) {
      addTarget(std::string(targets, spacepos));
      targets = spacepos + 1;
    }
    addTarget(targets);
  }

  return promise;
}

Promise<void> LinkAction::startTarget(
    EventManager* eventManager, BuildContext* context,
    const std::string& base, OwnedPtrVector<File>& flatDeps,
    const std::string& target) {
  const char* cxx = getenv("CXX");

  auto subprocess = newOwned<Subprocess>();

  std::string compiler = cxx == NULL ? "c++" : cxx;

  auto slashpos = compiler.find_last_of('/');
  std::string compilerName = slashpos < 0 ? compiler : compiler.substr(slashpos + 1);

  if (target.empty()) {
    subprocess->addArgument(std::move(compiler));
  } else if (strstr(compilerName.c_str(), "clang")) {
    subprocess->addArgument(std::move(compiler));
    subprocess->addArgument("-target");
    subprocess->addArgument(target);
  } else {
    subprocess->addArgument(target + '-' + compiler);
  }

  if (mode == NODEJS) {
    subprocess->addArgument("-shared");
  } else if (context->findProvider(Tag::fromName("canonical:" + base + ".link-static")) !=
             nullptr) {
    subprocess->addArgument("-static");
  }

  subprocess->addArgument("-o");

  auto executableFile = context->newOutput(target.empty() ? base : (base + "." + target));
  subprocess->addArgument(executableFile.get(), File::WRITE);

  if (isTestName(base)) {
    std::vector<Tag> tags;
    tags.push_back(TEST_EXECUTABLE);
    context->provide(executableFile.get(), tags);
  }

  for (int i = 0; i < flatDeps.size(); i++) {
    subprocess->addArgument(flatDeps.get(i), File::READ);
  }

  const char* libs = nullptr;
  if (!target.empty()) {
    // Look for arch-specific libs.
    std::string targetLibsName = "LIBS_" + target;
    for (char& c: targetLibsName) {
      if (c == '-') c = '_';
    }
    libs = getenv(targetLibsName.c_str());
  }

  if (libs == nullptr) {
    libs = getenv("LIBS");
  }

  if (libs != NULL) {
    while (const char* spacepos = strchr(libs, ' ')) {
      subprocess->addArgument(std::string(libs, spacepos));
      libs = spacepos + 1;
    }
    subprocess->addArgument(libs);
  }

  auto logStream = subprocess->captureStdoutAndStderr();

  auto subprocessWaitOp = eventManager->when(subprocess->start(eventManager))(
    [context](ProcessExitCode exitCode) {
      if (exitCode.wasSignaled() || exitCode.getExitCode() != 0) {
        context->failed();
      }
    });

  auto logger = newOwned<Logger>(context, logStream.release());
  auto logOp = logger->run(eventManager);

  return eventManager->when(subprocessWaitOp, logOp, logger, subprocess, executableFile)(
      [](Void, Void, OwnedPtr<Logger>, OwnedPtr<Subprocess>, OwnedPtr<File>){});
}

// =======================================================================================

const Tag CppActionFactory::MAIN_SYMBOLS[] = {
  Tag::fromName("c++symbol:main"),
  Tag::fromName("c++symbol:_main")
};

const Tag CppActionFactory::GTEST_TEST = Tag::fromName("gtest:test");
const Tag CppActionFactory::KJTEST_TEST = Tag::fromName("kjtest:test");
const Tag CppActionFactory::NODEJS_MODULE = Tag::fromName("nodejs:module");

CppActionFactory::CppActionFactory() {}
CppActionFactory::~CppActionFactory() {}

void CppActionFactory::enumerateTriggerTags(
    std::back_insert_iterator<std::vector<Tag> > iter) {
  for (unsigned int i = 0; i < (sizeof(MAIN_SYMBOLS) / sizeof(MAIN_SYMBOLS[0])); i++) {
    *iter++ = MAIN_SYMBOLS[i];
  }
  *iter++ = GTEST_TEST;
  *iter++ = KJTEST_TEST;
  *iter++ = NODEJS_MODULE;
}

OwnedPtr<Action> CppActionFactory::tryMakeAction(const Tag& id, File* file) {
  OwnedPtr<Action> result;
  for (unsigned int i = 0; i < (sizeof(MAIN_SYMBOLS) / sizeof(MAIN_SYMBOLS[0])); i++) {
    if (id == MAIN_SYMBOLS[i]) {
      return newOwned<LinkAction>(file, LinkAction::NORMAL);
    }
  }
  if (id == GTEST_TEST) {
    return newOwned<LinkAction>(file, LinkAction::GTEST);
  }
  if (id == KJTEST_TEST) {
    return newOwned<LinkAction>(file, LinkAction::KJTEST);
  }
  if (id == NODEJS_MODULE) {
    return newOwned<LinkAction>(file, LinkAction::NODEJS);
  }
  return nullptr;
}

}  // namespace ekam
