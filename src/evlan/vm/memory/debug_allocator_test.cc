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

#include "evlan/vm/memory/debug_allocator.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace memory {
namespace {

TEST(DebugAllocatorTest, IsAllocated) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);

  byte* bytes = allocator.Allocate(4);

  EXPECT_TRUE(allocator.IsAllocatedBytes(bytes, 4));
  EXPECT_TRUE(allocator.IsAllocatedBytes(bytes + 1, 2));

  EXPECT_FALSE(allocator.IsAllocatedBytes(bytes + 1, 4));
  EXPECT_FALSE(allocator.IsAllocatedBytes(bytes - 1, 4));
  EXPECT_FALSE(allocator.IsAllocatedBytes(NULL, 4));

  allocator.Free(bytes, 4);
}

TEST(DebugAllocatorTest, DetectMemoryLeak) {
  MallocAllocator malloc_allocator;

  EXPECT_DEATH({
      DebugAllocator allocator(&malloc_allocator);
      allocator.Allocate(4);
    }, "Not all allocations were freed before destroying DebugAllocator\\.");
}

TEST(DebugAllocatorTest, DetectNotAllocated) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  byte bytes[4];

  EXPECT_DEATH(allocator.Free(bytes, 4),
    "Free\\(\\) passed a pointer that does not point to allocated memory\\.");
}

TEST(DebugAllocatorTest, DetectDoubleFree) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  byte* bytes = allocator.Allocate(4);
  allocator.Free(bytes, 4);

  EXPECT_DEATH(allocator.Free(bytes, 4),
    "Free\\(\\) passed a pointer that does not point to allocated memory\\.");
}

TEST(DebugAllocatorTest, DetectMisalignedFree) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  byte* bytes = allocator.Allocate(4);

  EXPECT_DEATH(allocator.Free(bytes + 2, 2),
    "Free\\(\\) passed a pointer to allocated memory, but which "
    "doesn't point at the beginning of the allocation\\.");

  allocator.Free(bytes, 4);
}

TEST(DebugAllocatorTest, DetectSizeMismatch) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  byte* bytes = allocator.Allocate(4);

  EXPECT_DEATH(allocator.Free(bytes, 8),
    "Free\\(\\) passed size which does not match that passed to "
    "Allocate\\(\\)\\.");

  allocator.Free(bytes, 4);
}

TEST(DebugAllocatorTest, DetectMemoryCorruptionBeforeBlock) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  byte* bytes = allocator.Allocate(4);

  EXPECT_DEATH({
      bytes[-1] = 0;
      allocator.Free(bytes, 4);
    }, "Memory corruption in guard bytes before memory being Free\\(\\)ed\\.");

  allocator.Free(bytes, 4);
}

TEST(DebugAllocatorTest, DetectMemoryCorruptionAfterBlock) {
  MallocAllocator malloc_allocator;
  DebugAllocator allocator(&malloc_allocator);
  byte* bytes = allocator.Allocate(4);

  EXPECT_DEATH({
      bytes[4] = 0;
      allocator.Free(bytes, 4);
    }, "Memory corruption in guard bytes after memory being Free\\(\\)ed\\.");

  allocator.Free(bytes, 4);
}

TEST(DebugAllocatorTest, DetectFreedMemoryWrite) {
  MallocAllocator malloc_allocator;
  DebugAllocator* allocator = new DebugAllocator(&malloc_allocator);
  byte* bytes = allocator->Allocate(4);
  allocator->Free(bytes, 4);

  EXPECT_DEATH({
      bytes[0] = 0;
      delete allocator;
    }, "Memory modified after being Free\\(\\)ed.");

  delete allocator;
}

}  // namespace
}  // namespace memory
}  // namespace vm
}  // namespace evlan

