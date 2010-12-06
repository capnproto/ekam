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

#include "Socket.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "Debug.h"

namespace ekam {

namespace {

std::string splitFirst(std::string* str, char delim) {
  std::string::size_type pos = str->find_first_of(delim);
  std::string result;
  if (pos == std::string::npos) {
    result = *str;
    str->clear();
  } else {
    result.assign(*str, 0, pos);
    str->erase(0, pos + 1);
  }
  return result;
}

bool parseIpAddr(std::string text, struct sockaddr_in* addr) {
  // TODO:  This code sucks.  Find a library to call.

  addr->sin_family = AF_INET;

  std::string address = splitFirst(&text, ':');
  if (text.empty()) return false;

  std::vector<unsigned long> parts;
  while (!address.empty()) {
    std::string part = splitFirst(&address, '.');
    char* end;
    parts.push_back(strtoul(part.c_str(), &end, 0));
    if (end != part.data() + part.size()) return false;
  }

  if (parts.size() > 4) return false;

  addr->sin_addr.s_addr = 0;

  if (!parts.empty()) {
    for (size_t i = 0; i < parts.size() - 1; i++) {
      if (parts[i] > 0xFFu) return false;
      addr->sin_addr.s_addr |= parts[i] << ((3 - i) * 8);
    }

    if (parts.back() > (0xFFFFFFFFu >> ((parts.size() - 1) * 8))) return false;
    addr->sin_addr.s_addr |= parts.back();

    addr->sin_addr.s_addr = htonl(addr->sin_addr.s_addr);
  }

  char* end;
  unsigned long port = strtoul(text.c_str(), &end, 0);
  if (end != text.data() + text.size() || port > 0xFFFFu) return false;
  addr->sin_port = htons(port);

  return true;
}

}  // namespace

ServerSocket::ServerSocket(const std::string& bindAddress, int backlog)
    : handle(bindAddress, WRAP_SYSCALL(socket, AF_INET, SOCK_STREAM, 0)) {
  WRAP_SYSCALL(fcntl, handle, F_SETFL, O_NONBLOCK);

  int optval = 1;
  WRAP_SYSCALL(setsockopt, handle,  SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  struct sockaddr_in addr;
  if (!parseIpAddr(bindAddress, &addr)) {
    throw std::invalid_argument("Invalid bind address: " + bindAddress);
  }

  WRAP_SYSCALL(bind, handle, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  WRAP_SYSCALL(listen, handle, (backlog == 0) ? SOMAXCONN : backlog);
}

ServerSocket::~ServerSocket() {}

ServerSocket::AcceptCallback::~AcceptCallback() {}

class ServerSocket::AcceptOp : public EventManager::IoCallback, public AsyncOperation {
public:
  AcceptOp(EventManager* eventManager, OsHandle* handle, AcceptCallback* callback)
      : handle(handle), callback(callback) {
    eventManager->onReadable(handle->get(), this, &waitReadableOp);
  }
  ~AcceptOp() {}

  // implements IoCallback ---------------------------------------------------------------
  void ready() {
    int fd = accept(handle->get(), NULL, NULL);
    if (fd < 0) {
      switch (errno) {
        case EINTR:
        case ECONNABORTED:
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
          // This are "normal".
          DEBUG_INFO << "accept: " << strerror(errno);
          break;
        default:
          DEBUG_ERROR << "accept: " << strerror(errno);
          break;
      }
    } else {
      OwnedPtr<ByteStream> stream;
      // TODO:  Use peer address as name.
      stream.allocate(fd, "accepted connection");
      callback->accepted(&stream);
    }
  }

private:
  OsHandle* handle;
  AcceptCallback* callback;
  OwnedPtr<AsyncOperation> waitReadableOp;
};

void ServerSocket::onAccept(EventManager* eventManager, AcceptCallback* callback,
                            OwnedPtr<AsyncOperation>* output) {
  output->allocateSubclass<AcceptOp>(eventManager, &handle, callback);
}


}  // namespace ekam
