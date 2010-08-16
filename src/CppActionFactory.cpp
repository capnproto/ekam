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

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdexcept>
#include <tr1/unordered_set>

#include "Debug.h"
#include "FileDescriptor.h"
#include "Subprocess.h"

namespace ekam {

// compile action:  produces object file, entities for all symbols declared therein.
// link action:  triggers on "main" entity.

// Not a real anonymous namespace because of: http://gcc.gnu.org/bugzilla/show_bug.cgi?id=29365
namespace cppActionFactoryAnonNamespace {

void splitExtension(const std::string& name, std::string* base, std::string* ext) {
  std::string::size_type pos = name.find_last_of('.');
  if (pos == std::string::npos) {
    base->assign(name);
    ext->clear();
  } else {
    base->assign(name, 0, pos);
    ext->assign(name, pos, std::string::npos);
  }
}

void getDepsFile(File* objectFile, OwnedPtr<File>* output) {
  OwnedPtr<File> dir;
  objectFile->parent(&dir);
  dir->relative(objectFile->basename() + ".deps", output);
}

class Logger : public FileDescriptor::ReadAllCallback {
public:
  Logger(BuildContext* context, Subprocess* subprocess)
      : context(context), subprocess(subprocess) {}
  ~Logger() {
    subprocess->pipeDone();
  }

  // implements ReadAllCallback ----------------------------------------------------------
  void consume(const void* buffer, size_t size) {
    context->log(std::string(reinterpret_cast<const char*>(buffer), size));
  }

  void eof() {}

  void error(int number) {
    context->log("read(log pipe): " + std::string(strerror(errno)));
    context->failed();
  }

private:
  BuildContext* context;
  Subprocess* subprocess;
};

}  // anonymous cppActionFactoryAnonNamespace
using namespace cppActionFactoryAnonNamespace;

// =======================================================================================

class CppAction : public Action {
public:
  CppAction(File* file);
  ~CppAction();

  // implements Action -------------------------------------------------------------------
  std::string getVerb();
  void start(EventManager* eventManager, BuildContext* context);

private:
  class SymbolCollector;
  class CompileProcess;

  OwnedPtr<File> file;
};

CppAction::CppAction(File* file) {
  file->clone(&this->file);
}

CppAction::~CppAction() {}

std::string CppAction::getVerb() {
  return "compile";
}

#define ARRAY_SIZE(ARRAY) (sizeof(ARRAY) / sizeof(ARRAY[0]))

// ---------------------------------------------------------------------------------------

class CppAction::SymbolCollector : public FileDescriptor::ReadAllCallback {
public:
  SymbolCollector(BuildContext* context, File* objectFile, Subprocess* subprocess)
      : context(context), subprocess(subprocess) {
    objectFile->clone(&this->objectFile);
  }
  ~SymbolCollector() {
    subprocess->pipeDone();
  }

  // implements ReadAllCallback ----------------------------------------------------------
  void consume(const void* buffer, size_t size) {
    const char* data = reinterpret_cast<const char*>(buffer);
    std::string line = leftover;

    while (true) {
      const char* eol = reinterpret_cast<const char*>(memchr(data, '\n', size));
      if (eol == NULL) {
        leftover.assign(data, size);
        break;
      }
      line.append(data, eol);
      parseLine(line);
      line.clear();
      size -= eol + 1 - data;
      data = eol + 1;
    }
  }

  void eof() {
    DEBUG_INFO << "symbols done";
    context->provide(objectFile.get(), entities);

    OwnedPtr<File> depsFile;
    getDepsFile(objectFile.get(), &depsFile);

    depsFile->writeAll(deps);
  }

  void error(int number) {
    context->log("read(symbol pipe): " + std::string(strerror(errno)));
    context->failed();
  }

private:
  BuildContext* context;
  OwnedPtr<File> objectFile;
  Subprocess* subprocess;
  std::string leftover;
  std::vector<EntityId> entities;
  std::string deps;

