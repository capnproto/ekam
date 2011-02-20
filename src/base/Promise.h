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

#ifndef EKAM_BASE_PROMISE_H_
#define EKAM_BASE_PROMISE_H_

#include <set>
#include <vector>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "OwnedPtr.h"

namespace ekam {

// =======================================================================================
// TODO:  Put elsewhere

enum class Void {
  VOID
};

template <typename... Types>
class ValuePack;

template <typename Head, typename... Tail>
class ValuePack<Head, Tail...>: private ValuePack<Tail...> {
public:
  ValuePack(Head&& head, Tail&&... tail)
      : ValuePack<Tail...>(std::forward<Tail>(tail)...),
        head(std::forward<Head>(head)) {}
  ValuePack(const ValuePack& other) = delete;
  ValuePack(ValuePack&& other) = default;

  ValuePack& operator=(const ValuePack& other) = delete;
  ValuePack& operator=(ValuePack&& other) {
    head = std::move(other.head);
    ValuePack<Tail...>::operator=(std::move(other));
  }

  template <typename Func, typename... Params>
  auto apply(const Func& func, Params&&... params) const ->
      decltype(func(std::declval<Params>()..., std::declval<const Head&>(),
                    std::declval<const Tail&>()...)) {
    return ValuePack<Tail...>::apply(func, std::forward<Params>(params)..., head);
  }

  template <typename Func, typename... Params>
  auto applyMoving(const Func& func, Params&&... params) ->
      decltype(func(std::declval<Params>()..., std::declval<Head>(), std::declval<Tail>()...)) {
    return ValuePack<Tail...>::applyMoving(func, std::forward<Params>(params)..., std::move(head));
  }

private:
  Head head;
};

template <>
class ValuePack<> {
public:
  ValuePack() {}

  template <typename Func, typename... Params>
  auto apply(const Func& func, Params&&... params) const ->
      decltype(func(std::declval<Params>()...)) {
    func(std::forward<Params>(params)...);
  }

  template <typename Func, typename... Params>
  auto applyMoving(const Func& func, Params&&... params) ->
      decltype(func(std::declval<Params>()...)) {
    func(std::forward<Params>(params)...);
  }
};

// =======================================================================================
// TODO:  Put elsewhere

class WeakLink {
public:
  WeakLink(): other(nullptr) {}
  WeakLink(WeakLink* other): other(other) {
    if (other != nullptr) {
      other->disentangle();
      other->other = this;
    }
  }
  ~WeakLink() {
    disentangle();
  }

  void entangle(WeakLink* other) {
    disentangle();
    this->other = other;
    if (other != nullptr) {
      other->disentangle();
      other->other = this;
    }
  }

  bool isEntangled() {
    return other != nullptr;
  }

private:
  WeakLink* other;

  void disentangle() {
    if (other != nullptr) {
      other->other = nullptr;
      other = nullptr;
    }
  }
};

// =======================================================================================

template <typename T>
class Promise;

class Runnable {
public:
  virtual ~Runnable();

  virtual void run() = 0;
};

class PendingRunnable {
public:
  virtual ~PendingRunnable();
};

class Executor {
public:
  virtual ~Executor();

  virtual OwnedPtr<PendingRunnable> runLater(OwnedPtr<Runnable> runnable) = 0;

  template <typename... Types>
  class When;

