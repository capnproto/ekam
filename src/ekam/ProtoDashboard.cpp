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

#include "ProtoDashboard.h"

#include <errno.h>
#include <unistd.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <stdlib.h>

#include "dashboard.capnp.h"
#include "os/Socket.h"
#include "MuxDashboard.h"

namespace ekam {

class ProtoDashboard::TaskImpl : public Dashboard::Task {
public:
  TaskImpl(int id, const std::string& verb, const std::string& noun,
           Silence silence, WriteBuffer* output);
  ~TaskImpl();

  // implements Task ---------------------------------------------------------------------
  void setState(TaskState state);
  void addOutput(const std::string& text);

private:
  int id;
  WriteBuffer* output;

  static const proto::TaskUpdate::State STATE_CODES[];
};

const proto::TaskUpdate::State ProtoDashboard::TaskImpl::STATE_CODES[] = {
  proto::TaskUpdate::State::PENDING,
  proto::TaskUpdate::State::RUNNING,
  proto::TaskUpdate::State::DONE   ,
  proto::TaskUpdate::State::PASSED ,
  proto::TaskUpdate::State::FAILED ,
  proto::TaskUpdate::State::BLOCKED
};

ProtoDashboard::TaskImpl::TaskImpl(int id, const std::string& verb, const std::string& noun,
                                   Silence silence, WriteBuffer* output)
    : id(id), output(output) {
  capnp::MallocMessageBuilder message;
  proto::TaskUpdate::Builder update = message.getRoot<proto::TaskUpdate>();
  update.setId(id);
  update.setState(proto::TaskUpdate::State::PENDING);
  update.setVerb(verb);
  update.setNoun(noun);
  update.setSilent(silence == SILENT);
  output->write(message.getSegmentsForOutput());
}

ProtoDashboard::TaskImpl::~TaskImpl() {
  capnp::MallocMessageBuilder message;
  proto::TaskUpdate::Builder update = message.getRoot<proto::TaskUpdate>();
  update.setId(id);
  update.setState(proto::TaskUpdate::State::DELETED);
  output->write(message.getSegmentsForOutput());
}

void ProtoDashboard::TaskImpl::setState(TaskState state) {
  capnp::MallocMessageBuilder message;
  proto::TaskUpdate::Builder update = message.getRoot<proto::TaskUpdate>();
  update.setId(id);
  update.setState(STATE_CODES[state]);
  output->write(message.getSegmentsForOutput());
}

void ProtoDashboard::TaskImpl::addOutput(const std::string& text) {
  capnp::MallocMessageBuilder message;
  proto::TaskUpdate::Builder update = message.getRoot<proto::TaskUpdate>();
  update.setId(id);
  update.setLog(text);
  output->write(message.getSegmentsForOutput());
}

// =======================================================================================

ProtoDashboard::ProtoDashboard(EventManager* eventManager, OwnedPtr<ByteStream> stream)
    : idCounter(0),
      writeBuffer(eventManager, stream.release()) {
  capnp::MallocMessageBuilder message;
  proto::Header::Builder header = message.getRoot<proto::Header>();
  char* cwd = get_current_dir_name();
  header.setProjectRoot(cwd);
  free(cwd);
  writeBuffer.write(message.getSegmentsForOutput());
}
ProtoDashboard::~ProtoDashboard() {}

OwnedPtr<Dashboard::Task> ProtoDashboard::beginTask(
    const std::string& verb, const std::string& noun, Silence silence) {
  return newOwned<TaskImpl>(++idCounter, verb, noun, silence, &writeBuffer);
}

// =======================================================================================

ProtoDashboard::WriteBuffer::WriteBuffer(EventManager* eventManager,
                                         OwnedPtr<ByteStream> stream)
    : eventManager(eventManager), stream(stream.release()),
      ioWatcher(eventManager->watchFd(this->stream->getHandle()->get())),
      offset(0), disconnectFulfiller(NULL) {}
ProtoDashboard::WriteBuffer::~WriteBuffer() {}

void ProtoDashboard::WriteBuffer::write(kj::ArrayPtr<const kj::ArrayPtr<const capnp::word>> message) {
  if (stream == NULL) {
    // Already disconnected.
    return;
  }

  messages.push(capnp::messageToFlatArray(message));

  ready();
}

void ProtoDashboard::WriteBuffer::ready() {
  try {
    while (!messages.empty()) {
      const kj::Array<capnp::word>& message = messages.front();
      while (offset < message.size()) {
        offset += stream->write(message.asBytes().begin() + offset,
                                message.asBytes().size() - offset);
      }
      offset = 0;
      messages.pop();
    }
  } catch (const OsError& error) {
    if (error.getErrorNumber() == EAGAIN) {
      // Ran out of kernel buffer space.  Wait until writable again.
      waitWritablePromise = eventManager->when(ioWatcher->onWritable())(
        [this](Void) {
          ready();
        });
    } else {
      stream.clear();

      if (disconnectFulfiller != NULL) {
        disconnectFulfiller->disconnected();
      }
    }
  }
}

// =======================================================================================

ProtoDashboard::WriteBuffer::DisconnectFulfiller::DisconnectFulfiller(Callback* callback,
                                                                      WriteBuffer* writeBuffer)
    : callback(callback), writeBuffer(writeBuffer) {
  if (writeBuffer->disconnectFulfiller != NULL) {
    throw std::logic_error("Can only register one disconnect callback at a time.");
  }
  writeBuffer->disconnectFulfiller = this;
}

ProtoDashboard::WriteBuffer::DisconnectFulfiller::~DisconnectFulfiller() {
  assert(writeBuffer->disconnectFulfiller == this);
  writeBuffer->disconnectFulfiller = NULL;
}

Promise<void> ProtoDashboard::onDisconnect() {
  return writeBuffer.onDisconnect();
}

Promise<void> ProtoDashboard::WriteBuffer::onDisconnect() {
  return newPromise<DisconnectFulfiller>(this);
}

// =======================================================================================

class NetworkAcceptingDashboard : public Dashboard {
public:
  NetworkAcceptingDashboard(EventManager* eventManager, const std::string& address,
                            OwnedPtr<Dashboard> baseDashboard)
      : eventManager(eventManager),
        base(baseDashboard.release()),
        baseConnector(newOwned<MuxDashboard::Connector>(&mux, base.get())),
        socket(newOwned<ServerSocket>(eventManager, address)),
        acceptOp(doAccept()) {}
  ~NetworkAcceptingDashboard() {}

