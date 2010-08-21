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

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "Driver.h"
#include "Debug.h"
#include "DiskFile.h"
#include "Action.h"
#include "SimpleDashboard.h"
#include "ConsoleDashboard.h"
//#include "KqueueEventManager.h"
#include "PollEventManager.h"
#include "CppActionFactory.h"

namespace ekam {

void usage(const char* command, FILE* out) {
  fprintf(out,
    "usage: %s [-hv] [-j jobcount]\n", command);
}

int main(int argc, char* argv[]) {
  const char* command = argv[0];
  int maxConcurrentActions = 1;

  while (true) {
    int opt = getopt(argc, argv, "hvj:");
    if (opt == -1) break;

    switch (opt) {
      case 'v':
        DebugMessage::setLogLevel(DebugMessage::INFO);
        break;
      case 'j': {
        char* endptr;
        maxConcurrentActions = strtoul(optarg, &endptr, 10);
        if (*endptr != '\0') {
          fprintf(stderr, "Expected number after -j.\n");
          return 1;
        }
        break;
      }
      case 'h':
        usage(command, stdout);
        return 0;
      default:
        usage(command, stderr);
        return 1;
    }
  }

  argc -= optind;
  argv += optind;

  if (argc > 0) {
    fprintf(stderr, "%s: unknown argument -- %s\n", command, argv[0]);
    return 1;
  }


  DiskFile src("src", NULL);
  DiskFile tmp("tmp", NULL);

  OwnedPtr<Dashboard> dashboard;
  if (isatty(STDOUT_FILENO)) {
    dashboard.allocateSubclass<ConsoleDashboard>(stdout);
  } else {
    dashboard.allocateSubclass<SimpleDashboard>(stdout);
  }

  // TODO:  Select KqueueEventManager when available.
//  KqueueEventManager eventManager;
  PollEventManager eventManager;

  Driver driver(&eventManager, dashboard.get(), &src, &tmp, maxConcurrentActions);

  CppActionFactory cppActionFactory;
  driver.addActionFactory(&cppActionFactory);

  driver.start();
  eventManager.loop();

  // For debugging purposes, check for zombie processes.
  int zombieCount = 0;
  while (true) {
    int status;
    pid_t pid = wait(&status);
    if (pid < 0) {
      if (errno == ECHILD) {
        // No more children.
        break;
      } else {
        DEBUG_ERROR << "wait: " << strerror(errno);
      }
    } else {
      ++zombieCount;
    }
  }

  if (zombieCount > 0) {
    DEBUG_ERROR << "There were " << zombieCount
        << " zombie processes after the event loop stopped.";
    return 1;
  } else {
    DEBUG_INFO << "No zombie processes detected.  Hooray.";
    return 0;
  }
}

}  // namespace ekam

int main(int argc, char* argv[]) {
  ekam::main(argc, argv);
}
