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

#include "ExecPluginActionFactory.h"

#include <string.h>
#include <errno.h>
#include <map>

#include "os/Subprocess.h"
#include "ActionUtil.h"
#include "base/Debug.h"

namespace ekam {

namespace {

std::string splitToken(std::string* line) {
  std::string::size_type pos = line->find_first_of(' ');
  std::string result;
  if (pos == std::string::npos) {
    result = *line;
    line->clear();
  } else {
    result.assign(*line, 0, pos);
    line->erase(0, pos + 1);
  }
  return result;
}

}  // namespace

// =======================================================================================

class PluginDerivedActionFactory : public ActionFactory {
public:
  PluginDerivedActionFactory(OwnedPtr<File> executable,
                             std::string&& verb,
                             bool silent,
                             std::vector<Tag>&& triggers);
  ~PluginDerivedActionFactory();

  // implements ActionFactory -----------------------------------------------------------
  void enumerateTriggerTags(std::back_insert_iterator<std::vector<Tag> > iter);
  OwnedPtr<Action> tryMakeAction(const Tag& id, File* file);

private:
  OwnedPtr<File> executable;
  std::string verb;
  bool silent;
  std::vector<Tag> triggers;
};

// =======================================================================================

class PluginDerivedAction : public Action {
public:
  PluginDerivedAction(File* executable, const std::string& verb, bool silent, File* file)
      : executable(executable->clone()), verb(verb), silent(silent) {
    if (file != NULL) {
      this->file = file->clone();
    }
  }
  ~PluginDerivedAction() {}

  // implements Action -------------------------------------------------------------------
  std::string getVerb() { return verb; }
  bool isSilent() { return silent; }
  OwnedPtr<AsyncOperation> start(EventManager* eventManager, BuildContext* context);

private:
  class Process;
  class CommandReader;

  OwnedPtr<File> executable;
  std::string verb;
  bool silent;
  OwnedPtr<File> file;  // nullable
};

class PluginDerivedAction::CommandReader {
public:
  CommandReader(BuildContext* context, ByteStream* requestStream,
                OwnedPtr<ByteStream> responseStream, File* executable, File* input)
      : context(context), executable(executable->clone()),
        responseStream(responseStream.release()),
        lineReader(requestStream), silent(false) {
    if (input != NULL) {
      this->input = input->clone();
      knownFiles.add(input->canonicalName(), input->clone());
    }

    std::string junk;
    splitExtension(executable->basename(), &verb, &junk);
  }
  ~CommandReader() {}

  Promise<void> readAll(EventManager* eventManager) {
    return eventManager->when(lineReader.readLine(eventManager))(
      [=](OwnedPtr<std::string> line) -> Promise<void> {
        if (line == nullptr) {
          eof();
          return newFulfilledPromise();
        }

        consume(*line);
        return readAll(eventManager);
      }, [=](MaybeException<OwnedPtr<std::string>> error) {
        try {
          error.get();
        } catch (const std::exception& e) {
          context->log(e.what());
          context->failed();
          throw;
        } catch (...) {
          context->log("unknown exception");
          context->failed();
          throw;
        }
      });
  }

private:
  void consume(const std::string& line) {
    if (findInCache(line)) return;

    std::string args = line;
    std::string command = splitToken(&args);

    if (command == "verb") {
      verb = args;
    } else if (command == "silent") {
      silent = true;
    } else if (command == "trigger") {
      triggers.push_back(Tag::fromName(args));
    } else if (command == "findProvider" || command == "findInput") {
      File* provider;
      if (command == "findProvider") {
        provider = context->findProvider(Tag::fromName(args));
      } else if (input != NULL && args == input->canonicalName()) {
        provider = input.get();
      } else if (findInCache("newOutput " + args)) {
        // File was originally created by this action.  findInCache() already wrote the path,
        // so just return.
        return;
      } else {
        provider = context->findInput(args);
      }
      if (provider != NULL) {
        OwnedPtr<File::DiskRef> diskRef = provider->getOnDisk(File::READ);
        std::string path = diskRef->path();
        cache.insert(std::make_pair(line, diskRef.get()));
        diskRefs.add(diskRef.release());
        responseStream->writeAll(path.data(), path.size());

        knownFiles.add(path, provider->clone());
      }
      responseStream->writeAll("\n", 1);
    } else if (command == "newProvider") {
      // TODO:  Create a new output file and register it as a provider.
      context->log("newProvider not implemented");
      context->failed();
    } else if (command == "noteInput") {
      // The action is reading some file outside the working directory.  For now we ignore this.
      // TODO:  Pay attention?  We could trigger rebuilds when installed tools are updated, etc.
    } else if (command == "newOutput") {
      OwnedPtr<File> file = context->newOutput(args);

      OwnedPtr<File::DiskRef> diskRef = file->getOnDisk(File::WRITE);
      std::string path = diskRef->path();

      cache.insert(std::make_pair(line, diskRef.get()));
      diskRefs.add(diskRef.release());
      knownFiles.add(path, file.release());

      responseStream->writeAll(path.data(), path.size());
      responseStream->writeAll("\n", 1);
    } else if (command == "provide") {
      std::string filename = splitToken(&args);
      File* file = knownFiles.get(filename);
      if (file == NULL) {
        context->log("File passed to \"provide\" not created with \"newOutput\" nor noted as an "
                     "input: " + filename + "\n");
        context->failed();
      } else {
        provisions.insert(std::make_pair(file, Tag::fromName(args)));
      }
    } else if (command == "install") {
      std::string filename = splitToken(&args);
      File* file = knownFiles.get(filename);

      if (file == NULL) {
        context->log("File passed to \"install\" not created with \"newOutput\" nor noted as an "
                     "input: " + filename + "\n");
        context->failed();
      } else {
        std::string::size_type slashPos = args.find_first_of('/');
        if (slashPos == std::string::npos || slashPos == args.size() - 1) {
          context->log("invalid install location: " + args);
          context->failed();
        } else {
          std::string targetDir(args, 0, slashPos);
          std::string name(args, slashPos + 1);

          bool matched = false;
          BuildContext::InstallLocation location;

          for (int i = 0; i < BuildContext::INSTALL_LOCATION_COUNT; i++) {
            if (targetDir == BuildContext::INSTALL_LOCATION_NAMES[i]) {
              location = static_cast<BuildContext::InstallLocation>(i);
              matched = true;
              break;
            }
          }

          if (matched) {
            context->install(file, location, name);
          } else {
            context->log("invalid install location: " + args);
          }
        }
      }
    } else if (command == "passed") {
      context->passed();
    } else {
      context->log("invalid command: " + command);
      context->failed();
    }
  }

