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
#include "evlan/common.h"

namespace evlan {
namespace vm {
namespace memory {

const int DebugAllocator::kGuardByteCount;
const uint DebugAllocator::kGuardByteValue;

DebugAllocator::DebugAllocator(Allocator* inner)
  : inner_allocator_(inner),
    held_bytes_size_(0) {}

DebugAllocator::~DebugAllocator() {
  GOOGLE_CHECK(allocations_.empty())
    << ": Not all allocations were freed before destroying DebugAllocator.";
  while (!held_allocations_.empty()) {
    FlushOneHeldAllocation();
  }
}

bool DebugAllocator::IsAllocatedBytes(const byte* bytes, int size) const {
  map<const byte*, int>::const_iterator iter = allocations_.upper_bound(bytes);
  if (iter == allocations_.begin()) return false;

  --iter;
  return iter->first <= bytes &&
         iter->first + iter->second >= bytes + size;
}

byte* DebugAllocator::Allocate(int size) {
  byte* bytes = inner_allocator_->Allocate(size + kGuardByteCount * 2);

  memset(bytes, kGuardByteValue, kGuardByteCount);
  memset(bytes + size + kGuardByteCount, kGuardByteValue, kGuardByteCount);

  byte* result = bytes + kGuardByteCount;
  allocations_[result] = size;

  return result;
}

void DebugAllocator::Free(byte* bytes, int size) {
  byte* real_bytes = bytes - kGuardByteCount;
  byte* end_bytes = bytes + size;
  int real_size = size + kGuardByteCount * 2;

  map<const byte*, int>::iterator iter = allocations_.find(bytes);
  if (iter == allocations_.end()) {
    if (IsAllocatedBytes(bytes, 0)) {
      GOOGLE_LOG(FATAL) << "Free() passed a pointer to allocated memory, but which "
                    "doesn't point at the beginning of the allocation.";
    } else {
      GOOGLE_LOG(FATAL) << "Free() passed a pointer that does not point to allocated "
                    "memory.";
    }
  }

  GOOGLE_CHECK_EQ(size, iter->second)
    << ": Free() passed size which does not match that passed to Allocate().";

  allocations_.erase(iter);

  // Check the guard bytes.
  for (int i = 0; i < kGuardByteCount; i++) {
    GOOGLE_CHECK_EQ(real_bytes[i], kGuardByteValue)
      << ": Memory corruption in guard bytes before memory being Free()ed.";
  }
  for (int i = 0; i < kGuardByteCount; i++) {
    GOOGLE_CHECK_EQ(end_bytes[i], kGuardByteValue)
      << ": Memory corruption in guard bytes after memory being Free()ed.";
  }

  if (real_size <= kMaxHeldBytes) {
    while (held_bytes_size_ > kMaxHeldBytes - real_size) {
      FlushOneHeldAllocation();
    }
    HoldAllocation(real_bytes, real_size);
  } else {
    memset(real_bytes, kGuardByteValue, real_size);
    inner_allocator_->Free(real_bytes, real_size);
  }
}

void DebugAllocator::FlushOneHeldAllocation() {
  GOOGLE_CHECK(!held_allocations_.empty());
  byte* bytes = held_allocations_.front().first;
  int size = held_allocations_.front().second;
  for (int i = 0; i < size; i++) {
    GOOGLE_CHECK_EQ(bytes[i], kGuardByteValue)
      << ": Memory modified after being Free()ed.";
  }
  inner_allocator_->Free(bytes, size);
  GOOGLE_CHECK_GE(held_bytes_size_, size);
  held_bytes_size_ -= size;
  held_allocations_.pop_front();
}

void DebugAllocator::HoldAllocation(byte* bytes, int size) {
  memset(bytes, kGuardByteValue, size);
  held_bytes_size_ += size;
  held_allocations_.push_back(make_pair(bytes, size));
}

}  // namespace memory
}  // namespace vm
}  // namespace evlan