  template <typename... Types>
  When<typename std::remove_reference<Types>::type...> when(Types&&... params);
};

template <typename Func>
OwnedPtr<Runnable> newLambdaRunnable(Func&& func) {
  class RunnableImpl: public Runnable {
  public:
    RunnableImpl(Func&& func): func(std::move(func)) {}
    ~RunnableImpl() {}

    void run() {
      func();
    }

  private:
    Func func;
  };

  return newOwned<RunnableImpl>(std::move(func));
}

// =======================================================================================

template <typename T>
class MaybeException {
public:
  MaybeException(): broken(false) {
    new(&this->value) T;
  }
  MaybeException(T value): broken(false) {
    new(&this->value) T(std::move(value));
  }
  MaybeException(std::exception_ptr error): broken(true) {
    new(&this->error) std::exception_ptr(std::move(error));
  }
  MaybeException(const MaybeException& other) {
    if (broken) {
      new(&error) std::exception_ptr(other.error);
    } else {
      new(&value) T(other.value);
    }
  }
  MaybeException(MaybeException&& other): broken(other.broken) {
    if (broken) {
      new(&error) std::exception_ptr(std::move(other.error));
    } else {
      new(&value) T(std::move(other.value));
    }
  }

  ~MaybeException() {
    if (broken) {
      error.~exception_ptr();
    } else {
      value.~T();
    }
  }

  MaybeException& operator=(const MaybeException& other) {
    this->~MaybeException();
    broken = other.broken;
    if (broken) {
      new(&error) std::exception_ptr(other.error);
    } else {
      new(&value) T(other.value);
    }
    return *this;
  }
  MaybeException& operator=(MaybeException&& other) {
    this->~MaybeException();
    broken = other.broken;
    if (broken) {
      new(&error) std::exception_ptr(std::move(other.error));
    } else {
      new(&value) T(std::move(other.value));
    }
    return *this;
  }

  bool isException() {
    return broken;
  }

  const T& get() {
    if (broken) {
      std::rethrow_exception(error);
    }
    return value;
  }

  T release() {
    if (broken) {
      std::rethrow_exception(error);
    }
    return std::move(value);
  }

private:
  bool broken;
  union {
    T value;
    std::exception_ptr error;
  };
};

template <>
class MaybeException<void> {
public:
  MaybeException(): broken(false) {}
  MaybeException(std::exception_ptr error): broken(true) {
    new(&this->error) std::exception_ptr(std::move(error));
  }
  MaybeException(const MaybeException& other) {
    if (broken) {
      new(&error) std::exception_ptr(other.error);
    }
  }
  MaybeException(MaybeException&& other): broken(other.broken) {
    if (broken) {
      new(&error) std::exception_ptr(std::move(other.error));
    }
  }

  ~MaybeException() {
    if (broken) {
      error.~exception_ptr();
    }
  }

  MaybeException& operator=(const MaybeException& other) {
    this->~MaybeException();
    broken = other.broken;
    if (broken) {
      new(&error) std::exception_ptr(other.error);
    }
    return *this;
  }
  MaybeException& operator=(MaybeException&& other) {
    this->~MaybeException();
    broken = other.broken;
    if (broken) {
      new(&error) std::exception_ptr(std::move(other.error));
    }
    return *this;
  }

  bool isException() {
    return broken;
  }

  Void get() {
    if (broken) {
      std::rethrow_exception(error);
    }
    return Void::VOID;
  }

  Void release() {
    if (broken) {
      std::rethrow_exception(error);
    }
    return Void::VOID;
  }

private:
  bool broken;
  union {
    std::exception_ptr error;
  };
};

// =======================================================================================

namespace promiseInternal {

class PromiseStateBase {
public:
  PromiseStateBase(Executor* executor, WeakLink* fulfiller)
      : executor(executor), isFulfilled(false), dependencyFailed(false),
        dependenciesLeft(0), dependent(NULL), fulfillerLink(fulfiller) {}

  PromiseStateBase(const PromiseStateBase& other) = delete;
  PromiseStateBase& operator=(const PromiseStateBase& other) = delete;

  virtual ~PromiseStateBase() {}

  void addDependency(PromiseStateBase* dependency) {
    if (dependency->dependent != NULL) {
      throw std::invalid_argument("Already waiting on this Promise.");
    }
    dependency->dependent = this;
    if (!dependency->isFulfilled) {
      ++dependenciesLeft;
    }
  }

  void allDependenciesAdded() {
    if (dependenciesLeft == 0) {
      readyLater();
    }
  }

protected:
  virtual void ready() {}
  virtual void error() {}

