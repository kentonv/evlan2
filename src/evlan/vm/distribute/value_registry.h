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

#ifndef EVLAN_VM_DISTRIBUTE_VALUE_REGISTRY_H_
#define EVLAN_VM_DISTRIBUTE_VALUE_REGISTRY_H_

#include "evlan/vm/runtime.h"

namespace evlan {
namespace vm {
namespace distribute {

class ValueRegistry {
 public:
  virtual ~ValueRegistry();

  virtual Value Find(const ValueId& id,
                     memory::MemoryManager* memory_manager) = 0;
};

class ValueRegistrar {
 public:
  virtual ~ValueRegistrar();

  virtual void Register(const ValueId& id, Value value,
                        memory::MemoryManager* memory_manager,
                        memory::MemoryRoot* memory_root) = 0;
};

}  // namespace distribute
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_DISTRIBUTE_VALUE_REGISTRY_H_
