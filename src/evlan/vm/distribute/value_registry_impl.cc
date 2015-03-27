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

#include "evlan/vm/distribute/value_registry_impl.h"
#include "evlan/map-util.h"

namespace evlan {
namespace vm {
namespace distribute {

PersistentValueRegistry::PersistentValueRegistry() {}

PersistentValueRegistry::~PersistentValueRegistry() {
  for (ValueMap::iterator iter = values_.begin();
       iter != values_.end(); ++iter) {
    iter->second->Release();
  }
}

Value PersistentValueRegistry::Find(const ValueId& id,
                                    memory::MemoryManager* memory_manager) {
  MutexLock lock(&mutex_);

  const KeepAliveValue* result = FindPtrOrNull(values_, id);
  if (result == NULL) {
    return Value();
  } else {
    return result->Get(memory_manager);
  }
}

void PersistentValueRegistry::Register(const ValueId& id, Value value,
                                       memory::MemoryManager* memory_manager,
                                       memory::MemoryRoot* memory_root) {
  MutexLock lock(&mutex_);

  const KeepAliveValue** map_slot = &values_[id];
  if (*map_slot != NULL) {
    (*map_slot)->Release();
  }

  *map_slot = new KeepAliveValue(memory_manager, value);
}

// ===================================================================

// TODO(kenton):  Maps to values which remove themselves when garbage-collected
//   is becoming a common pattern (AtomRegistry, RemoteWorkerFactoryImpl, etc.),
//   but the implementation is subtle.  We should factor out a common
//   implementation.
class CachedValueRegistry::TrackedValue : public memory::TrackedObject {
 public:
  TrackedValue(CachedValueRegistry* registry,
               memory::MemoryManager* memory_manager,
               const ValueId& id, Value value)
      : registry_(registry), id_(id), value_(value),
        tracker_(memory_manager->AddTrackedObject(this)) {}
  virtual ~TrackedValue() {}

  CachedValueRegistry* const registry_;
  ValueId const id_;
  Value const value_;
  memory::ObjectTracker* const tracker_;

  // implements TrackedObject ----------------------------------------

  virtual void NotReachable() {
    if (registry_ != NULL) {
      registry_->Remove(id_, this);
    }
    delete this;
  }

  virtual void Accept(memory::MemoryVisitor* visitor) const {
    value_.Accept(visitor);
  }
};

CachedValueRegistry::CachedValueRegistry(ValueRegistry* fallback)
    : fallback_(fallback) {}

CachedValueRegistry::~CachedValueRegistry() {}

Value CachedValueRegistry::Find(const ValueId& id,
                                memory::MemoryManager* memory_manager) {
  MutexLock lock(&mutex_);

  const TrackedValue* result = FindPtrOrNull(values_, id);
  if (result != NULL && memory_manager->ReviveTrackedObject(result->tracker_)) {
    return result->value_;
  } else {
    return fallback_->Find(id, memory_manager);
  }
}

void CachedValueRegistry::Register(const ValueId& id, Value value,
                                   memory::MemoryManager* memory_manager,
                                   memory::MemoryRoot* memory_root) {
  MutexLock lock(&mutex_);
  values_[id] = new TrackedValue(this, memory_manager, id, value);
}

void CachedValueRegistry::Remove(const ValueId& id,
                                 const TrackedValue* value) {
  MutexLock lock(&mutex_);

  // Don't remove the map entry unless it is actually mapping the given id to
  // the given value.  It's possible that some other call to Register()
  // overwrote the map entry in the meantime.
  ValueMap::iterator iter = values_.find(id);
  if (iter != values_.end() && iter->second == value) {
    values_.erase(iter);
  }
}

}  // namespace distribute
}  // namespace vm
}  // namespace evlan