  void preFulfill() {
    if (isFulfilled) {
      throw std::logic_error("Already fulfilled this promise.");
    }
    isFulfilled = true;
  }

  void postFulfill(bool isError) {
    std::vector<PromiseStateBase*> completedDependents;
    if (dependent != NULL) {
      if (isError) {
        dependent->dependencyFailed = true;
      }
      if (--dependent->dependenciesLeft == 0) {
        dependent->readyLater();
      }
    }
  }

private:
  Executor* executor;
  bool isFulfilled;
  bool dependencyFailed;
  int dependenciesLeft;
  PromiseStateBase* dependent;
  WeakLink fulfillerLink;
  OwnedPtr<PendingRunnable> pendingReadyLater;

  void readyLater() {
    pendingReadyLater = executor->runLater(newLambdaRunnable([this]() {
      auto objectToDeleteOnReturn = pendingReadyLater.release();
      if (dependencyFailed) {
        error();
      } else {
        ready();
      }
    }));
  }
};

template <typename T>
class PromiseState: public PromiseStateBase {
public:
  PromiseState(Executor* executor, WeakLink* fulfiller)
      : PromiseStateBase(executor, fulfiller) {}
  ~PromiseState() {}

  void fulfill(const T& value) {
    preFulfill();
    this->value = value;
    postFulfill(false);
  }

  void fulfill(T&& value) {
    preFulfill();
    this->value = std::move(value);
    postFulfill(false);
  }

  void propagateCurrentException() {
    preFulfill();
    this->value = std::current_exception();
    postFulfill(true);
  }

  template <typename Func, typename... Params>
  void callAndFulfill(const Func& func, Params&&... params) {
    try {
      fulfill(func(std::forward<Params>(params)...));
    } catch (...) {
      propagateCurrentException();
    }
  }

  T release() {
    return value.release();
  }

  MaybeException<T> releaseMaybeException() {
    return std::move(value);
  }

private:
  MaybeException<T> value;
};

template <>
class PromiseState<void>: public promiseInternal::PromiseStateBase {
public:
  PromiseState(Executor* executor, WeakLink* fulfiller)
      : PromiseStateBase(executor, fulfiller) {}
  ~PromiseState() {}

  void fulfill() {
    preFulfill();
    postFulfill(false);
  }

  void propagateCurrentException() {
    preFulfill();
    this->value = std::current_exception();
    postFulfill(true);
  }

  template <typename Func, typename... Params>
  void callAndFulfill(const Func& func, Params&&... params) {
    try {
      func(std::forward<Params>(params)...);
      fulfill();
    } catch (...) {
      propagateCurrentException();
    }
  }

  Void release() {
    return value.release();
  }

  MaybeException<void> releaseMaybeException() {
    return std::move(value);
  }

private:
  MaybeException<void> value;
};

template <typename T>
struct Unpack {
  typedef T&& Type;
};

template <typename T>
struct Unpack<Promise<T>> {
  typedef T Type;
};

template <>
struct Unpack<Promise<void>> {
  typedef Void Type;
};

template <typename T>
inline T&& unpack(T&& value) {
  return std::forward<T>(value);
}

template <typename T>
inline typename Unpack<Promise<T>>::Type unpack(Promise<T>&& promise) {
  return promise.state->release();
}

template <typename T>
inline T&& unpackMaybeException(T&& value) {
  return std::forward<T>(value);
}

template <typename T>
inline MaybeException<T> unpackMaybeException(Promise<T>&& promise) {
  return promise.state->releaseMaybeException();
}

template <typename T, typename Func, typename ExceptionHandler, typename ParamPack>
class DependentPromiseState : public PromiseState<T> {
private:
  struct DoReadyFunctor {
    template <typename... Params>
    void operator()(const Func& func, DependentPromiseState* state, Params&&... params) const {
      state->callAndFulfill(func, promiseInternal::unpack(std::forward<Params>(params))...);
    }
  };

