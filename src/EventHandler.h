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

#ifndef EKAM_CANCELABLEEVENTHANDLER_H_
#define EKAM_CANCELABLEEVENTHANDLER_H_

#include "OwnedPtr.h"
#include "EventManager.h"
#include "Debug.h"

namespace ekam {

template <typename Event>
class EventHandler {
public:
  virtual ~EventHandler() {}

  virtual bool handle(const Event& event) = 0;
};

template <typename Event>
class EventHandlerRegistrar {
public:
  virtual ~EventHandlerRegistrar() {}

  virtual void unregister(EventHandler<Event>* handler) = 0;
};

template <typename Event>
class EventHandlerRegistrarImpl : public EventHandlerRegistrar<Event> {
public:
  typedef OwnedPtrMap<EventHandler<Event>*, EventHandler<Event> > HandlerMap;
  EventHandlerRegistrarImpl(HandlerMap* handlerMap) : handlerMap(handlerMap) {}
  ~EventHandlerRegistrarImpl() {}

  // implements EventHandlerRegistrar ----------------------------------------------------
  void unregister(EventHandler<Event>* handler) {
    if (!handlerMap->erase(handler)) {
      DEBUG_ERROR << "Tried to unregister handler that was not registered.";
    }
  }

private:
  HandlerMap* handlerMap;
};

template <typename Event>
class CancelableEventHandler : public EventHandler<Event> {
public:
  CancelableEventHandler(OwnedPtr<EventHandler<Event> >* innerHandlerToAdopt,
                         EventHandlerRegistrar<Event>* registrar,
                         OwnedPtr<EventManager::Canceler>* output);
  ~CancelableEventHandler();

  // implements EventHandler -------------------------------------------------------------
  bool handle(const Event& event);

private:
  class CancelationDeferrer;
  class CancelerImpl;

  CancelerImpl* canceler;
  CancelationDeferrer* cancelationDeferrer;
  OwnedPtr<EventHandler<Event> > innerHandler;
};

// =======================================================================================
// Implementation details

template <typename Event>
class CancelableEventHandler<Event>::CancelationDeferrer {
public:
  CancelationDeferrer(CancelableEventHandler<Event>* handler)
      : handler(handler), canceled(false) {
    handler->cancelationDeferrer = this;
  }
  ~CancelationDeferrer() {
    handler->cancelationDeferrer = NULL;
  }

  void cancelWhenLeavingScope() {
    canceled = true;
  }

  bool shouldCancel(bool handleResult) {
    return handleResult || canceled;
  }

private:
  CancelableEventHandler<Event>* handler;
  bool canceled;
};

template <typename Event>
class CancelableEventHandler<Event>::CancelerImpl : public EventManager::Canceler {
public:
  CancelerImpl(CancelableEventHandler<Event>* handler,
               EventHandlerRegistrar<Event>* registrar)
      : handler(handler), registrar(registrar) {}
  ~CancelerImpl() {
    if (handler != NULL) {
      handler->canceler = NULL;
    }
  }

  // implements Canceler ---------------------------------------------------------------
  void cancel() {
    if (handler != NULL) {
      if (handler->cancelationDeferrer == NULL) {
        registrar->unregister(handler);
      } else {
        handler->cancelationDeferrer->cancelWhenLeavingScope();
      }
    }
  }

private:
  CancelableEventHandler<Event>* handler;
  EventHandlerRegistrar<Event>* registrar;

  friend class CancelableEventHandler;
};

template <typename Event>
CancelableEventHandler<Event>::CancelableEventHandler(
    OwnedPtr<EventHandler<Event> >* innerHandlerToAdopt,
    EventHandlerRegistrar<Event>* registrar,
    OwnedPtr<EventManager::Canceler>* output)
    : canceler(NULL), cancelationDeferrer(NULL) {
  innerHandler.adopt(innerHandlerToAdopt);

  OwnedPtr<CancelerImpl> canceler;
  canceler.allocate(this, registrar);
  this->canceler = canceler.get();
  output->adopt(&canceler);
}

template <typename Event>
CancelableEventHandler<Event>::~CancelableEventHandler() {
  if (canceler != NULL) {
    canceler->handler = NULL;
  }
}

template <typename Event>
bool CancelableEventHandler<Event>::handle(const Event& event) {
  CancelationDeferrer deferrer(this);
  return deferrer.shouldCancel(innerHandler->handle(event));
}

}  // namespace ekam

#endif  // EKAM_CANCELABLEEVENTHANDLER_H_
