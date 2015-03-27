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

#ifndef EVLAN_VM_MEMORY_DEBUG_MEMORY_MANAGER_H__
#define EVLAN_VM_MEMORY_DEBUG_MEMORY_MANAGER_H__

#include <vector>
#include <map>
#include <set>

#include "evlan/common.h"
#include "evlan/vm/memory/memory_manager.h"

namespace evlan {
namespace vm {
namespace memory {

class Allocator;
class MallocAllocator;
class DebugAllocator;

// A memory manager implementation which sweeps every time AllocateBytes() is
// called and moves around all memory, thus revealing bugs in callers.
class DebugMemoryManager : public MemoryManager {
 public:
  // Uses a DebugAllocator around a MallocAllocator for the allocator.
  DebugMemoryManager();
  // Use a custom allocator.
  DebugMemoryManager(Allocator* allocator);
  ~DebugMemoryManager();

  // Set false if you want TryActivateBytes() to always fail.
  void AllowActivations(bool allow);
  // Set false if you want TryExtendBytes() to always fail.
  void AllowExtensions(bool allow);

  // Force a sweep to happen *now*.
  void Collect(const MemoryRoot* root);

  // implements MemoryManager ----------------------------------------
  byte* AllocateBytes(int size, const MemoryRoot* root);
  byte* TryActivateBytes(const byte* bytes);
  byte* TryExtendBytes(int size);
  bool TryWithdrawBytes(const byte* bytes, int size);
  ObjectTracker* AddTrackedObject(TrackedObject* object);
  bool ReviveTrackedObject(ObjectTracker* object_tracker);
  Backlink* MakeBacklink();

 private:
  class DebugObjectTracker;
  class DebugBacklink;
  class DebugMemoryVisitor;
  friend class DebugObjectTracker;
  friend class DebugBacklink;
  friend class DebugMemoryVisitor;

  // A block is one contiguous range of bytes allocated in a particular call to
  // AllocateBytes() or TryExtendBytes().
  class Block;

  // An allocation is a set of Blocks allocated via one call to Allocate() and
  // zero or more subsequent calls to TryExtendBytes().
  class Allocation;

  scoped_ptr<MallocAllocator> malloc_allocator_;
  scoped_ptr<DebugAllocator> debug_allocator_;
  Allocator* allocator_;

  // All allocations, stored in order of creation.
  vector<Allocation*> allocations_;

  // All object trackers and backlinks.
  vector<DebugObjectTracker*> object_trackers_;
  vector<DebugBacklink*> backlinks_;

  // ObjectTrackers which currently have a positive external refcount.
  set<DebugObjectTracker*> external_references_;

  // Lookup table mapping pointers to the Blocks they reside in.  We use
  // pointers to the *beginning* of each Block as the key, but by making use
  // of map::upper_bound we can look up a Block given a pointer to any byte
  // in the Block (see FindBlock(), below).
  map<const byte*, Block*> block_map_;

  // We increment allocate_counter_ with every allocation, then do a sweep
  // and reset it whenever it reaches sweep_frequency_.  Usually,
  // sweep_frequency_ is 1.
  int allocate_counter_;
  int sweep_frequency_;

  // Set using AllowActivations() and AllowExtensions(), default true.
  bool allow_activations_;
  bool allow_extensions_;

  // Create a new Allocation.
  Allocation* AddAllocation();

  // Create a new Block within an Allocation.
  Block* AddBlock(Allocation* allocation, int size);

  // Look up the Block which contains the given byte.  Returns NULL if the
  // pointer does not point into any Block.
  Block* FindBlock(const byte* bytes);
};

}  // namespace memory
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_MEMORY_DEBUG_MEMORY_MANAGER_H__

