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

#ifndef EKAM_BASE_OWNEDPTR_H_
#define EKAM_BASE_OWNEDPTR_H_

#include <stddef.h>
#include <tr1/type_traits>
#include <vector>
#include <deque>
#include <queue>
#include <tr1/unordered_map>
#include <assert.h>

#ifdef __CDT_PARSER__
#define nullptr 0
namespace std { struct nullptr_t; }
#endif

namespace ekam {

template <typename T>
inline void deleteEnsuringCompleteType(T* ptr) {
  enum { type_must_be_complete = sizeof(T) };
  delete ptr;
}

template <typename T>
class OwnedPtr {
public:
  OwnedPtr() : ptr(NULL) {}
  OwnedPtr(const OwnedPtr&) = delete;
  OwnedPtr(OwnedPtr&& other) : ptr(other.releaseRaw()) {}
  template <typename U>
  OwnedPtr(OwnedPtr<U>&& other) : ptr(other.releaseRaw()) {}
  OwnedPtr(std::nullptr_t) : ptr(NULL) {}
  ~OwnedPtr() {
    deleteEnsuringCompleteType(ptr);
  }

  OwnedPtr& operator=(const OwnedPtr&) = delete;
  OwnedPtr& operator=(OwnedPtr&& other) {
    reset(other.releaseRaw());
    return *this;
  }

  template <typename U>
  OwnedPtr& operator=(OwnedPtr<U>&& other) {
    reset(other.releaseRaw());
    return *this;
  }

  T* get() const { return ptr; }
  T* operator->() const { assert(ptr != NULL); return ptr; }
  T& operator*() const { assert(ptr != NULL); return *ptr; }

  OwnedPtr release() {
    return OwnedPtr(releaseRaw());
  }

  void clear() {
    reset(NULL);
  }

  bool operator==(const T* other) { return ptr == other; }
  bool operator!=(const T* other) { return ptr != other; }

private:
  T* ptr;

  explicit OwnedPtr(T* ptr) : ptr(ptr) {}

  void reset(T* newValue) {
    T* oldValue = ptr;
    ptr = newValue;
    deleteEnsuringCompleteType(oldValue);
  }

  T* releaseRaw() {
    T* result = ptr;
    ptr = NULL;
    return result;
  }

  template <typename U>
  friend class OwnedPtr;
  template <typename U, typename... Params>
  friend OwnedPtr<U> newOwned(Params&&... params);
  template <typename U>
  friend class SmartPtr;
  template <typename U>
  friend class OwnedPtrVector;
  template <typename U>
  friend class OwnedPtrDeque;
  template <typename U>
  friend class OwnedPtrQueue;
  template <typename Key, typename U, typename HashFunc, typename EqualsFunc>
  friend class OwnedPtrMap;
};

template <typename T, typename... Params>
OwnedPtr<T> newOwned(Params&&... params) {
  return OwnedPtr<T>(new T(std::forward<Params>(params)...));
}

// TODO:  Hide this somewhere private?
class Refcount {
public:
  Refcount(): strong(1), weak(0) {}
  Refcount(const Refcount& other) = delete;
  Refcount& operator=(const Refcount& other) = delete;

  void inc() {
    if (this != NULL) ++strong;
  }
  bool dec() {
    if (this == NULL) {
      return false;
    } else if (--strong == 0) {
      if (weak == 0) {
        delete this;
      }
      return true;
    } else {
      return false;
    }
  }

  void incWeak() {
    if (this != NULL) ++weak;
  }
  void decWeak() {
    if (this != NULL && --weak == 0 && strong == 0) {
      delete this;
    }
  }

  bool release() {
    if (this != NULL && strong == 1) {
      dec();
      return true;
    } else {
      return false;
    }
  }

  bool isLive() {
    return this != NULL && strong > 0;
  }

  bool isOnlyReference() {
    return strong == 1;
  }

private:
  int strong;
  int weak;
};

template <typename T>
class SmartPtr {
public:
  SmartPtr() : ptr(NULL), refcount(NULL) {}
  SmartPtr(std::nullptr_t) : ptr(NULL), refcount(NULL) {}
  ~SmartPtr() {
    if (refcount->dec()) {
      deleteEnsuringCompleteType(ptr);
    }
  }

  SmartPtr(const SmartPtr& other) : ptr(other.ptr), refcount(other.refcount) {
    refcount->inc();
  }
  template <typename U>
  SmartPtr(const SmartPtr<U>& other) : ptr(other.ptr), refcount(other.refcount) {
    refcount->inc();
  }
  SmartPtr& operator=(const SmartPtr& other) {
    reset(other.ptr, other.refcount);
    return *this;
  }
  template <typename U>
  SmartPtr& operator=(const SmartPtr<U>& other) {
    reset(other.ptr, other.refcount);
    return *this;
  }