  Promise<void> doAccept() {
    return eventManager->when(socket->accept())(
      [this](OwnedPtr<ByteStream> stream){
        accepted(stream.release());
        return doAccept();
      });
  }

  void accepted(OwnedPtr<ByteStream> stream);

  // implements Dashboard ----------------------------------------------------------------
  OwnedPtr<Task> beginTask(const std::string& verb, const std::string& noun, Silence silence) {
    return mux.beginTask(verb, noun, silence);
  }

private:
  EventManager* eventManager;
  OwnedPtr<Dashboard> base;
  MuxDashboard mux;
  OwnedPtr<MuxDashboard::Connector> baseConnector;
  OwnedPtr<ServerSocket> socket;
  Promise<void> acceptOp;

  class ConnectedProtoDashboard {
  public:
    ConnectedProtoDashboard(NetworkAcceptingDashboard* owner, EventManager* eventManager,
                            OwnedPtr<ByteStream> stream)
        : protoDashboard(eventManager, stream.release()),
          connector(newOwned<MuxDashboard::Connector>(&owner->mux, &protoDashboard)) {
      disconnectPromise = eventManager->when(protoDashboard.onDisconnect())(
        [this, owner](Void) {
          connector.clear();
          owner->connectedDashboards.erase(this);
        });
    }
    ~ConnectedProtoDashboard() {}

  private:
    ProtoDashboard protoDashboard;
    OwnedPtr<MuxDashboard::Connector> connector;
    Promise<void> disconnectPromise;
  };
  OwnedPtrMap<ConnectedProtoDashboard*, ConnectedProtoDashboard> connectedDashboards;
};

void NetworkAcceptingDashboard::accepted(OwnedPtr<ByteStream> stream) {
  auto connectedDashboard = newOwned<ConnectedProtoDashboard>(this, eventManager, stream.release());
  auto key = connectedDashboard.get();  // cannot inline due to undefined evaluation order
  connectedDashboards.add(key, connectedDashboard.release());
}

OwnedPtr<Dashboard> initNetworkDashboard(EventManager* eventManager, const std::string& address,
                                         OwnedPtr<Dashboard> dashboardToWrap) {
  return newOwned<NetworkAcceptingDashboard>(eventManager, address, dashboardToWrap.release());
}

}  // namespace ekam
