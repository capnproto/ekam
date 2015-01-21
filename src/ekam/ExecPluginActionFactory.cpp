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
  Promise<void> start(EventManager* eventManager, BuildContext* context);

private:
  class CommandReader;

  OwnedPtr<File> executable;
  std::string verb;
  bool silent;
  OwnedPtr<File> file;  // nullable
};

class PluginDerivedAction::CommandReader {
public:
  CommandReader(BuildContext* context, OwnedPtr<ByteStream> requestStream,
                OwnedPtr<ByteStream> responseStream, File* executable, File* input)
      : context(context), executable(executable->clone()),
        requestStream(requestStream.release()),
        responseStream(responseStream.release()),
        lineReader(this->requestStream.get()), silent(false) {
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
    } else if (command == "findModifiers") {
      auto dir = input->parent();
      std::vector<File*> results;
      for (;;) {
        File* provider = context->findProvider(Tag::fromName(
            "canonical:" + dir->relative(args)->canonicalName()));
        if (provider != NULL) {
          results.push_back(provider);
        }
        if (!dir->hasParent()) {
          break;
        }
        dir = dir->parent();
      }

      for (auto iter = results.rbegin(); iter != results.rend(); ++iter) {
        File* provider = *iter;
        OwnedPtr<File::DiskRef> diskRef = provider->getOnDisk(File::READ);
        std::string path = diskRef->path();
        diskRefs.add(diskRef.release());
        responseStream->writeAll(path.data(), path.size());
        knownFiles.add(path, provider->clone());
        responseStream->writeAll("\n", 1);
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
  OwnedPtr<ByteStream> requestStream;
  OwnedPtr<ByteStream> responseStream;
  LineReader lineReader;

  std::string verb;
  bool silent;
  std::vector<Tag> triggers;

  OwnedPtrMap<std::string, File> knownFiles;

  typedef std::unordered_map<std::string, File::DiskRef*> CacheMap;
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

Promise<void> PluginDerivedAction::start(EventManager* eventManager, BuildContext* context) {
  auto subprocess = newOwned<Subprocess>();

  subprocess->addArgument(executable.get(), File::READ);
  if (file != NULL) {
    subprocess->addArgument(file->canonicalName());
  }

  OwnedPtr<ByteStream> responseStream = subprocess->captureStdin();
  OwnedPtr<ByteStream> commandStream = subprocess->captureStdout();
  OwnedPtr<ByteStream> logStream = subprocess->captureStderr();

  auto subprocessWaitOp = eventManager->when(subprocess->start(eventManager))(
    [context](ProcessExitCode exitCode) {
      if (exitCode.wasSignaled() || exitCode.getExitCode() != 0) {
        context->failed();
      }
    });

  auto commandReader = newOwned<CommandReader>(
      context, commandStream.release(), responseStream.release(), executable.get(), file.get());
  auto commandOp = commandReader->readAll(eventManager);

  OwnedPtr<Logger> logger = newOwned<Logger>(context, logStream.release());
  auto logOp = logger->run(eventManager);

  return eventManager->when(subprocessWaitOp, commandOp, logOp, subprocess, commandReader, logger)(
      [](Void, Void, Void, OwnedPtr<Subprocess>, OwnedPtr<CommandReader>, OwnedPtr<Logger>){});
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