  SmartPtr(SmartPtr&& other) : ptr(other.ptr), refcount(other.refcount) {
    other.ptr = NULL;
    other.refcount = NULL;
  }
  template <typename U>
  SmartPtr(SmartPtr<U>&& other) : ptr(other.ptr), refcount(other.refcount) {
    other.ptr = NULL;
    other.refcount = NULL;
  }
  SmartPtr& operator=(SmartPtr&& other) {
    // Move pointers to locals before reset() in case &other == this.
    T* tempPtr = other.ptr;
    Refcount* tempRefcount = other.refcount;
    other.ptr = NULL;
    other.refcount = NULL;

    reset(NULL, NULL);

    ptr = tempPtr;
    refcount = tempRefcount;
    return *this;
  }
  template <typename U>
  SmartPtr& operator=(SmartPtr<U>&& other) {
    // Move pointers to locals before reset() in case &other == this.
    T* tempPtr = other.ptr;
    Refcount* tempRefcount = other.refcount;
    other.ptr = NULL;
    other.refcount = NULL;

    reset(NULL, NULL);

    ptr = tempPtr;
    refcount = tempRefcount;
    return *this;
  }

  template <typename U>
  SmartPtr(OwnedPtr<U>&& other)
      : ptr(other.releaseRaw()),
        refcount(ptr == NULL ? NULL : new Refcount()) {}
  template <typename U>
  SmartPtr& operator=(OwnedPtr<U>&& other) {
    reset(other.releaseRaw());
    return *this;
  }

  T* get() const { return ptr; }
  T* operator->() const { assert(ptr != NULL); return ptr; }
  T& operator*() const { assert(ptr != NULL); return *ptr; }

  template <typename U>
  bool release(OwnedPtr<U>* other) {
    if (refcount->release()) {
      other->reset(ptr);
      ptr = NULL;
      return true;
    } else {
      return false;
    }
  }

  bool isOnlyReference() {
    return refcount->isOnlyReference();
  }

  void clear() {
    reset(NULL);
  }

  bool operator==(const T* other) { return ptr == other; }
  bool operator!=(const T* other) { return ptr != other; }
  bool operator==(const SmartPtr<T>& other) { return ptr == other.ptr; }
  bool operator!=(const SmartPtr<T>& other) { return ptr != other.ptr; }

  void allocate() {
    reset(new T());
  }
  template <typename P1>
  void allocate(const P1& p1) {
    reset(new T(p1));
  }
  template <typename P1, typename P2>
  void allocate(const P1& p1, const P2& p2) {
    reset(new T(p1, p2));
  }
  template <typename P1, typename P2, typename P3>
  void allocate(const P1& p1, const P2& p2, const P3& p3) {
    reset(new T(p1, p2, p3));
  }
  template <typename P1, typename P2, typename P3, typename P4>
  void allocate(const P1& p1, const P2& p2, const P3& p3, const P4& p4) {
    reset(new T(p1, p2, p3, p4));
  }

  template <typename Sub>
  void allocateSubclass() {
    reset(new Sub());
  }
  template <typename Sub, typename P1>
  void allocateSubclass(const P1& p1) {
    reset(new Sub(p1));
  }
  template <typename Sub, typename P1, typename P2>
  void allocateSubclass(const P1& p1, const P2& p2) {
    reset(new Sub(p1, p2));
  }
  template <typename Sub, typename P1, typename P2, typename P3>
  void allocateSubclass(const P1& p1, const P2& p2, const P3& p3) {
    reset(new Sub(p1, p2, p3));
  }
  template <typename Sub, typename P1, typename P2, typename P3, typename P4>
  void allocateSubclass(const P1& p1, const P2& p2, const P3& p3, const P4& p4) {
    reset(new Sub(p1, p2, p3, p4));
  }

private:
  T* ptr;
  Refcount* refcount;

  inline void reset(T* newValue) {
    reset(newValue, newValue == NULL ? NULL : new Refcount());
    refcount->dec();
  }

  void reset(T* newValue, Refcount* newRefcount) {
    T* oldValue = ptr;
    Refcount* oldRefcount = refcount;
    ptr = newValue;
    refcount = newRefcount;
    refcount->inc();
    if (oldRefcount->dec()) {
      deleteEnsuringCompleteType(oldValue);
    }
  }

