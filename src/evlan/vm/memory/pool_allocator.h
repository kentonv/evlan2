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

// Author: John Dethridge
//
// PoolAllocator is an implementation of Allocator which allocates memory out
// of pools obtained from another Allocator, and ignores Free() calls.

#ifndef EVLAN_VM_MEMORY_POOL_ALLOCATOR_H__
#define EVLAN_VM_MEMORY_POOL_ALLOCATOR_H__

#include <deque>
#include <utility>
#include <limits>
#include "evlan/vm/memory/allocator.h"

namespace evlan {
namespace vm {
namespace memory {

class PoolAllocator : public Allocator {
 public:
  PoolAllocator(Allocator* inner);
  ~PoolAllocator();

  // implements Allocator --------------------------------------------
  byte* Allocate(int size);
  void Free(byte* bytes, int size);

 private:
  Allocator* inner_allocator_;
  deque<pair<byte*, int> > pools_;
  byte* current_pool_start_;
  byte* current_pool_position_;
  int current_pool_remaining_bytes_;

  // Size of each new pool.
  static const int kNewPoolSize = 262144;

  // If a request does not fit in the current pool, and is larger than this
  // size, then keep the current pool and get a separate allocation from the
  // inner allocator to satisfy the request.  This limits the amount of wasted
  // space at the end of each pool to ~6%.
  // Must not be larger than kNewPoolSize.
  static const int kSeparateAllocationThreshold = 16384;

  inline int Align(int size) {
    // 8-byte alignment.
    // If size is <= 0, change size to 8.
    // Otherwise, round up to the nearest 8 bytes, unless this would overflow.
    if (size <= 0) {
      return 8;
    } else {
      int newsize = (size - 1) & ~0x7;
      if (newsize != (std::numeric_limits<int>::max() & ~0x7)) {
        return newsize + 8;
      }
      return size;
    }
  }
};

}  // namespace memory
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_MEMORY_POOL_ALLOCATOR_H__
