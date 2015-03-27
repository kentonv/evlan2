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

#include "evlan/vm/memory/pool_allocator.h"
#include "evlan/common.h"

namespace evlan {
namespace vm {
namespace memory {

const int PoolAllocator::kNewPoolSize;
const int PoolAllocator::kSeparateAllocationThreshold;

PoolAllocator::PoolAllocator(Allocator* inner)
  : inner_allocator_(inner),
    current_pool_start_(NULL),
    current_pool_position_(NULL),
    current_pool_remaining_bytes_(0) {}

PoolAllocator::~PoolAllocator() {
  for (deque<pair<byte*, int> >::iterator it = pools_.begin();
       it != pools_.end();
       ++it) {
    inner_allocator_->Free(it->first, it->second);
  }
}

byte* PoolAllocator::Allocate(int size) {
  size = Align(size);

  // Check if current pool cannot satisfy this request.
  if (size > current_pool_remaining_bytes_) {
    // If size is larger than kSeparateAllocationThreshold, get an allocation
    // from inner_allocator to handle just this request, and keep the current
    // pool active.
    if (size > kSeparateAllocationThreshold) {
      byte* result = inner_allocator_->Allocate(size);
      pools_.push_back(std::make_pair(result, size));
      return result;
    }

    // Create a new pool.
    current_pool_position_ = inner_allocator_->Allocate(kNewPoolSize);
    current_pool_start_ = current_pool_position_;
    current_pool_remaining_bytes_ = kNewPoolSize;
    pools_.push_back(std::make_pair(current_pool_position_, kNewPoolSize));
  }
  byte* result = current_pool_position_;
  current_pool_position_ += size;
  current_pool_remaining_bytes_ -= size;
  return result;
}

void PoolAllocator::Free(byte* bytes, int size) {
  // If this is the last block in the pool, back up the current position so
  // that we can reuse this memory.  Otherwise, ignore the Free().
  size = Align(size);
  if (bytes + size == current_pool_position_ &&
      bytes >= current_pool_start_) {
    current_pool_position_ -= size;
    current_pool_remaining_bytes_ += size;
  }
}

}  // namespace memory
}  // namespace vm
}  // namespace evlan
