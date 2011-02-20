// c++ -- http://code.google.com/p/c++
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
//     * Neither the name of the c++ project nor the names of its
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

#include "Promise.h"
#include <stdio.h>
#include <stdlib.h>
#include "gtest.h"

namespace ekam {
namespace {

class MockExecutor: public Executor {
private:
  class PendingRunnableImpl : public PendingRunnable {
  public:
    PendingRunnableImpl(MockExecutor* executor, OwnedPtr<Runnable> runnable)
        : executor(executor), runnable(runnable.release()) {}

    ~PendingRunnableImpl() {
      if (runnable != nullptr) {
        for (auto iter = executor->queue.begin(); iter != executor->queue.end(); ++iter) {
          if (*iter == this) {
            executor->queue.erase(iter);
            break;
          }
        }
      }
    }

    void run() {
      runnable.release()->run();
    }

  private:
    MockExecutor* executor;
    OwnedPtr<Runnable> runnable;
  };

public:
  MockExecutor() {}
  ~MockExecutor() {
    EXPECT_TRUE(queue.empty());
  }

  void runNext() {
    ASSERT_FALSE(queue.empty());
    auto front = queue.front();
    queue.pop_front();
    front->run();
  }

  bool empty() {
    return queue.empty();
  }

  // implements Executor -----------------------------------------------------------------
  OwnedPtr<PendingRunnable> runLater(OwnedPtr<Runnable> runnable) {
    auto result = newOwned<PendingRunnableImpl>(this, runnable.release());
    queue.push_back(result.get());
    return result.release();
  }

private:
  std::deque<PendingRunnableImpl*> queue;
};

template <typename T>
class MockFulfillerAgent: public PromiseFulfiller<T> {
public:
  typedef typename PromiseFulfiller<T>::Callback Callback;
  MockFulfillerAgent(Callback* callback, Callback** callbackPtr)
      : callbackPtr(callbackPtr) {
    *callbackPtr = callback;
  }
  ~MockFulfillerAgent() {
    *callbackPtr = nullptr;
  }

private:
  Callback** callbackPtr;
};

TEST(PromiseTest, Basic) {
  MockExecutor mockExecutor;

  PromiseFulfiller<int>::Callback* fulfiller;
  auto promise = newPromise<MockFulfillerAgent<int>>(&fulfiller);

  bool triggered = false;

  Promise<int> promise2 = mockExecutor.when(promise)(
    [&triggered](int i) -> int {
      triggered = true;
      return 123;
    });

  EXPECT_FALSE(triggered);

  fulfiller->fulfill(5);

  EXPECT_FALSE(triggered);

  mockExecutor.runNext();

  EXPECT_TRUE(triggered);

  // Fulfiller deleted because promise has been consumed.
  EXPECT_TRUE(fulfiller == nullptr);
}

TEST(PromiseTest, Dependent) {
  MockExecutor mockExecutor;

  PromiseFulfiller<int>::Callback* fulfiller1;
  auto promise1 = newPromise<MockFulfillerAgent<int>>(&fulfiller1);
  PromiseFulfiller<int>::Callback* fulfiller2;
  auto promise2 = newPromise<MockFulfillerAgent<int>>(&fulfiller2);

  Promise<int> promise3 = mockExecutor.when(promise1, promise2)(
    [](int a, int b) -> int {
      return a + b;
    });

  int result = 0;

  Promise<void> promise4 = mockExecutor.when(promise3)(
    [&result](int a) {
      result = a;
    });

  EXPECT_TRUE(mockExecutor.empty());
  fulfiller1->fulfill(12);
  EXPECT_TRUE(mockExecutor.empty());
  fulfiller2->fulfill(34);
  EXPECT_FALSE(mockExecutor.empty());
  mockExecutor.runNext();
  EXPECT_FALSE(mockExecutor.empty());
  mockExecutor.runNext();
  ASSERT_EQ(result, 46);
}

TEST(PromiseTest, MoveSemantics) {
  MockExecutor mockExecutor;

  PromiseFulfiller<OwnedPtr<int>>::Callback* fulfiller;
  auto promise = newPromise<MockFulfillerAgent<OwnedPtr<int>>>(&fulfiller);

  OwnedPtr<int> ptr = newOwned<int>(12);

  int result = 0;

  Promise<void> promise2 = mockExecutor.when(promise, ptr)(
    [&result](OwnedPtr<int> i, OwnedPtr<int> j) {
      result = *i + *j;
    });

  fulfiller->fulfill(newOwned<int>(34));
  mockExecutor.runNext();
  ASSERT_EQ(result, 46);
}

TEST(PromiseTest, Cancel) {
  MockExecutor mockExecutor;

  PromiseFulfiller<int>::Callback* fulfiller;
  auto promise = newPromise<MockFulfillerAgent<int>>(&fulfiller);

  Promise<void> promise2 = mockExecutor.when(promise)(
    [](int i) {
      ADD_FAILURE() << "Can't get here.";
    });

  EXPECT_TRUE(mockExecutor.empty());
  fulfiller->fulfill(5);
  EXPECT_FALSE(mockExecutor.empty());
  promise2.release();
  EXPECT_TRUE(mockExecutor.empty());
}

TEST(PromiseTest, VoidPromise) {
  MockExecutor mockExecutor;

  PromiseFulfiller<void>::Callback* fulfiller;
  auto promise = newPromise<MockFulfillerAgent<void>>(&fulfiller);

  bool triggered = false;

  Promise<void> promise2 = mockExecutor.when(promise)(
    [&triggered](Void) {
      triggered = true;
    });

  EXPECT_FALSE(triggered);
  fulfiller->fulfill();
  EXPECT_FALSE(triggered);
  mockExecutor.runNext();
  EXPECT_TRUE(triggered);
}

TEST(PromiseTest, Exception) {
  MockExecutor mockExecutor;

  PromiseFulfiller<int>::Callback* fulfiller1;
  auto promise1 = newPromise<MockFulfillerAgent<int>>(&fulfiller1);
  PromiseFulfiller<int>::Callback* fulfiller2;
  auto promise2 = newPromise<MockFulfillerAgent<int>>(&fulfiller2);

  bool triggered = false;

  Promise<void> promise3 = mockExecutor.when(promise1, promise2, 123)(
    [](int i, int j, int k) {
      ADD_FAILURE() << "Can't get here.";
    }, [&triggered](MaybeException<int> i, MaybeException<int> j, int k) {
      triggered = true;

      ASSERT_TRUE(i.isException());
      ASSERT_FALSE(j.isException());
      ASSERT_EQ(123, k);
      ASSERT_EQ(456, j.get());

      try {
        i.get();
        ADD_FAILURE() << "Expected exception.";
      } catch (const std::logic_error& e) {
        ASSERT_STREQ("test", e.what());
      }
    });

  try {
    throw std::logic_error("test");
  } catch (...) {
    fulfiller1->propagateCurrentException();
  }
  fulfiller2->fulfill(456);

  EXPECT_FALSE(triggered);

  mockExecutor.runNext();

  EXPECT_TRUE(triggered);
}

TEST(PromiseTest, ExceptionInCallback) {
  MockExecutor mockExecutor;

  PromiseFulfiller<int>::Callback* fulfiller1;
  auto promise1 = newPromise<MockFulfillerAgent<int>>(&fulfiller1);

  Promise<int> promise2 = mockExecutor.when(promise1)(
    [](int a) -> int {
      throw std::logic_error("test");
    });

  bool triggered = false;

  Promise<void> promise3 = mockExecutor.when(promise2)(
    [](int i) {
      ADD_FAILURE() << "Can't get here.";
    }, [&triggered](MaybeException<int> i) {
      triggered = true;

      ASSERT_TRUE(i.isException());
      try {
        i.get();
        ADD_FAILURE() << "Expected exception.";
      } catch (const std::logic_error& e) {
        ASSERT_STREQ("test", e.what());
      }
    });

  EXPECT_TRUE(mockExecutor.empty());
  fulfiller1->fulfill(12);
  EXPECT_FALSE(mockExecutor.empty());
  mockExecutor.runNext();
  EXPECT_FALSE(mockExecutor.empty());
  mockExecutor.runNext();
  ASSERT_TRUE(triggered);
}

TEST(PromiseTest, ExceptionPropagation) {
  MockExecutor mockExecutor;

  PromiseFulfiller<int>::Callback* fulfiller1;
  auto promise1 = newPromise<MockFulfillerAgent<int>>(&fulfiller1);

  Promise<void> promise2 = mockExecutor.when(promise1)(
    [](int a) {
      ADD_FAILURE() << "Can't get here.";
    });

  bool triggered = false;

  Promise<void> promise3 = mockExecutor.when(promise2)(
    [](Void) {
      ADD_FAILURE() << "Can't get here.";
    }, [&triggered](MaybeException<void> i) {
      triggered = true;

      ASSERT_TRUE(i.isException());
      try {
        i.get();
        ADD_FAILURE() << "Expected exception.";
      } catch (const std::logic_error& e) {
        ASSERT_STREQ("test", e.what());
      }
    });

  EXPECT_TRUE(mockExecutor.empty());
  try {
    throw std::logic_error("test");
  } catch (...) {
    fulfiller1->propagateCurrentException();
  }
  EXPECT_FALSE(mockExecutor.empty());
  mockExecutor.runNext();
  EXPECT_FALSE(mockExecutor.empty());
  mockExecutor.runNext();
  ASSERT_TRUE(triggered);
}

}  // namespace
}  // namespace ekam
