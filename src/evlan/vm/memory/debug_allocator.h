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

#ifndef EVLAN_VM_MEMORY_DEBUG_ALLOCATOR_H__
#define EVLAN_VM_MEMORY_DEBUG_ALLOCATOR_H__

#include <deque>
#include <map>
#include <vector>
#include <utility>

#include "evlan/vm/memory/allocator.h"

namespace evlan {
namespace vm {
namespace memory {

// An implementation of Allocator which aggressively checks for misuse.
// Among other things, this implementation checks that:
//   * Memory before and after the allocated block is not overwritten.
//   * The pointer passed to Free() was, in fact, one returned by Allocate().
//   * The same pointer is not Free()d twice.
//   * The memory is not used after Free().
//   * All allocations are Free()d before the allocator is destroyed.
// DebugAllocator is usefor for testing MemoryManager implementations.  It
// can also be paired with DebugMemoryManager to test MemoryManager users.
class DebugAllocator : public Allocator {
 public:
  DebugAllocator(Allocator* inner);
  ~DebugAllocator();

  bool IsAllocatedBytes(const byte* bytes, int size) const;

  template <typename Type>
  bool IsAllocated(const Type* object) const {
    return IsAllocatedBytes(reinterpret_cast<const byte*>(object),
                            sizeof(*object));
  }

  // implements Allocator --------------------------------------------
  byte* Allocate(int size);
  void Free(byte* bytes, int size);

 private:
  void FlushOneHeldAllocation();
  void HoldAllocation(byte* bytes, int size);

  Allocator* inner_allocator_;
  map<const byte*, int> allocations_;
  deque<pair<byte*, int> > held_allocations_;
  int held_bytes_size_;
  static const int kMaxHeldBytes = 256*1024;
  static const int kGuardByteCount = 8;
  static const uint kGuardByteValue = 0xeb;
};

}  // namespace memory
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_MEMORY_DEBUG_ALLOCATOR_H__
