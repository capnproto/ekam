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

#ifndef EKAM_OWNEDPTR_H_
#define EKAM_OWNEDPTR_H_

#include <stddef.h>
#include <tr1/type_traits>
#include <vector>
#include <queue>
#include <tr1/unordered_map>
#include <assert.h>

namespace ekam {

template <typename T>
class OwnedPtr {
public:
  OwnedPtr() : ptr(NULL) {}
  ~OwnedPtr() { delete ptr; }

  T* get() const { return ptr; }
  T* operator->() const { assert(ptr != NULL); return ptr; }
  T& operator*() const { assert(ptr != NULL); return *ptr; }

  template <typename U>
  void adopt(OwnedPtr<U>* other) {
    reset(other->release());
  }

  void clear() {
    reset(NULL);
  }

  bool operator==(const T* other) { return ptr == other; }
  bool operator!=(const T* other) { return ptr != other; }

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
  template <typename P1, typename P2, typename P3, typename P4, typename P5>
  void allocate(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5) {
    reset(new T(p1, p2, p3, p4, p5));
  }
  template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
  void allocate(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5,
                const P6& p6) {
    reset(new T(p1, p2, p3, p4, p5, p6));
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

  OwnedPtr(OwnedPtr&);
  OwnedPtr& operator=(OwnedPtr&);

  void reset(T* newValue) {
    T* oldValue = ptr;
    ptr = newValue;
    delete oldValue;
  }

  T* release() {
    T* result = ptr;
    ptr = NULL;
    return result;
  }

  template <typename U>
  friend class OwnedPtr;
  template <typename U>
  friend class SmartPtr;
  template <typename U>
  friend class OwnedPtrVector;
  template <typename U>
  friend class OwnedPtrQueue;
  template <typename Key, typename U, typename HashFunc, typename EqualsFunc>
  friend class OwnedPtrMap;
};

template <typename T>
class SmartPtr {
public:
  SmartPtr() : ptr(NULL), refcount(NULL) {}
  ~SmartPtr() {
    if (refcount != NULL && --*refcount == 0) {
      delete ptr;
    }
  }

  SmartPtr(const SmartPtr& other) : ptr(other.ptr), refcount(other.refcount) {
    if (refcount != NULL) ++*refcount;
  }
  SmartPtr& operator=(const SmartPtr& other) {
    reset(other.ptr, other.refcount);
    return this;
  }

  T* get() const { return ptr; }
  T* operator->() const { assert(ptr != NULL); return ptr; }
  T& operator*() const { assert(ptr != NULL); return *ptr; }

  template <typename U>
  void adopt(OwnedPtr<U>* other) {
    reset(other->release());
  }

  template <typename U>
  bool release(OwnedPtr<U>* other) {
    if (*refcount != 1) {
      return false;
    }

    T* result = ptr;
    ptr = NULL;
    delete refcount;
    other->reset(result);
    return true;
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
  int* refcount;

  inline void reset(T* newValue) {
    reset(newValue, newValue == NULL ? NULL : new int(0));
  }

  void reset(T* newValue, int* newRefcount) {
    T* oldValue = ptr;
    int* oldRefcount = refcount;
    ptr = newValue;
    refcount = newRefcount;
    if (refcount != NULL) ++*refcount;
    if (oldRefcount != NULL && --*oldRefcount == 0) {
      delete oldRefcount;
      delete oldValue;
    }
  }

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
class OwnedPtrVector {
public:
  OwnedPtrVector() {}
  ~OwnedPtrVector() {
    for (typename std::vector<T*>::const_iterator iter = vec.begin(); iter != vec.end(); ++iter) {
      delete *iter;
    }
  }

  int size() const { return vec.size(); }
  T* get(int index) const { return vec[index]; }
  bool empty() const { return vec.empty(); }

  void adopt(int index, OwnedPtr<T>* ptr) {
    delete vec[index];
    vec[index] = ptr->release();
  }

  void release(int index, OwnedPtr<T>* output) {
    output->reset(vec[index]);
    vec[index] = NULL;
  }

  void adoptBack(OwnedPtr<T>* ptr) {
    vec.push_back(ptr->release());
  }

  void releaseBack(OwnedPtr<T>* output) {
    output->reset(vec.back());
    vec.pop_back();
  }

  void releaseAndShift(int index, OwnedPtr<T>* output) {
    output->reset(vec[index]);
    vec.erase(vec.begin() + index);
  }

  void clear() {
    for (typename std::vector<T*>::const_iterator iter = vec.begin(); iter != vec.end(); ++iter) {
      delete *iter;
    }
    vec.clear();
  }

  void swap(OwnedPtrVector* other) {
    vec.swap(other->vec);
  }

  class Appender {
  public:
    explicit Appender(OwnedPtrVector* vec) : vec(vec) {}

    void adopt(OwnedPtr<T>* ptr) {
      vec->adoptBack(ptr);
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
class OwnedPtrQueue {
public:
  OwnedPtrQueue() {}
  ~OwnedPtrQueue() {
    clear();
  }

  int size() const { return q.size(); }
  bool empty() const { return q.empty(); }

  void adopt(OwnedPtr<T>* ptr) {
    q.push(ptr->release());
  }

  void release(OwnedPtr<T>* ptr) {
    ptr->reset(q.front());
    q.pop();
  }

  void clear() {
    while (!q.empty()) {
      delete q.front();
      q.pop();
    }
  }

  class Appender {
  public:
    Appender(OwnedPtrQueue* q) : q(q) {}

    void adopt(OwnedPtr<T>* ptr) {
      q->adopt(ptr);
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
      delete iter->second;
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

  void adopt(const Key& key, OwnedPtr<T>* ptr) {
    T* value = ptr->release();
    std::pair<typename InnerMap::iterator, bool> insertResult =
        map.insert(std::make_pair(key, value));
    if (!insertResult.second) {
      delete insertResult.first->second;
      insertResult.first->second = value;
    }
  }

  bool adoptIfNew(const Key& key, OwnedPtr<T>* ptr) {
    T* value = ptr->release();
    std::pair<typename InnerMap::iterator, bool> insertResult =
        map.insert(std::make_pair(key, value));
    if (insertResult.second) {
      return true;
    } else {
      ptr->reset(value);
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
      OwnedPtr<T> ptr;
      ptr.reset(iter->second);
      output.adopt(&ptr);
    }
    map.clear();
  }

  bool erase(const Key& key) {
    typename InnerMap::iterator iter = map.find(key);
    if (iter == map.end()) {
      return false;
    } else {
      delete iter->second;
      map.erase(iter);
      return true;
    }
  }

  void clear() {
    for (typename InnerMap::const_iterator iter = map.begin();
         iter != map.end(); ++iter) {
      delete iter->second;
    }
    map.clear();
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

    const T* value() {
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

#endif  // EKAM_OWNEDPTR_H_
