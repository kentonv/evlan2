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

#ifndef EVLAN_VM_MEMORY_BRAINDEAD_MEMORY_MANAGER_H__
#define EVLAN_VM_MEMORY_BRAINDEAD_MEMORY_MANAGER_H__

#include <utility>
#include <vector>

#include "evlan/vm/memory/memory_manager.h"

namespace evlan {
namespace vm {
namespace memory {

class Allocator;

// A memory manager implementation which allocates using operator new
// and doesn't free any memory until the destructor is called.
class BraindeadMemoryManager : public MemoryManager {
 public:
  BraindeadMemoryManager(Allocator* allocator);
  ~BraindeadMemoryManager();

  // implements MemoryManager ----------------------------------------
  byte* AllocateBytes(int size, const MemoryRoot* root);
  byte* TryActivateBytes(const byte* bytes);
  byte* TryExtendBytes(int size);
  bool TryWithdrawBytes(const byte* bytes, int size);
  ObjectTracker* AddTrackedObject(TrackedObject* object);
  bool ReviveTrackedObject(ObjectTracker* object_tracker);
  Backlink* MakeBacklink();

 private:
  Allocator* allocator_;

  // All these must be freed when the manager is destroyed.
  vector<pair<byte*, int> > blocks_;
  vector<pair<byte*, int> > withdrawn_blocks_;
  vector<ObjectTracker*> object_trackers_;
  vector<Backlink*> backlinks_;

  // TryWithdrawBytes()'s contract says that the bytes remain readable until
  // the next allocate.  So, we put withdrawn blocks onto withdrawn_blocks_
  // and call DoPendingWithdraw() once it's safe to free them.
  void DoPendingWithdraw();
};

}  // namespace memory
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_MEMORY_BRAINDEAD_MEMORY_MANAGER_H__
