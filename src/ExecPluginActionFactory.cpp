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

#include "Subprocess.h"
#include "ActionUtil.h"
#include "Debug.h"

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
  PluginDerivedActionFactory(OwnedPtr<File>* executableToAdopt,
                             std::string* verbToAdopt,
                             std::vector<Tag>* triggersToAdopt);
  ~PluginDerivedActionFactory();

  // implements ActionFactory -----------------------------------------------------------
  void enumerateTriggerTags(std::back_insert_iterator<std::vector<Tag> > iter);
  bool tryMakeAction(const Tag& id, File* file, OwnedPtr<Action>* output);

private:
  OwnedPtr<File> executable;
  std::string verb;
  std::vector<Tag> triggers;
};

// =======================================================================================

class PluginDerivedAction : public Action {
public:
  PluginDerivedAction(File* executable, const std::string& verb, File* file)
      : verb(verb) {
    executable->clone(&this->executable);
    if (file != NULL) {
      file->clone(&this->file);
    }
  }
  ~PluginDerivedAction() {}

  // implements Action -------------------------------------------------------------------
  std::string getVerb() { return verb; }
  void start(EventManager* eventManager, BuildContext* context, OwnedPtr<AsyncOperation>* output);

private:
  class Process;
  class CommandReader;

  OwnedPtr<File> executable;
  std::string verb;
  OwnedPtr<File> file;  // nullable
};

class PluginDerivedAction::CommandReader : public LineReader::Callback {
public:
  CommandReader(BuildContext* context, OwnedPtr<ByteStream>* responseStreamToAdopt,
                File* executable, File* input)
      : context(context), lineReader(this) {
    responseStream.adopt(responseStreamToAdopt);

    if (input != NULL) {
      input->clone(&this->input);

      OwnedPtr<File> inputClone;
      input->clone(&inputClone);
      knownFiles.adopt(input->canonicalName(), &inputClone);
    }

    executable->clone(&this->executable);
    std::string junk;
    splitExtension(executable->basename(), &verb, &junk);
  }
  ~CommandReader() {}

  ByteStream::ReadAllCallback* asReadAllCallback() {
    return &lineReader;
  }

  // implements LineReader::Callback -----------------------------------------------------
  void consume(const std::string& line) {
    if (findInCache(line)) return;

    std::string args = line;
    std::string command = splitToken(&args);

    if (command == "verb") {
      verb = args;
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
        OwnedPtr<File::DiskRef> diskRef;
        provider->getOnDisk(File::READ, &diskRef);
        std::string path = diskRef->path();
        cache.insert(std::make_pair(line, diskRef.get()));
        diskRefs.adoptBack(&diskRef);
        responseStream->writeAll(path.data(), path.size());

        OwnedPtr<File> fileClone;
        provider->clone(&fileClone);
        knownFiles.adopt(path, &fileClone);
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
      OwnedPtr<File> file;
      context->newOutput(args, &file);

      OwnedPtr<File::DiskRef> diskRef;
      file->getOnDisk(File::WRITE, &diskRef);
      std::string path = diskRef->path();

      cache.insert(std::make_pair(line, diskRef.get()));
      diskRefs.adoptBack(&diskRef);
      knownFiles.adopt(path, &file);

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
    OwnedPtr<ActionFactory> newFactory;
    newFactory.allocateSubclass<PluginDerivedActionFactory>(&executable, &verb, &triggers);
    context->addActionType(&newFactory);
  }

  void error(int number) {
    context->log("read(command pipe): " + std::string(strerror(number)));
    context->failed();
  }

private:
  BuildContext* context;
  OwnedPtr<File> executable;
  OwnedPtr<File> input;  // nullable
  OwnedPtr<ByteStream> responseStream;
  LineReader lineReader;

  std::string verb;
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

class PluginDerivedAction::Process : public AsyncOperation,
                                     public EventManager::ProcessExitCallback {
public:
  Process(EventManager* eventManager, BuildContext* context, File* executable, File* input)
      : context(context) {
    subprocess.addArgument(executable, File::READ);
    if (input != NULL) {
      subprocess.addArgument(input->canonicalName());
    }

    OwnedPtr<ByteStream> responseStream;
    subprocess.captureStdin(&responseStream);
    subprocess.captureStdout(&commandStream);
    subprocess.captureStderr(&logStream);
    subprocess.start(eventManager, this);

    commandReader.allocate(context, &responseStream, executable, input);
    commandStream->readAll(eventManager, commandReader->asReadAllCallback(), &commandOp);

    logger.allocate(context);
    logStream->readAll(eventManager, logger.get(), &logOp);
  }
  ~Process() {}

  // implements ProcessExitCallback ------------------------------------------------------
  void exited(int exitCode) {
    if (exitCode != 0) {
      context->failed();
    }
  }
  void signaled(int signalNumber) {
    context->failed();
  }

private:
  BuildContext* context;
  Subprocess subprocess;

  OwnedPtr<ByteStream> commandStream;
  OwnedPtr<CommandReader> commandReader;
  OwnedPtr<AsyncOperation> commandOp;

  OwnedPtr<ByteStream> logStream;
  OwnedPtr<Logger> logger;
  OwnedPtr<AsyncOperation> logOp;
};

void PluginDerivedAction::start(EventManager* eventManager, BuildContext* context,
                                OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<Process>(eventManager, context, executable.get(), file.get());
}

// =======================================================================================

PluginDerivedActionFactory::PluginDerivedActionFactory(OwnedPtr<File>* executableToAdopt,
                                                       std::string* verbToAdopt,
                                                       std::vector<Tag>* triggersToAdopt) {
  executable.adopt(executableToAdopt);
  verb.swap(*verbToAdopt);
  triggers.swap(*triggersToAdopt);
}
PluginDerivedActionFactory::~PluginDerivedActionFactory() {}

void PluginDerivedActionFactory::enumerateTriggerTags(
    std::back_insert_iterator<std::vector<Tag> > iter) {
  for (unsigned int i = 0; i < triggers.size(); i++) {
    *iter++ = triggers[i];
  }
}
bool PluginDerivedActionFactory::tryMakeAction(const Tag& id, File* file, OwnedPtr<Action>* output) {
  output->allocateSubclass<PluginDerivedAction>(executable.get(), verb, file);
  return true;
}

// =======================================================================================

ExecPluginActionFactory::ExecPluginActionFactory() {}
ExecPluginActionFactory::~ExecPluginActionFactory() {}

// implements ActionFactory --------------------------------------------------------------

void ExecPluginActionFactory::enumerateTriggerTags(
    std::back_insert_iterator<std::vector<Tag> > iter) {
  *iter++ = Tag::fromName("filetype:.ekam-rule");
}

bool ExecPluginActionFactory::tryMakeAction(
    const Tag& id, File* file, OwnedPtr<Action>* output) {
  output->allocateSubclass<PluginDerivedAction>(file, "learn", (File*)NULL);
  return true;
}

}  // namespace ekam