  struct DoErrorFunctor {
    template <typename... Params>
    void operator()(const ExceptionHandler& exceptionHandler,
                    DependentPromiseState* state, Params&&... params) const {
      state->callAndFulfill(exceptionHandler, promiseInternal::unpackMaybeException(
          std::forward<Params>(params))...);
    }
  };

public:
  DependentPromiseState(Executor* executor, Func&& func,
                        ExceptionHandler&& exceptionHandler, ParamPack&& params)
      : PromiseState<T>(executor, nullptr),
        func(std::move(func)),
        exceptionHandler(std::move(exceptionHandler)),
        params(std::move(params)) {}

  void ready() {
    // Move stuff to stack in case promise is deleted during callback.
    ParamPack localParams(std::move(params));
    Func localFunc(std::move(func));

    localParams.applyMoving(DoReadyFunctor(), localFunc, this);
  }

  void error() {
    // Move stuff to stack in case promise is deleted during callback.
    ParamPack localParams(std::move(this->params));
    ExceptionHandler localExceptionHandler(std::move(this->exceptionHandler));

    localParams.applyMoving(DoErrorFunctor(), localExceptionHandler, this);
  }

  Func func;
  ExceptionHandler exceptionHandler;
  ParamPack params;
};

template <typename ReturnType>
struct ForceErrorFunctor {
  template <typename Head, typename... Tail>
  ReturnType operator()(Head&& head, Tail&&... tail) const {
    return operator()(tail...);
  }
  template <typename T, typename... Tail>
  ReturnType operator()(MaybeException<T>&& head, Tail&&... tail) const {
    head.get();
    return operator()(tail...);
  }
  ReturnType operator()() const {
    throw std::logic_error(
        "Promise implementation failure:  Exception handler called with no exceptions.");
  }
};

}  // namespace promiseInternal

// =======================================================================================

template <typename T>
class Fulfiller;

template <typename T>
class Promise {
public:
  Promise() {}
  Promise(const Promise& other) = delete;
  Promise(Promise&& other) = default;

  Promise& operator=(const Promise& other) = delete;
  Promise& operator=(Promise&& other) {
    state = std::move(other.state);
  }

  T* operator->() const {
    return state->getValue()->operator->();
  }

  Promise<T> release() {
    return std::move(*this);
  }

private:
  typedef promiseInternal::PromiseState<T> State;

  Promise(OwnedPtr<State> state): state(state.release()) {}

  OwnedPtr<State> state;

  friend class Fulfiller<T>;
  friend class Executor;
  template <typename U>
  friend typename promiseInternal::Unpack<Promise<U>>::Type
      promiseInternal::unpack(Promise<U>&& promise);
  template <typename U>
  friend MaybeException<U> promiseInternal::unpackMaybeException(Promise<U>&& promise);
};

template <>
class Promise<void> {
public:
  Promise() {}
  Promise(const Promise& other) = delete;
  Promise(Promise&& other) = default;

  Promise& operator=(const Promise& other) = delete;
  Promise& operator=(Promise&& other) {
    state = std::move(other.state);
    return *this;
  }

  Promise release() {
    return std::move(*this);
  }

private:
  typedef promiseInternal::PromiseState<void> State;

  Promise(OwnedPtr<State> state): state(state.release()) {}

  OwnedPtr<State> state;
  friend class Fulfiller<void>;
  friend class Executor;
  template <typename U>
  friend typename promiseInternal::Unpack<Promise<U>>::Type
      promiseInternal::unpack(Promise<U>&& promise);
  template <typename U>
  friend MaybeException<U> promiseInternal::unpackMaybeException(Promise<U>&& promise);
};

// =======================================================================================

template <typename T>
class Fulfiller {
public:
  Fulfiller(): state(nullptr) {}

  Fulfiller(const Fulfiller& other) = delete;
  Fulfiller& operator=(const Fulfiller& other) = delete;

