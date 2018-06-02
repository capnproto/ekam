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

#ifndef KENTONSCODE_BASE_PROMISE_H_
#define KENTONSCODE_BASE_PROMISE_H_

#include <set>
#include <vector>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "OwnedPtr.h"
#include "Debug.h"

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
  virtual ~Executor() noexcept(false);

  virtual OwnedPtr<PendingRunnable> runLater(OwnedPtr<Runnable> runnable) = 0;

  template <typename... Types>
  class When;

  template <typename... Types>
  When<typename std::remove_reference<Types>::type...> when(Types&&... params);
};

template <typename Func>
class LambdaRunnable: public Runnable {
public:
  LambdaRunnable(Func&& func): func(std::move(func)) {}
  ~LambdaRunnable() {}

  void run() {
    func();
  }

private:
  Func func;
};

template <typename Func>
OwnedPtr<Runnable> newLambdaRunnable(Func&& func) {
  return newOwned<LambdaRunnable<Func>>(std::move(func));
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

template <typename T>
class PromiseFulfiller {
public:
  typedef T PromiseType;

  class Callback;
};

namespace promiseInternal {

struct PromiseConstructors;

template <typename T, typename Func, typename ExceptionHandler, typename ParamPack>
class DependentPromiseFulfiller;

class PromiseListener {
public:
  virtual void dependencyDone(bool failed) = 0;
};

template <typename T>
class PromiseState {
public:
  PromiseState(): owner(nullptr), listener(nullptr), fulfilled(false), failed(false) {}
  virtual ~PromiseState() {}

  Promise<T>* owner;
  OwnedPtr<PromiseState> chainedPromise;

private:
  PromiseListener* listener;
  bool fulfilled;
  bool failed;
  MaybeException<T> value;

  void setListener(PromiseListener* listener) {
    if (this->listener != nullptr) {
      throw std::invalid_argument("Already waiting on this Promise.");
    }
    this->listener = listener;
    if (fulfilled) {
      listener->dependencyDone(failed);
    }
  }

  void preFulfill() {
    if (fulfilled) {
      throw std::logic_error("Already fulfilled this promise.");
    }
    fulfilled = true;
  }

  void postFulfill(bool failed) {
    this->failed = failed;
    if (listener != nullptr) {
      listener->dependencyDone(failed);
    }
  }

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

  void fulfill(Promise<T> chainedPromise);

  void propagateCurrentException() {
    preFulfill();
    this->value = std::current_exception();
    postFulfill(true);
  }

  T release() {
    return value.release();
  }

  MaybeException<T> releaseMaybeException() {
    return std::move(value);
  }

  template <typename U>
  friend class PromiseFulfiller<U>::Callback;
  template <typename U, typename Func, typename ExceptionHandler, typename ParamPack>
  friend class DependentPromiseFulfiller;
  friend struct PromiseConstructors;
};

template <>
class PromiseState<void> {
public:
  PromiseState(): owner(nullptr), listener(nullptr), fulfilled(false), failed(false) {}
  virtual ~PromiseState() {}

  Promise<void>* owner;
  OwnedPtr<PromiseState> chainedPromise;

private:
  PromiseListener* listener;
  bool fulfilled;
  bool failed;
  MaybeException<void> value;

  void setListener(PromiseListener* listener) {
    if (this->listener != nullptr) {
      throw std::invalid_argument("Already waiting on this Promise.");
    }
    this->listener = listener;
    if (fulfilled) {
      listener->dependencyDone(failed);
    }
  }

  void preFulfill() {
    if (fulfilled) {
      throw std::logic_error("Already fulfilled this promise.");
    }
    fulfilled = true;
  }

  void postFulfill(bool failed) {
    this->failed = failed;
    if (listener != nullptr) {
      listener->dependencyDone(failed);
    }
  }

  void fulfill() {
    preFulfill();
    postFulfill(false);
  }

  void fulfill(Promise<void> chainedPromise);

  void propagateCurrentException() {
    preFulfill();
    this->value = std::current_exception();
    postFulfill(true);
  }

  Void release() {
    return value.release();
  }

  MaybeException<void> releaseMaybeException() {
    return std::move(value);
  }

  friend class PromiseFulfiller<void>::Callback;
  template <typename U, typename Func, typename ExceptionHandler, typename ParamPack>
  friend class DependentPromiseFulfiller;
  friend struct PromiseConstructors;
};

template <typename T>
struct Unpack {
  typedef T Type;
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
struct Chain {
  typedef T Type;
};

template <typename T>
struct Chain<Promise<T>> {
  typedef T Type;
};

// =======================================================================================

template <typename T, typename PromiseFulfillerImpl>
class FulfillerPromiseState;

}  // namespace promiseInternal

template <typename T>
class PromiseFulfiller<T>::Callback {
public:
  template <typename U>
  void fulfill(U&& value) {
    state->fulfill(std::forward<U>(value));
  }

  void propagateCurrentException() {
    state->propagateCurrentException();
  }

private:
  promiseInternal::PromiseState<T>* state;

  Callback(promiseInternal::PromiseState<T>* state): state(state) {}
  ~Callback() {}

  template <typename U, typename PromiseFulfillerImpl>
  friend class promiseInternal::FulfillerPromiseState;
};

template <>
class PromiseFulfiller<void>::Callback {
public:
  void fulfill() {
    state->fulfill();
  }

  void fulfill(Promise<void> chain);

  void propagateCurrentException() {
    state->propagateCurrentException();
  }

private:
  promiseInternal::PromiseState<void>* state;

  Callback(promiseInternal::PromiseState<void>* state): state(state) {}
  ~Callback() {}

  template <typename U, typename PromiseFulfillerImpl>
  friend class promiseInternal::FulfillerPromiseState;
};

namespace promiseInternal {

template <typename T, typename PromiseFulfillerImpl>
class FulfillerPromiseState : public PromiseState<T> {
public:
  template <typename... Params>
  FulfillerPromiseState(Params&&... params)
      : PromiseState<T>(),
        callback(this),
        fulfillerImpl(&callback, std::forward<Params>(params)...) {}
  virtual ~FulfillerPromiseState() {}

private:
  typename PromiseFulfiller<T>::Callback callback;
  PromiseFulfillerImpl fulfillerImpl;
};

// =======================================================================================

template <typename ReturnType>
struct CallAndFulfillFunctor {
  template <typename Callback, typename Func2, typename... Params>
  void operator()(WeakLink* linkToFulfiller, Callback* callback,
                  Func2& func, Params&&... params) const {
    try {
      auto result = func(std::forward<Params>(params)...);
      if (linkToFulfiller->isEntangled()) {
        callback->fulfill(std::move(result));
      }
    } catch (...) {
      callback->propagateCurrentException();
    }
  }
};

template <>
struct CallAndFulfillFunctor<void> {
  template <typename Callback, typename Func2, typename... Params>
  void operator()(WeakLink* linkToFulfiller, Callback* callback,
                  Func2& func, Params&&... params) const {
    try {
      func(std::forward<Params>(params)...);
      if (linkToFulfiller->isEntangled()) {
        callback->fulfill();
      }
    } catch (...) {
      callback->propagateCurrentException();
    }
  }
};

template <typename T, typename Func, typename ExceptionHandler, typename ParamPack>
class DependentPromiseFulfiller : public PromiseFulfiller<T>, public PromiseListener {
public:
  typedef typename PromiseFulfiller<T>::Callback Callback;

  DependentPromiseFulfiller(Callback* callback, Executor* executor, Func&& func,
                            ExceptionHandler&& exceptionHandler, ParamPack&& params)
      : callback(callback),
        executor(executor),
        func(std::move(func)),
        exceptionHandler(std::move(exceptionHandler)),
        params(std::move(params)),
        dependencyFailed(false),
        dependenciesLeft(1) {
    addAllDependencies();
  }

  ~DependentPromiseFulfiller() {
    if (pendingReadyLater != nullptr) {
      DEBUG_INFO << "Promise canceled: " << this;
    }
  }

  void dependencyDone(bool failed) {
    if (failed) {
      dependencyFailed = true;
    }
    if (--dependenciesLeft == 0) {
      readyLater();
    }
  }

  Callback* callback;
  Executor* executor;
  Func func;
  ExceptionHandler exceptionHandler;
  ParamPack params;

private:
  bool dependencyFailed;
  int dependenciesLeft;
  OwnedPtr<PendingRunnable> pendingReadyLater;
  WeakLink weakLink;

  struct AddDependenciesFunctor {
    template <typename First, typename... Rest>
    void operator()(DependentPromiseFulfiller* fulfiller, const Promise<First>& first,
                    Rest&&... rest) const {
      ++fulfiller->dependenciesLeft;
      first.state->setListener(fulfiller);
      operator()(fulfiller, std::forward<Rest>(rest)...);
    }

    template <typename First, typename... Rest>
    void operator()(DependentPromiseFulfiller* fulfiller, First&& first, Rest&&... rest) const {
      operator()(fulfiller, std::forward<Rest>(rest)...);
    }

    void operator()(DependentPromiseFulfiller* fulfiller) const {
      if (--fulfiller->dependenciesLeft == 0) {
        fulfiller->readyLater();
      }
    }
  };

  void addAllDependencies() {
    params.apply(AddDependenciesFunctor(), this);
  }

  template <typename U>
  static U&& unpack(U&& value) {
    return std::forward<U>(value);
  }

  template <typename U>
  static typename Unpack<Promise<U>>::Type unpack(Promise<U>&& promise) {
    return promise.state->release();
  }

  struct DoReadyFunctor {
    template <typename... Params>
    void operator()(WeakLink* linkToFulfiller, Callback* callback,
                    Func& func, Params&&... params) const {
      CallAndFulfillFunctor<decltype(func(unpack(std::forward<Params>(params))...))>()(
          linkToFulfiller, callback, func, unpack(std::forward<Params>(params))...);
    }
  };

  void ready() {
    // Move stuff to stack in case promise is deleted during callback.
    ParamPack localParams(std::move(params));
    Func localFunc(std::move(func));

    WeakLink linkToFulfiller(&weakLink);
    localParams.applyMoving(DoReadyFunctor(), &linkToFulfiller, callback, localFunc);
  }

  template <typename U>
  static U&& unpackMaybeException(U&& value) {
    return std::forward<U>(value);
  }

  template <typename U>
  static MaybeException<U> unpackMaybeException(Promise<U>&& promise) {
    return promise.state->releaseMaybeException();
  }

  struct DoErrorFunctor {
    template <typename... Params>
    void operator()(WeakLink* linkToFulfiller, Callback* callback,
                    const ExceptionHandler& exceptionHandler, Params&&... params) const {
      CallAndFulfillFunctor<decltype(
          exceptionHandler(unpackMaybeException(std::forward<Params>(params))...))>()(
            linkToFulfiller, callback, exceptionHandler,
            unpackMaybeException(std::forward<Params>(params))...);
    }
  };

  void error() {
    // Move stuff to stack in case promise is deleted during callback.
    ParamPack localParams(std::move(this->params));
    ExceptionHandler localExceptionHandler(std::move(this->exceptionHandler));

    WeakLink linkToFulfiller(&weakLink);
    localParams.applyMoving(DoErrorFunctor(), &linkToFulfiller, callback, localExceptionHandler);
  }

  void readyLater() {
    pendingReadyLater = executor->runLater(newLambdaRunnable(
      [this]() {
        auto objectToDeleteOnReturn = this->pendingReadyLater.release();
        if (this->dependencyFailed) {
          this->error();
        } else {
          this->ready();
        }
      }));
  }
};

// =======================================================================================

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
class Promise {
public:
  Promise() {}
  Promise(const Promise& other) = delete;
  Promise(Promise&& other): state(other.state.release()) {
    if (state != nullptr) {
      state->owner = this;
    }
  }

  Promise& operator=(const Promise& other) = delete;
  Promise& operator=(Promise&& other) {
    state = other.state.release();
    state->owner = this;
    return *this;
  }

  bool operator==(std::nullptr_t) {
    return state == nullptr;
  }
  bool operator!=(std::nullptr_t) {
    return state != nullptr;
  }

  T* operator->() const {
    return state->getValue()->operator->();
  }

  Promise<T> release() {
    return std::move(*this);
  }

private:
  typedef promiseInternal::PromiseState<T> State;

  explicit Promise(OwnedPtr<State> state): state(state.release()) {
    while (this->state->chainedPromise != nullptr) {
      this->state = this->state->chainedPromise.release();
    }
    this->state->owner = this;
  }

  OwnedPtr<State> state;

  friend class Executor;
  friend struct promiseInternal::PromiseConstructors;
  template <typename U, typename Func, typename ExceptionHandler, typename ParamPack>
  friend class promiseInternal::DependentPromiseFulfiller;
  friend class promiseInternal::PromiseState<T>;
};

inline void PromiseFulfiller<void>::Callback::fulfill(Promise<void> chain) {
  state->fulfill(chain.release());
}

namespace promiseInternal {

template <typename T>
void PromiseState<T>::fulfill(Promise<T> chainedPromise) {
  auto listener = this->listener;
  auto owner = this->owner;

  if (owner == nullptr) {
    throw std::logic_error(
        "Not supported: Calling fulfill() with a chained promise from the PromiseFulfiller's "
        "constructor.  This is probably not what you intended anyway.");
  } else {
    *owner = chainedPromise.release();  // will delete this!
    if (listener != nullptr) {
      owner->state->setListener(listener);
    }
  }
}

inline void PromiseState<void>::fulfill(Promise<void> chainedPromise) {
  auto listener = this->listener;
  auto owner = this->owner;

  if (owner == nullptr) {
    throw std::logic_error(
        "Not supported: Calling fulfill() with a chained promise from the PromiseFulfiller's "
        "constructor.  This is probably not what you intended anyway.");
  } else {
    *owner = chainedPromise.release();  // will delete this!
    if (listener != nullptr) {
      owner->state->setListener(listener);
    }
  }
}

struct PromiseConstructors {
  template <typename PromiseFulfillerImpl, typename... Params>
  static Promise<typename PromiseFulfillerImpl::PromiseType> newPromise(Params&&... params) {
    typedef typename PromiseFulfillerImpl::PromiseType T;
    return Promise<T>(newOwned<promiseInternal::FulfillerPromiseState<T, PromiseFulfillerImpl>>(
        std::forward<Params>(params)...));
  }

  template <typename T>
  static Promise<typename std::remove_reference<T>::type> newFulfilledPromise(T&& value) {
    Promise<T> result(newOwned<promiseInternal::PromiseState<T>>());
    result.state->fulfill(std::forward<T>(value));
    return result;
  }

  static Promise<void> newFulfilledPromise() {
    Promise<void> result(newOwned<promiseInternal::PromiseState<void>>());
    result.state->fulfill();
    return result;
  }

  template <typename T>
  static Promise<T> newPromiseFromCurrentException() {
    Promise<T> result(newOwned<promiseInternal::PromiseState<T>>());
    result.state->propagateCurrentException();
    return result;
  }
};

}  // namespace promiseInternal

template <typename PromiseFulfillerImpl, typename... Params>
Promise<typename PromiseFulfillerImpl::PromiseType> newPromise(Params&&... params) {
  return promiseInternal::PromiseConstructors::newPromise<PromiseFulfillerImpl, Params...>(
      std::forward<Params>(params)...);
}

template <typename T>
Promise<typename std::remove_reference<T>::type> newFulfilledPromise(T&& value) {
  return promiseInternal::PromiseConstructors::newFulfilledPromise(std::forward<T>(value));
}

inline Promise<void> newFulfilledPromise() {
  return promiseInternal::PromiseConstructors::newFulfilledPromise();
}

template <typename T>
Promise<T> newPromiseFromCurrentException() {
  return promiseInternal::PromiseConstructors::newPromiseFromCurrentException<T>();
}

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
    typename promiseInternal::Chain< \
      decltype(func(std::declval<typename promiseInternal::Unpack<Types>::Type>()...))>::Type

  template <typename Func>
  auto operator()(Func&& func) -> Promise<RESULT_TYPE> {
    typedef RESULT_TYPE ResultType;
    return operator()(std::move(func), promiseInternal::ForceErrorFunctor<ResultType>());
  }

  template <typename Func, typename ExceptionHandler>
  auto operator()(Func&& func, ExceptionHandler&& exceptionHandler) -> Promise<RESULT_TYPE> {
    typedef RESULT_TYPE ResultType;

    typedef promiseInternal::DependentPromiseFulfiller<
        ResultType, Func, ExceptionHandler, ParamPack> Fulfiller;

    return newPromise<Fulfiller>(
        executor, std::move(func), std::move(exceptionHandler), std::move(params));
  }

#undef RESULT_TYPE

private:
  Executor* executor;
  ParamPack params;

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

#endif  // KENTONSCODE_BASE_PROMISE_H_
