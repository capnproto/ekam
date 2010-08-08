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
#include <errno.h>
#include <fcntl.h>
#include <stdexcept>
#include <tr1/unordered_set>
#include "Debug.h"

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

class Pipe {
public:
  Pipe() {
    if (pipe(fds) != 0) {
      // TODO:  Better exception.
      throw std::runtime_error(std::string("pipe: ") + strerror(errno));
    }
  }

  ~Pipe() {
    // TODO:  Check close() errors?
    closeReadEnd();
    closeWriteEnd();
  }

  int readEnd() {
    closeWriteEnd();
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    return fds[0];
  }

  int writeEnd() {
    closeReadEnd();
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return fds[1];
  }

  void attachReadEnd(int target) {
    dup2(readEnd(), target);
    close(fds[1]);
    fds[1] = -1;
  }

  void attachWriteEnd(int target) {
    dup2(writeEnd(), target);
    close(fds[0]);
    fds[0] = -1;
  }

private:
  int fds[2];

  void closeReadEnd() {
    if (fds[0] != -1) {
      if (close(fds[0]) != 0) {
        DEBUG_ERROR << "close(pipe): " << strerror(errno);
      }
      fds[0] = -1;
    }
  }

  void closeWriteEnd() {
    if (fds[1] != -1) {
      if (close(fds[1]) != 0) {
        DEBUG_ERROR << "close(pipe): " << strerror(errno);
      }
      fds[1] = -1;
    }
  }
};

class DoneBarrier {
public:
  DoneBarrier(BuildContext* context, int count)
      : context(context), count(count), successCount(0), releaseCount(0) {}
  ~DoneBarrier() {}

  void success() {
    DEBUG_INFO << "successCount = " << successCount;
    if (++successCount == count) {
      context->success();
    }
  }

  void release() {
    if (++releaseCount == count) {
      delete this;
    }
  }

private:
  BuildContext* context;
  int count;
  int successCount;
  int releaseCount;
};

class Logger : public EventManager::ContinuousReadCallback {
public:
  Logger(BuildContext* context, DoneBarrier* doneBarrier)
      : context(context), doneBarrier(doneBarrier) {}
  ~Logger() {
    doneBarrier->release();
  }

  // implements ContinuousReadCallback ---------------------------------------------------
  void data(const void* buffer, int size) {
    context->log(std::string(reinterpret_cast<const char*>(buffer), size));
  }
  void eof() {
    DEBUG_INFO << "logger done";
    doneBarrier->success();
  }
  void error(int number) {
    context->log(std::string("read(log pipe): ") + strerror(number));
    context->failed();
  }

private:
  BuildContext* context;
  DoneBarrier* doneBarrier;
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

class CppAction::SymbolCollector : public EventManager::ContinuousReadCallback {
public:
  SymbolCollector(BuildContext* context, File* objectFile, DoneBarrier* doneBarrier)
      : context(context), doneBarrier(doneBarrier) {
    objectFile->clone(&this->objectFile);
  }
  ~SymbolCollector() {
    doneBarrier->release();
  }

  // implements ContinuousReadCallback ---------------------------------------------------
  void data(const void* buffer, int size) {
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

    doneBarrier->success();
  }
  void error(int number) {
    DEBUG_INFO << "symbol pipe broken";
    context->log(std::string("read(symbol pipe): ") + strerror(number));
    context->failed();
  }

private:
  BuildContext* context;
  OwnedPtr<File> objectFile;
  DoneBarrier* doneBarrier;
  std::string leftover;
  std::vector<EntityId> entities;
  std::string deps;

  void parseLine(const std::string& line) {
    std::string::size_type pos = line.find_first_of(' ');
    if (pos != std::string::npos && pos + 1 < line.size()) {
      std::string symbolName(line, 0, pos);
      char type = line[pos + 1];
      if (strchr("ABCDGRSTV", type) != NULL) {
        entities.push_back(EntityId::fromName("c++symbol:" + symbolName));
        DEBUG_INFO << objectFile->basename() << ": " << symbolName;
      } else if (type == 'U') {
        deps.append(symbolName);
        deps.push_back('\n');
      }
    }
  }
};

// ---------------------------------------------------------------------------------------

class CppAction::CompileProcess : public EventManager::ProcessExitCallback {
public:
  CompileProcess(CppAction* action, EventManager* eventManager, BuildContext* context)
      : action(action), eventManager(eventManager), context(context),
        doneBarrier(new DoneBarrier(context, 3)) {}
  ~CompileProcess() {
    doneBarrier->release();
  }

