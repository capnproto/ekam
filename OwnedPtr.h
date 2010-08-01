// kake2 -- http://code.google.com/p/kake2
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
//     * Neither the name of the kake2 project nor the names of its
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

#ifndef KAKE2_OWNEDPTR_H_
#define KAKE2_OWNEDPTR_H_

#include <stddef.h>
#include <tr1/type_traits>
#include <vector>
#include <tr1/unordered_map>

namespace kake2 {

template <typename T>
class OwnedPtr {
public:
  OwnedPtr() : ptr(NULL) {}
  ~OwnedPtr() { delete ptr; }

  T* get() const { return ptr; }
  T* operator->() const { return ptr; }
  const T& operator*() const { return *ptr; }

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
    delete ptr;
    ptr = newValue;
  }

  T* release() {
    T* result = ptr;
    ptr = NULL;
    return result;
  }

  template <typename U>
  friend class OwnedPtrVector;
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

  class Appender {
  public:
    Appender(OwnedPtrVector* vec) : vec(vec) {}

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

  void erase(const Key& key) {
    map.erase(key);
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

}  // namespace kake2

#endif  // KAKE2_OWNEDPTR_H_