  template <typename U>
  friend class SmartPtr;
  template <typename U>
  friend class WeakPtr;
  template <typename U>
  friend class OwnedPtr;
  template <typename U>
  friend class OwnedPtrVector;
  template <typename U>
  friend class OwnedPtrQueue;
  template <typename Key, typename U, typename HashFunc, typename EqualsFunc>
  friend class OwnedPtrMap;
};

template <typename T>
class WeakPtr {
public:
  WeakPtr(): ptr(NULL), refcount(NULL) {}
  WeakPtr(const WeakPtr& other): ptr(other.ptr), refcount(other.refcount) {
    refcount->incWeak();
  }
  WeakPtr(const SmartPtr<T>& other): ptr(other.ptr), refcount(other.refcount) {
    refcount->incWeak();
  }
  WeakPtr(std::nullptr_t): ptr(nullptr), refcount(nullptr) {}
  ~WeakPtr() {
    refcount->decWeak();
  }

  WeakPtr& operator=(const WeakPtr& other) {
    Refcount* oldRefcount = refcount;
    ptr = other.ptr;
    refcount = other.refcount;
    refcount->incWeak();
    oldRefcount->decWeak();
    return *this;
  }
  template <typename U>
  WeakPtr& operator=(const WeakPtr<U>& other) {
    Refcount* oldRefcount = refcount;
    ptr = other.ptr;
    refcount = other.refcount;
    refcount->incWeak();
    oldRefcount->decWeak();
    return *this;
  }
  template <typename U>
  WeakPtr& operator=(const SmartPtr<U>& other) {
    Refcount* oldRefcount = refcount;
    ptr = other.ptr;
    refcount = other.refcount;
    refcount->incWeak();
    oldRefcount->decWeak();
    return *this;
  }
  WeakPtr& operator=(std::nullptr_t) {
    refcount->decWeak();
    ptr = nullptr;
    refcount = nullptr;
    return *this;
  }

  template <typename U>
  operator SmartPtr<U>() const {
    SmartPtr<U> result;
    if (refcount->isLive()) {
      result.reset(ptr, refcount);
    }
    return result;
  }

private:
  T* ptr;
  Refcount* refcount;
};

template <typename T>
class OwnedPtrVector {
public:
  OwnedPtrVector() {}
  OwnedPtrVector(const OwnedPtrVector&) = delete;
  ~OwnedPtrVector() {
    for (typename std::vector<T*>::const_iterator iter = vec.begin(); iter != vec.end(); ++iter) {
      deleteEnsuringCompleteType(*iter);
    }
  }

  OwnedPtrVector& operator=(const OwnedPtrVector&) = delete;

  int size() const { return vec.size(); }
  T* get(int index) const { return vec[index]; }
  bool empty() const { return vec.empty(); }

  void add(OwnedPtr<T> ptr) {
    vec.push_back(ptr.releaseRaw());
  }

  void set(int index, OwnedPtr<T> ptr) {
    deleteEnsuringCompleteType(vec[index]);
    vec[index] = ptr->releaseRaw();
  }

  OwnedPtr<T> release(int index) {
    T* result = vec[index];
    vec[index] = NULL;
    return OwnedPtr<T>(result);
  }

  OwnedPtr<T> releaseBack() {
    T* result = vec.back();
    vec.pop_back();
    return OwnedPtr<T>(result);
  }

  OwnedPtr<T> releaseAndShift(int index) {
    T* result = vec[index];
    vec.erase(vec.begin() + index);
    return OwnedPtr<T>(result);
  }

  void clear() {
    for (typename std::vector<T*>::const_iterator iter = vec.begin(); iter != vec.end(); ++iter) {
      deleteEnsuringCompleteType(*iter);
    }
    vec.clear();
  }

  void swap(OwnedPtrVector* other) {
    vec.swap(other->vec);
  }

  class Appender {
  public:
    explicit Appender(OwnedPtrVector* vec) : vec(vec) {}

    void add(OwnedPtr<T> ptr) {
      vec->add(ptr.release());
    }

  private:
    OwnedPtrVector* vec;
  };

  Appender appender() {
    return Appender(this);
  }

private:
  std::vector<T*> vec;
};

template <typename T>
class OwnedPtrDeque {
public:
  OwnedPtrDeque() {}
  ~OwnedPtrDeque() {
    for (typename std::deque<T*>::const_iterator iter = q.begin(); iter != q.end(); ++iter) {
      deleteEnsuringCompleteType(*iter);
    }
  }

  int size() const { return q.size(); }
  T* get(int index) const { return q[index]; }
  bool empty() const { return q.empty(); }

  void pushFront(OwnedPtr<T> ptr) {
    q.push_front(ptr.releaseRaw());
  }

  OwnedPtr<T> popFront() {
    T* ptr = q.front();
    q.pop_front();
    return OwnedPtr<T>(ptr);
  }

  void pushBack(OwnedPtr<T> ptr) {
    q.push_back(ptr.releaseRaw());
  }

  OwnedPtr<T> popBack() {
    T* ptr = q.back();
    q.pop_back();
    return OwnedPtr<T>(ptr);
  }