  Promise<T> makePromise() {
    auto state = newOwned<promiseInternal::PromiseState<T>>(nullptr, &stateLink);
    this->state = state.get();
    return Promise<T>(state.release());
  }

  void fulfill(const T& value) {
    if (stateLink.isEntangled()) {
      state->fulfill(value);
    }
  }

  void fulfill(T&& value) {
    if (stateLink.isEntangled()) {
      state->fulfill(std::move(value));
    }
  }

  void propagateCurrentException() {
    if (stateLink.isEntangled()) {
      state->propagateCurrentException();
    }
  }

private:
  promiseInternal::PromiseState<T>* state;
  WeakLink stateLink;
};

template <>
class Fulfiller<void> {
public:
  Fulfiller(): state(nullptr) {}

  Fulfiller(const Fulfiller& other) = delete;
  Fulfiller& operator=(const Fulfiller& other) = delete;

  Promise<void> makePromise() {
    auto state = newOwned<promiseInternal::PromiseState<void>>(nullptr, &stateLink);
    this->state = state.get();
    return Promise<void>(state.release());
  }

  void fulfill() {
    if (stateLink.isEntangled()) {
      state->fulfill();
    }
    stateLink.entangle(nullptr);
    state = nullptr;
  }

  void propagateCurrentException() {
    if (stateLink.isEntangled()) {
      state->propagateCurrentException();
    }
  }

private:
  promiseInternal::PromiseState<void>* state;
  WeakLink stateLink;
};

// =======================================================================================

template <typename... Types>
class Executor::When {
  typedef ValuePack<Types...> ParamPack;
public:
  When(const When& other) = delete;
  When(When&& other) = default;

  When& operator=(const When& other) = delete;
  When& operator=(When&& other) = delete;

#define RESULT_TYPE \
    decltype(func(std::declval<typename promiseInternal::Unpack<Types>::Type>()...))

  template <typename Func>
  auto operator()(Func&& func) -> Promise<RESULT_TYPE> {
    typedef RESULT_TYPE ResultType;
    return operator()(std::move(func), promiseInternal::ForceErrorFunctor<ResultType>());
  }

  template <typename Func, typename ExceptionHandler>
  auto operator()(Func&& func, ExceptionHandler&& exceptionHandler) -> Promise<RESULT_TYPE> {
    typedef RESULT_TYPE ResultType;

    typedef promiseInternal::DependentPromiseState<
        ResultType, Func, ExceptionHandler, ParamPack> PromiseState;

    OwnedPtr<PromiseState> state = newOwned<PromiseState>(
        executor, std::move(func), std::move(exceptionHandler), std::move(params));
    state->params.apply(AddDependenciesFunctor(), state.get());
    return Promise<ResultType>(state.release());
  }

#undef RESULT_TYPE

private:
  Executor* executor;
  ParamPack params;

  struct AddDependenciesFunctor {
    template <typename First, typename... Rest>
    void operator()(promiseInternal::PromiseStateBase* state,
                    const Promise<First>& first, Rest&&... rest) const {
      state->addDependency(first.state.get());
      operator()(state, std::forward<Rest>(rest)...);
    }

    template <typename First, typename... Rest>
    void operator()(promiseInternal::PromiseStateBase* state, First&& first, Rest&&... rest) const {
      operator()(state, std::forward<Rest>(rest)...);
    }

    void operator()(promiseInternal::PromiseStateBase* state) const {
      state->allDependenciesAdded();
    }
  };

  When(Executor* executor, Types&&... params)
      : executor(executor),
        params(std::move(params)...) {}
  friend class Executor;
};

template <typename... Types>
Executor::When<typename std::remove_reference<Types>::type...> Executor::when(Types&&... params) {
  return When<typename std::remove_reference<Types>::type...>(this, std::move(params)...);
}

}  // namespace ekam

#endif  // EKAM_BASE_PROMISE_H_