  void start(OwnedPtr<CompileProcess>* selfPtr) {
    std::string base, ext;
    splitExtension(action->file->basename(), &base, &ext);

    context->newOutput(base + ".o", &objectFile);
    action->file->getOnDisk(&sourceRef);
    objectFile->getOnDisk(&objectRef);

    std::string sourcePath = sourceRef->path();
    std::string objectPath = objectRef->path();

    symbolPipe.allocate();
    logPipe.allocate();

    pid_t pid = fork();

    if (pid < 0) {
      context->log(std::string("fork: ") + strerror(errno));
      context->failed();
    } else if (pid == 0) {
      // In child.
      symbolPipe->attachWriteEnd(STDOUT_FILENO);
      logPipe->attachWriteEnd(STDERR_FILENO);

      const char* argv[] = {
        "./c++wrap.sh", sourcePath.c_str(), objectPath.c_str(), NULL
      };
      char* mutableArgv[ARRAY_SIZE(argv)];

      std::string command;
      for (unsigned int i = 0; i < ARRAY_SIZE(argv); i++) {
        if (argv[i] == NULL) {
          mutableArgv[i] = NULL;
        } else {
          mutableArgv[i] = strdup(argv[i]);
          if (!command.empty()) command.push_back(' ');
          command.append(argv[i]);
        }
      }

      DEBUG_INFO << "exec: " << command;

      execvp(argv[0], mutableArgv);

      perror("execvp");
      exit(1);
    } else {
      OwnedPtr<EventManager::ContinuousReadCallback> symbolCallback;
      symbolCallback.allocateSubclass<SymbolCollector>(context, objectFile.get(), doneBarrier);
      eventManager->readContinuously(symbolPipe->readEnd(), &symbolCallback);

      OwnedPtr<EventManager::ContinuousReadCallback> loggerCallback;
      loggerCallback.allocateSubclass<Logger>(context, doneBarrier);
      eventManager->readContinuously(logPipe->readEnd(), &loggerCallback);

      OwnedPtr<EventManager::ProcessExitCallback> callback;
      callback.adopt(selfPtr);
      eventManager->waitPid(pid, &callback);
    }
  }

  // implements ProcessExitCallback ----------------------------------------------------
  void exited(int exitCode) {
    if (exitCode == 0) {
      DEBUG_INFO << "proc done";
      doneBarrier->success();
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
  DoneBarrier* doneBarrier;
  OwnedPtr<File> objectFile;
  OwnedPtr<File::DiskRef> sourceRef;
  OwnedPtr<File::DiskRef> objectRef;
  OwnedPtr<Pipe> symbolPipe;
  OwnedPtr<Pipe> logPipe;
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
      : action(action), eventManager(eventManager), context(context),
        doneBarrier(new DoneBarrier(context, 2)) {
    deps.swap(depsToAdopt);
  }
  ~LinkProcess() {
    doneBarrier->release();
  }

  void start(OwnedPtr<LinkProcess>* selfPtr) {
    // TODO:  Reuse code with CppAction.
    std::string base, ext;
    splitExtension(action->file->basename(), &base, &ext);

    context->newOutput(base, &executableFile);
    executableFile->getOnDisk(&executableRef);
    std::string executablePath = executableRef->path();

    std::vector<std::string> depsPaths;

    for (int i = 0; i < deps.size(); i++) {
      OwnedPtr<File::DiskRef> ref;
      deps.get(i)->getOnDisk(&ref);
      depsPaths.push_back(ref->path());
      depsRefs.adoptBack(&ref);
    }

    logPipe.allocate();

    pid_t pid = fork();

    if (pid < 0) {
      context->log(std::string("fork: ") + strerror(errno));
      context->failed();
    } else if (pid == 0) {
      // In child.
      logPipe->attachWriteEnd(STDERR_FILENO);
      dup2(STDERR_FILENO, STDOUT_FILENO);

      std::vector<char*> argv;
      argv.push_back(strdup("c++"));
      argv.push_back(strdup("-o"));
      argv.push_back(strdup(executablePath.c_str()));

      std::string command = "c++ -o " + executablePath;

      for (unsigned int i = 0; i < depsPaths.size(); i++) {
        argv.push_back(strdup(depsPaths[i].c_str()));
        command.push_back(' ');
        command.append(depsPaths[i]);
      }

      // Temporary hack:  Add "-lmd" until we auto-detect library deps.
      // TODO: Remove this.
      argv.push_back(strdup("-lmd"));
      command.append(" -lmd");

      argv.push_back(NULL);

      DEBUG_INFO << "exec: " << command;

      execvp(argv[0], &argv[0]);

      perror("execvp");
      exit(1);
    } else {
      OwnedPtr<EventManager::ContinuousReadCallback> loggerCallback;
      loggerCallback.allocateSubclass<Logger>(context, doneBarrier);
      eventManager->readContinuously(logPipe->readEnd(), &loggerCallback);

      OwnedPtr<EventManager::ProcessExitCallback> callback;
      callback.adopt(selfPtr);
      eventManager->waitPid(pid, &callback);
    }
  }

  // implements ProcessExitCallback ----------------------------------------------------
  void exited(int exitCode) {
    if (exitCode == 0) {
      doneBarrier->success();
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
  DoneBarrier* doneBarrier;

  OwnedPtrVector<File> deps;
  OwnedPtr<File> executableFile;

  OwnedPtrVector<File::DiskRef> depsRefs;
  OwnedPtr<File::DiskRef> executableRef;

  OwnedPtr<Pipe> logPipe;
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

const EntityId CppActionFactory::MAIN_SYMBOL = EntityId::fromName("c++symbol:main");

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
  *iter++ = MAIN_SYMBOL;
}

bool CppActionFactory::tryMakeAction(const EntityId& id, File* file, OwnedPtr<Action>* output) {
  if (id == MAIN_SYMBOL) {
    output->allocateSubclass<LinkAction>(file);
    return true;
  } else {
    return false;
  }
}

}  // namespace ekam
