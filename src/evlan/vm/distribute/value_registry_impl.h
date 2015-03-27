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

#ifndef EVLAN_VM_DISTRIBUTE_VALUE_REGISTRY_IMPL_H_
#define EVLAN_VM_DISTRIBUTE_VALUE_REGISTRY_IMPL_H_

#include "evlan/vm/distribute/value_registry.h"
#include "evlan/common.h"
#include <unordered_map>

namespace evlan {
namespace vm {
namespace distribute {

class PersistentValueRegistry : public ValueRegistry, public ValueRegistrar {
 public:
  PersistentValueRegistry();
  virtual ~PersistentValueRegistry();

  // TODO(kenton):  Garbage collection methods.

  // implements ValueRegistry ----------------------------------------
  virtual Value Find(const ValueId& id,
                     memory::MemoryManager* memory_manager);

  // implements ValueRegistrar ---------------------------------------
  virtual void Register(const ValueId& id, Value value,
                        memory::MemoryManager* memory_manager,
                        memory::MemoryRoot* memory_root);

 private:
  typedef unordered_map<ValueId, const KeepAliveValue*> ValueMap;

  Mutex mutex_;
  ValueMap values_;
};

class CachedValueRegistry : public ValueRegistry, public ValueRegistrar {
 public:
  CachedValueRegistry(ValueRegistry* fallback);
  virtual ~CachedValueRegistry();

  // implements ValueRegistry ----------------------------------------
  virtual Value Find(const ValueId& id,
                     memory::MemoryManager* memory_manager);

  // implements ValueRegistrar ---------------------------------------
  virtual void Register(const ValueId& id, Value value,
                        memory::MemoryManager* memory_manager,
                        memory::MemoryRoot* memory_root);

 private:
  class TrackedValue;
  typedef unordered_map<ValueId, const TrackedValue*> ValueMap;

  ValueRegistry* const fallback_;

  Mutex mutex_;
  ValueMap values_;

  void Remove(const ValueId& id, const TrackedValue* value);
};

}  // namespace distribute
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_DISTRIBUTE_VALUE_REGISTRY_IMPL_H_
