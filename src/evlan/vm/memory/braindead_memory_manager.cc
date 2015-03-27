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
#include "evlan/stl-util.h"

namespace evlan {
namespace vm {
namespace memory {

BraindeadMemoryManager::BraindeadMemoryManager(Allocator* allocator)
  : allocator_(allocator) {}

BraindeadMemoryManager::~BraindeadMemoryManager() {
  DoPendingWithdraw();
  for (int i = 0; i < blocks_.size(); i++) {
    allocator_->Free(blocks_[i].first, blocks_[i].second);
  }
  STLDeleteElements(&object_trackers_);
  STLDeleteElements(&backlinks_);
}

byte* BraindeadMemoryManager::AllocateBytes(int size, const MemoryRoot* root) {
  DoPendingWithdraw();
  byte* result = allocator_->Allocate(size);
  blocks_.push_back(make_pair(result, size));
  return result;
}

byte* BraindeadMemoryManager::TryActivateBytes(const byte* bytes) {
  if (!blocks_.empty() &&
      blocks_.back().first <= bytes &&
      blocks_.back().first + blocks_.back().second > bytes) {
    // Weird math here avoids a const_cast.
    int offset = bytes - blocks_.back().first;
    return blocks_.back().first + offset;
  } else {
    return NULL;
  }
}

byte* BraindeadMemoryManager::TryExtendBytes(int size) {
  DoPendingWithdraw();
  return NULL;
}

bool BraindeadMemoryManager::TryWithdrawBytes(const byte* bytes, int size) {
  if (!blocks_.empty() && blocks_.back().first == bytes &&
      blocks_.back().second == size) {
    withdrawn_blocks_.push_back(blocks_.back());
    blocks_.pop_back();
    return true;
  } else {
    return false;
  }
}

void BraindeadMemoryManager::DoPendingWithdraw() {
  for (int i = 0; i < withdrawn_blocks_.size(); i++) {
    allocator_->Free(withdrawn_blocks_[i].first, withdrawn_blocks_[i].second);
  }
  withdrawn_blocks_.clear();
}

ObjectTracker* BraindeadMemoryManager::AddTrackedObject(
    TrackedObject* object) {
  class BraindeadObjectTracker : public ObjectTracker {
   public:
    BraindeadObjectTracker(TrackedObject* object): object_(object) {}
    ~BraindeadObjectTracker() {
      object_->NotReachable();
    }

    // implements ObjectTracker --------------------------------------
    TrackedObject* GetTrackedObject() { return object_; }
    void AddExternalReference() {}
    void RemoveExternalReference() {}

   private:
    TrackedObject* object_;
  };

  BraindeadObjectTracker* result = new BraindeadObjectTracker(object);
  object_trackers_.push_back(result);
  return result;
}

bool BraindeadMemoryManager::ReviveTrackedObject(
    ObjectTracker* object_tracker) {
  return true;
}

Backlink* BraindeadMemoryManager::MakeBacklink() {
  class BraindeadBacklink : public Backlink {
   public:
    BraindeadBacklink() : value_(NULL) {}
    ~BraindeadBacklink() {}

    // implements Backlink -------------------------------------------
    const byte* GetBytes() { return value_; }
    void SetBytes(const byte* target, const MemoryGuide* guide) {
      value_ = target;
    }

   private:
    const byte* value_;
  };

  Backlink* result = new BraindeadBacklink();
  backlinks_.push_back(result);
  return result;
}

}  // namespace memory
}  // namespace vm
}  // namespace evlan
