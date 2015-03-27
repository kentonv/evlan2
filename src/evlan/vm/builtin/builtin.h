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

#ifndef EVLAN_VM_BUILTIN_BUILTIN_H__
#define EVLAN_VM_BUILTIN_BUILTIN_H__

#include "evlan/vm/runtime.h"

namespace evlan {
  namespace vm {
    namespace distribute {
      class ValueRegistrar;
    }
  }
}

namespace evlan {
namespace vm {
namespace builtin {

// Returns a Value for an object containing all builtins.  This Value does not
// contain pointers to any managed memory.
Value GetBuiltinLibrary();

// Register all built-ins in the given registry.
void RegisterBuiltinLibrary(distribute::ValueRegistrar* registrar,
                            memory::MemoryManager* memory_manager,
                            memory::MemoryRoot* memory_root);

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_BUILTIN_BUILTIN_H__
