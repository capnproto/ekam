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

#ifndef EKAM_PROTODASHBOARD_H_
#define EKAM_PROTODASHBOARD_H_

#include <queue>
#include <string>

#include "Dashboard.h"
#include "ByteStream.h"
#include "EventManager.h"

namespace google {
  namespace protobuf {
    class MessageLite;
  }
}

namespace ekam {

class ProtoDashboard : public Dashboard {
public:
  ProtoDashboard(EventManager* eventManager, OwnedPtr<ByteStream>* streamToAdopt);
  ~ProtoDashboard();

  class DisconnectedCallback {
  public:
    virtual ~DisconnectedCallback() {}

    virtual void disconnected() = 0;
  };
  void onDisconnect(DisconnectedCallback* callback, OwnedPtr<AsyncOperation>* output);

  // implements Dashboard ----------------------------------------------------------------
  void beginTask(const std::string& verb, const std::string& noun,
                 Silence silence, OwnedPtr<Task>* output);

private:
  class TaskImpl;

  class WriteBuffer : public EventManager::IoCallback {
  public:
    WriteBuffer(EventManager* eventManager, OwnedPtr<ByteStream>* streamToAdopt);
    ~WriteBuffer();

    void write(const google::protobuf::MessageLite& data);
    void onDisconnect(DisconnectedCallback* callback, OwnedPtr<AsyncOperation>* output);

    // implements IoCallback -------------------------------------------------------------
    void ready();

  private:
    EventManager* eventManager;
    OwnedPtr<ByteStream> stream;
    std::queue<std::string> messages;
    std::string::size_type offset;
    OwnedPtr<AsyncOperation> waitWritableOp;

    class DisconnectOp : public AsyncOperation {
    public:
      DisconnectOp(WriteBuffer* writeBuffer, DisconnectedCallback* callback);
      ~DisconnectOp();

      void disconnected() { callback->disconnected(); }

    private:
      WriteBuffer* writeBuffer;
      DisconnectedCallback* callback;
    };
    DisconnectOp* disconnectOp;
  };

  int idCounter;
  WriteBuffer writeBuffer;
};

}  // namespace ekam

#endif  // EKAM_PROTODASHBOARD_H_