  void parseLine(const std::string& line) {
    // Awkward:  The output of nm has lines that might look like:
    //   0000000000000160 T fooBar
    // or they might look like:
    //                    U fooBar
    // We don't care about the address, but we want the one-letter type code and the following
    // symbol name.

    // TODO:  Make this parsing code less ugly?

    // Seach for the first space.  This will be the first character if there is no address,
    // otherwise it will be the character after the address.
    std::string::size_type pos = line.find_first_of(' ');
    if (pos == std::string::npos) return;

    // Search for the first non-space after the first space.  This ought to be the position of
    // the type code.
    pos = line.find_first_not_of(' ', pos);
    if (pos == std::string::npos) return;

    // Record the type code.
    char type = line[pos];

    // Look for the first non-space after the type code.  Should be the beginning of the symbol
    // name.
    pos = line.find_first_not_of(' ', pos + 1);
    if (pos == std::string::npos) return;

    // The rest of the line is the symbol name.
    std::string symbolName(line, pos);

    // OK, interpret it.
    if (strchr("ABCDGRSTV", type) != NULL) {
      entities.push_back(EntityId::fromName("c++symbol:" + symbolName));
      DEBUG_INFO << objectFile->basename() << ": " << symbolName;
    } else if (type == 'U') {
      deps.append(symbolName);
      deps.push_back('\n');
    }
  }
};

// ---------------------------------------------------------------------------------------

class CppAction::CompileProcess : public EventManager::ProcessExitCallback {
public:
  CompileProcess(CppAction* action, EventManager* eventManager, BuildContext* context)
      : action(action), eventManager(eventManager), context(context) {}
  ~CompileProcess() {}

  void start(OwnedPtr<CompileProcess>* selfPtr) {
    std::string base, ext;
    splitExtension(action->file->basename(), &base, &ext);
    context->newOutput(base + ".o", &objectFile);

    subprocess.addArgument("./c++wrap.sh");
    subprocess.addArgument(action->file.get(), File::READ);
    subprocess.addArgument(objectFile.get(), File::WRITE);

    subprocess.captureStdout(&symbolStream);
    subprocess.captureStderr(&logStream);

    OwnedPtr<EventManager::ProcessExitCallback> callback;
    callback.adopt(selfPtr);
    subprocess.start(eventManager, &callback);

    OwnedPtr<FileDescriptor::ReadAllCallback> symbolCallback;
    symbolCallback.allocateSubclass<SymbolCollector>(context, objectFile.get(), &subprocess);
    symbolStream->readAll(eventManager, &symbolCallback);

    OwnedPtr<FileDescriptor::ReadAllCallback> loggerCallback;
    loggerCallback.allocateSubclass<Logger>(context, &subprocess);
    logStream->readAll(eventManager, &loggerCallback);
  }

  // implements ProcessExitCallback ----------------------------------------------------
  void exited(int exitCode) {
    if (exitCode == 0) {
      DEBUG_INFO << "proc done";
      context->success();
    } else {
      context->failed();
    }
  }
  void signaled(int signalNumber) {
    context->failed();
  }

private:
  CppAction* action;
  EventManager* eventManager;
  BuildContext* context;
  OwnedPtr<File> objectFile;

  Subprocess subprocess;
  OwnedPtr<FileDescriptor> symbolStream;
  OwnedPtr<FileDescriptor> logStream;
};

void CppAction::start(EventManager* eventManager, BuildContext* context) {
  OwnedPtr<CompileProcess> compileProcess;
  compileProcess.allocate(this, eventManager, context);

  compileProcess->start(&compileProcess);
}

// =======================================================================================

class LinkAction : public Action {
public:
  LinkAction(File* file);
  ~LinkAction();

  // implements Action -------------------------------------------------------------------
  std::string getVerb();
  void start(EventManager* eventManager, BuildContext* context);

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
    bool hasMissing() { return !missing.empty(); }
    void enumerate(OwnedPtrVector<File>::Appender output) {
      deps.releaseAll(output);
    }

  private:
    OwnedPtrMap<File*, File, FileHashFunc, FileEqualFunc> deps;
    std::tr1::unordered_set<std::string> missing;
  };

  OwnedPtr<File> file;
};