  OwnedPtr<T> releaseAndShift(int index) {
    T* ptr = q[index];
    q.erase(q.begin() + index);
    return OwnedPtr<T>(ptr);
  }

  void clear() {
    for (typename std::deque<T*>::const_iterator iter = q.begin(); iter != q.end(); ++iter) {
      deleteEnsuringCompleteType(*iter);
    }
    q.clear();
  }

  void swap(OwnedPtrDeque* other) {
    q.swap(other->q);
  }

private:
  std::deque<T*> q;
};

template <typename T>
class OwnedPtrQueue {
public:
  OwnedPtrQueue() {}
  ~OwnedPtrQueue() {
    clear();
  }

  int size() const { return q.size(); }
  bool empty() const { return q.empty(); }

  void push(OwnedPtr<T> ptr) {
    q.push(ptr.releaseRaw());
  }

  OwnedPtr<T> pop() {
    T* ptr = q.front();
    q.pop();
    return OwnedPtr<T>(ptr);
  }

  void clear() {
    while (!q.empty()) {
      deleteEnsuringCompleteType(q.front());
      q.pop();
    }
  }

  class Appender {
  public:
    Appender(OwnedPtrQueue* q) : q(q) {}

    void add(OwnedPtr<T> ptr) {
      q->push(ptr);
    }

  private:
    OwnedPtrQueue* q;
  };

  Appender appender() {
    return Appender(this);
  }

private:
  std::queue<T*> q;
};

template <typename Key, typename T,
          typename HashFunc = std::tr1::hash<Key>,
          typename EqualsFunc = std::equal_to<Key> >
class OwnedPtrMap {
  typedef std::tr1::unordered_map<Key, T*, HashFunc, EqualsFunc> InnerMap;

public:
  OwnedPtrMap() {}
  ~OwnedPtrMap() {
    for (typename InnerMap::const_iterator iter = map.begin();
         iter != map.end(); ++iter) {
      deleteEnsuringCompleteType(iter->second);
    }
  }

  bool empty() const {
    return map.empty();
  }

  int size() const {
    return map.size();
  }

  bool contains(const Key& key) const {
    return map.count(key) > 0;
  }

  T* get(const Key& key) const {
    typename InnerMap::const_iterator iter = map.find(key);
    if (iter == map.end()) {
      return NULL;
    } else {
      return iter->second;
    }
  }

  void add(const Key& key, OwnedPtr<T> ptr) {
    T* value = ptr.releaseRaw();
    std::pair<typename InnerMap::iterator, bool> insertResult =
        map.insert(std::make_pair(key, value));
    if (!insertResult.second) {
      deleteEnsuringCompleteType(insertResult.first->second);
      insertResult.first->second = value;
    }
  }

  bool addIfNew(const Key& key, OwnedPtr<T> ptr) {
    T* value = ptr.releaseRaw();
    std::pair<typename InnerMap::iterator, bool> insertResult =
        map.insert(std::make_pair(key, value));
    if (insertResult.second) {
      return true;
    } else {
      deleteEnsuringCompleteType(value);
      return false;
    }
  }

  bool release(const Key& key, OwnedPtr<T>* output) {
    typename InnerMap::iterator iter = map.find(key);
    if (iter == map.end()) {
      output->reset(NULL);
      return false;
    } else {
      output->reset(iter->second);
      map.erase(iter);
      return true;
    }
  }

  void releaseAll(typename OwnedPtrVector<T>::Appender output) {
    for (typename InnerMap::const_iterator iter = map.begin();
         iter != map.end(); ++iter) {
      output.add(OwnedPtr<T>(iter->second));
    }
    map.clear();
  }

  bool erase(const Key& key) {
    typename InnerMap::iterator iter = map.find(key);
    if (iter == map.end()) {
      return false;
    } else {
      deleteEnsuringCompleteType(iter->second);
      map.erase(iter);
      return true;
    }
  }

  void clear() {
    for (typename InnerMap::const_iterator iter = map.begin();
         iter != map.end(); ++iter) {
      deleteEnsuringCompleteType(iter->second);
    }
    map.clear();
  }

  void swap(OwnedPtrMap* other) {
    map.swap(other->map);
  }

  class Iterator {
  public:
    Iterator(const OwnedPtrMap& map)
      : nextIter(map.map.begin()),
        end(map.map.end()) {}

    bool next() {
      if (nextIter == end) {
        return false;
      } else {
        iter = nextIter;
        ++nextIter;
        return true;
      }
    }

    const Key& key() {
      return iter->first;
    }

    T* value() {
      return iter->second;
    }

  private:
    typename InnerMap::const_iterator iter;
    typename InnerMap::const_iterator nextIter;
    typename InnerMap::const_iterator end;
  };

private:
  InnerMap map;
};

}  // namespace ekam

#endif  // EKAM_BASE_OWNEDPTR_H_