  void eof() {
    // Gather provisions and pass to context.
    std::vector<Tag> tags;
    File* currentFile = NULL;

    for (ProvisionMap::iterator iter = provisions.begin(); iter != provisions.end(); ++iter) {
      if (iter->first != currentFile && !tags.empty()) {
        context->provide(currentFile, tags);
        tags.clear();
      }
      currentFile = iter->first;
      tags.push_back(iter->second);
    }
    if (!tags.empty()) {
      context->provide(currentFile, tags);
    }

    // Also register new triggers.
    context->addActionType(newOwned<PluginDerivedActionFactory>(
        executable.release(), std::move(verb), silent, std::move(triggers)));
  }

private:
  BuildContext* context;
  OwnedPtr<File> executable;
  OwnedPtr<File> input;  // nullable
  OwnedPtr<ByteStream> responseStream;
  LineReader lineReader;

  std::string verb;
  bool silent;
  std::vector<Tag> triggers;

  OwnedPtrMap<std::string, File> knownFiles;

  typedef std::tr1::unordered_map<std::string, File::DiskRef*> CacheMap;
  CacheMap cache;
  OwnedPtrVector<File::DiskRef> diskRefs;

  typedef std::multimap<File*, Tag> ProvisionMap;
  ProvisionMap provisions;

  bool findInCache(const std::string& line) {
    CacheMap::const_iterator iter = cache.find(line);
    if (iter == cache.end()) {
      return false;
    } else {
      std::string path = iter->second->path();
      responseStream->writeAll(path.data(), path.size());
      responseStream->writeAll("\n", 1);
      return true;
    }
  }
};

class PluginDerivedAction::Process : public AsyncOperation {
public:
  Process(EventManager* eventManager, BuildContext* context, File* executable, File* input) {
    subprocess.addArgument(executable, File::READ);
    if (input != NULL) {
      subprocess.addArgument(input->canonicalName());
    }

    OwnedPtr<ByteStream> responseStream;
    responseStream = subprocess.captureStdin();
    commandStream = subprocess.captureStdout();
    logStream = subprocess.captureStderr();

    subprocessWaitOp = eventManager->when(subprocess.start(eventManager))(
      [context](ProcessExitCode exitCode) {
        if (exitCode.wasSignaled() || exitCode.getExitCode() != 0) {
          context->failed();
        }
      });

    commandReader = newOwned<CommandReader>(context, commandStream.get(), responseStream.release(),
                                            executable, input);
    commandOp = commandReader->readAll(eventManager);

    logger = newOwned<Logger>(context);
    logOp = logger->readAll(eventManager, logStream.get());
  }
  ~Process() {}

private:
  Subprocess subprocess;
  Promise<void> subprocessWaitOp;

  OwnedPtr<ByteStream> commandStream;
  OwnedPtr<CommandReader> commandReader;
  Promise<void> commandOp;

  OwnedPtr<ByteStream> logStream;
  OwnedPtr<Logger> logger;
  Promise<void> logOp;
};

OwnedPtr<AsyncOperation> PluginDerivedAction::start(EventManager* eventManager,
                                                    BuildContext* context) {
  return newOwned<Process>(eventManager, context, executable.get(), file.get());
}

// =======================================================================================

PluginDerivedActionFactory::PluginDerivedActionFactory(OwnedPtr<File> executable,
                                                       std::string&& verb,
                                                       bool silent,
                                                       std::vector<Tag>&& triggers)
    : executable(executable.release()), silent(silent) {
  this->verb.swap(verb);
  this->triggers.swap(triggers);
}
PluginDerivedActionFactory::~PluginDerivedActionFactory() {}

void PluginDerivedActionFactory::enumerateTriggerTags(
    std::back_insert_iterator<std::vector<Tag> > iter) {
  for (unsigned int i = 0; i < triggers.size(); i++) {
    *iter++ = triggers[i];
  }
}
OwnedPtr<Action> PluginDerivedActionFactory::tryMakeAction(const Tag& id, File* file) {
  return newOwned<PluginDerivedAction>(executable.get(), verb, silent, file);
}

// =======================================================================================

ExecPluginActionFactory::ExecPluginActionFactory() {}
ExecPluginActionFactory::~ExecPluginActionFactory() {}

// implements ActionFactory --------------------------------------------------------------

void ExecPluginActionFactory::enumerateTriggerTags(
    std::back_insert_iterator<std::vector<Tag> > iter) {
  *iter++ = Tag::fromName("filetype:.ekam-rule");
}

OwnedPtr<Action> ExecPluginActionFactory::tryMakeAction(const Tag& id, File* file) {
  return newOwned<PluginDerivedAction>(file, "learn", false, (File*)NULL);
}

}  // namespace ekam