LinkAction::LinkAction(File* file) {
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

      // Temporary hack:  Ignore symbol names not containing "ekam" so that we don't get upset
      //   that the libc symbols aren't found.
      // TODO: Remove this.
      if (symbolName.find("ekam") == std::string::npos) {
        prevPos = pos + 1;
        pos = data.find_first_of('\n', prevPos);
        continue;
      }

      File* file = context->findProvider(EntityId::fromName("c++symbol:" + symbolName), symbolName);
      if (file == NULL) {
        missing.insert(symbolName);
        context->log("missing symbol: " + symbolName + "\n");
      } else {
        addObject(context, file);
      }

      prevPos = pos + 1;
      pos = data.find_first_of('\n', prevPos);
    }
  }
}

// ---------------------------------------------------------------------------------------

class LinkAction::LinkProcess : public EventManager::ProcessExitCallback {
public:
  LinkProcess(LinkAction* action, EventManager* eventManager, BuildContext* context,
              OwnedPtrVector<File>* depsToAdopt)
      : action(action), eventManager(eventManager), context(context) {
    deps.swap(depsToAdopt);
  }
  ~LinkProcess() {}

  void start(OwnedPtr<LinkProcess>* selfPtr) {
    // TODO:  Reuse code with CppAction.
    std::string base, ext;
    splitExtension(action->file->basename(), &base, &ext);

    subprocess.addArgument("c++");
    subprocess.addArgument("-o");

    context->newOutput(base, &executableFile);
    subprocess.addArgument(executableFile.get(), File::WRITE);

    for (int i = 0; i < deps.size(); i++) {
      subprocess.addArgument(deps.get(i), File::READ);
    }

    subprocess.captureStdout(&logStream);

    OwnedPtr<EventManager::ProcessExitCallback> callback;
    callback.adopt(selfPtr);
    subprocess.start(eventManager, &callback);

    OwnedPtr<FileDescriptor::ReadAllCallback> loggerCallback;
    loggerCallback.allocateSubclass<Logger>(context, &subprocess);
    logStream->readAll(eventManager, &loggerCallback);
  }

  // implements ProcessExitCallback ----------------------------------------------------
  void exited(int exitCode) {
    if (exitCode == 0) {
      context->success();
    } else {
      context->failed();
    }
  }
  void signaled(int signalNumber) {
    context->failed();
  }

private:
  LinkAction* action;
  EventManager* eventManager;
  BuildContext* context;

  OwnedPtrVector<File> deps;
  OwnedPtr<File> executableFile;

  Subprocess subprocess;
  OwnedPtr<FileDescriptor> logStream;
};

void LinkAction::start(EventManager* eventManager, BuildContext* context) {
  DepsSet deps;
  deps.addObject(context, file.get());

  if (deps.hasMissing()) {
    context->failed();
    return;
  }

  OwnedPtrVector<File> flatDeps;
  deps.enumerate(flatDeps.appender());

  OwnedPtr<LinkProcess> linkProcess;
  linkProcess.allocate(this, eventManager, context, &flatDeps);

  linkProcess->start(&linkProcess);
}

// =======================================================================================

const EntityId CppActionFactory::MAIN_SYMBOLS[] = {
  EntityId::fromName("c++symbol:main"),
  EntityId::fromName("c++symbol:_main")
};

CppActionFactory::CppActionFactory() {}
CppActionFactory::~CppActionFactory() {}

bool CppActionFactory::tryMakeAction(File* file, OwnedPtr<Action>* output) {
  std::string base, ext;
  splitExtension(file->basename(), &base, &ext);

  if (ext == ".cpp" || ext == ".cc" || ext == ".C") {
    output->allocateSubclass<CppAction>(file);
    return true;
  } else {
    return false;
  }
}

void CppActionFactory::enumerateTriggerEntities(
    std::back_insert_iterator<std::vector<EntityId> > iter) {
  for (unsigned int i = 0; i < (sizeof(MAIN_SYMBOLS) / sizeof(MAIN_SYMBOLS[0])); i++) {
    *iter++ = MAIN_SYMBOLS[i];
  }
}

bool CppActionFactory::tryMakeAction(const EntityId& id, File* file, OwnedPtr<Action>* output) {
  for (unsigned int i = 0; i < (sizeof(MAIN_SYMBOLS) / sizeof(MAIN_SYMBOLS[0])); i++) {
    if (id == MAIN_SYMBOLS[i]) {
      output->allocateSubclass<LinkAction>(file);
      return true;
    }
  }
  return false;
}

}  // namespace ekam
