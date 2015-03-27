// Copyright (c) 2006-2012 Google, Inc. and contributors.
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

// Author: Kenton Varda

#include "evlan/vm/memory/braindead_memory_manager.h"
#include "evlan/vm/memory/allocator.h"
#include "evlan/vm/memory/debug_allocator.h"
#include "evlan/vm/memory/pool_allocator.h"
#include <gtest/gtest.h>

// TODO(kenton): Write a benchmarking framework.
#define BENCHMARK(FUNC)

namespace evlan {
namespace vm {
namespace memory {
namespace {

class MockObject {
 public:
  int Accept(MemoryVisitor* visitor) const { return sizeof(*this); }
};

class MockTrackedObject : public TrackedObject {
 public:
  MockTrackedObject(): not_reachable_called_(false) {}
  ~MockTrackedObject() {}

  bool not_reachable_called() const { return not_reachable_called_; }

  // implements TrackedObject ----------------------------------------
  void NotReachable() { not_reachable_called_ = true; }
  void Accept(MemoryVisitor* visitor) const {}

 private:
  bool not_reachable_called_;
};

TEST(evlan_memory_BraindeadMemoryManager, AllocateBytes) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  BraindeadMemoryManager manager(&allocator);

  EXPECT_TRUE(manager.AllocateBytes(4, NULL) != NULL);
}

TEST(evlan_memory_BraindeadMemoryManager, TrackedObjects) {
  MockTrackedObject tracked_object;

  {  // scope for manager
    MallocAllocator malloc_allocator;
    DebugAllocator allocator(&malloc_allocator);
    BraindeadMemoryManager manager(&allocator);
    ObjectTracker* tracker = manager.AddTrackedObject(&tracked_object);
    EXPECT_EQ(&tracked_object, tracker->GetTrackedObject());
    EXPECT_FALSE(tracked_object.not_reachable_called());
  }

  EXPECT_TRUE(tracked_object.not_reachable_called());
}

TEST(evlan_memory_BraindeadMemoryManager, Backlinks) {
  MockObject dummy;

  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  BraindeadMemoryManager manager(&allocator);

  Backlink* backlink = manager.MakeBacklink();
  EXPECT_TRUE(backlink->GetBytes() == NULL);

  backlink->Set(&dummy);
  EXPECT_EQ(&dummy, backlink->Get<MockObject>());
}

TEST(evlan_memory_BraindeadMemoryManager, ActivateAndWithdraw) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  BraindeadMemoryManager manager(&allocator);

  byte* bytes = manager.AllocateBytes(4, NULL);

  // Can activate any pointer within the allocation.
  EXPECT_EQ(bytes, manager.TryActivateBytes(bytes));
  EXPECT_EQ(bytes + 3, manager.TryActivateBytes(bytes + 3));
  EXPECT_EQ(NULL, manager.TryActivateBytes(bytes - 1));
  EXPECT_EQ(NULL, manager.TryActivateBytes(bytes + 4));

  byte* bytes2 = manager.AllocateBytes(4, NULL);

  // Can no longer activate original alloc.
  EXPECT_EQ(NULL, manager.TryActivateBytes(bytes));
  EXPECT_EQ(NULL, manager.TryActivateBytes(bytes + 3));

  // Can activate new alloc.
  EXPECT_EQ(bytes2, manager.TryActivateBytes(bytes2));
  EXPECT_EQ(bytes2 + 3, manager.TryActivateBytes(bytes2 + 3));

  // Cannot withdraw original alloc.
  EXPECT_FALSE(manager.TryWithdrawBytes(bytes, 4));

  // *Can* withdraw new alloc.
  EXPECT_TRUE(manager.TryWithdrawBytes(bytes2, 4));

  // Bytes are still valid, though.
  EXPECT_TRUE(allocator.IsAllocatedBytes(bytes2, 4));

  // But cannot be activated.
  EXPECT_EQ(NULL, manager.TryActivateBytes(bytes2));
  EXPECT_EQ(NULL, manager.TryActivateBytes(bytes2 + 3));

  // Original alloc can be activated.
  EXPECT_EQ(bytes, manager.TryActivateBytes(bytes));
  EXPECT_EQ(bytes + 3, manager.TryActivateBytes(bytes + 3));

  // Now we can allocate again.
  manager.AllocateBytes(4, NULL);

  // Old bytes were deleted.
  EXPECT_FALSE(allocator.IsAllocatedBytes(bytes2, 4));
}

// Benchmark allocating 1M of 8-byte words per iteration using MemoryAllocator.
static void BM_BraindeadMemoryManagerStressTest(int iters) {
  for (int iter = 0; iter < iters; iter++) {
    MallocAllocator malloc_allocator;
    BraindeadMemoryManager manager(&malloc_allocator);

    for (int i = 0; i < 128 * 1024; i++) {
      GOOGLE_CHECK(manager.AllocateBytes(8, NULL) != NULL);
    }
  }
}
BENCHMARK(BM_BraindeadMemoryManagerStressTest);

// Benchmark allocating 1M of 8-byte words per iteration using PoolAllocator.
static void BM_BraindeadMemoryManagerPoolStressTest(int iters) {
  for (int iter = 0; iter < iters; iter++) {
    MallocAllocator malloc_allocator;
    PoolAllocator pool_allocator(&malloc_allocator);
    BraindeadMemoryManager manager(&pool_allocator);

    for (int i = 0; i < 128 * 1024; i++) {
      GOOGLE_CHECK(manager.AllocateBytes(8, NULL) != NULL);
    }
  }
}
BENCHMARK(BM_BraindeadMemoryManagerPoolStressTest);

// Test that the benchmark functions run, even if we're not going to run them
// as benchmarks.
TEST(evlan_memory_BraindeadMemoryManager, StressTest) {
  BM_BraindeadMemoryManagerStressTest(1);
  BM_BraindeadMemoryManagerPoolStressTest(1);
}

}  // namespace
}  // namespace memory
}  // namespace vm
}  // namespace evlan
