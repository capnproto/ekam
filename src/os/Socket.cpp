// Kenton's Code Playground -- http://code.google.com/p/kentons-code
// Author: Kenton Varda (temporal@gmail.com)
// Copyright (c) 2010 Google, Inc. and contributors.
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

#include "Socket.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "base/Debug.h"

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

ServerSocket::ServerSocket(EventManager* eventManager, const std::string& bindAddress, int backlog)
    : eventManager(eventManager),
      handle(bindAddress, WRAP_SYSCALL(socket, AF_INET, SOCK_STREAM, 0)),
      watcher(eventManager->watchFd(handle.get())) {
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

Promise<OwnedPtr<ByteStream>> ServerSocket::accept() {
  return eventManager->when(watcher->onReadable())(
    [this](Void) -> Promise<OwnedPtr<ByteStream>> {
      int fd = ::accept(handle.get(), NULL, NULL);
      if (fd < 0) {
        switch (errno) {
          case EINTR:
          case ECONNABORTED:
          case EAGAIN:
#if EAGAIN != EWOULDBLOCK
          case EWOULDBLOCK:
#endif
            // This are "normal".  Try again.
            DEBUG_INFO << "accept: " << strerror(errno);
            return accept();
          default:
            throw OsError("accept", errno);
            break;
        }
      } else {
        // TODO:  Use peer address as name.
        return newFulfilledPromise(newOwned<ByteStream>(fd, "accepted connection"));
      }
    });
}


}  // namespace ekam
