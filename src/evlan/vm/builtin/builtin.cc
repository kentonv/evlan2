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

#include "evlan/vm/builtin/builtin.h"
#include "evlan/vm/builtin/array.h"
#include "evlan/vm/builtin/atom.h"
#include "evlan/vm/builtin/boolean.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/native.h"
#include "evlan/vm/builtin/protobuf.h"
#include "evlan/vm/builtin/type_error.h"

namespace evlan {
namespace vm {
namespace builtin {

namespace {

const Logic* NewBuiltinLibrary() {
  NativeModule* result = new NativeModule("builtin");
  result->AddLogic("integer", GetIntegerBuiltins());
  result->AddLogic("double", GetDoubleBuiltins());
  result->AddLogic("boolean", BooleanUtils::GetBuiltinModule());
  result->AddLogic("atom", GetAtomBuiltins());
  result->AddLogic("bytes", ByteUtils::GetBuiltinModule());
  result->AddLogic("protobuf", ProtobufUtils::GetBuiltinModule());
  result->AddLogic("typeError", TypeErrorUtils::GetBuiltinModule());
  result->AddLogic("array", ArrayUtils::GetBuiltinModule());
  return result;
}

}  // namespace

Value GetBuiltinLibrary() {
  static const Value kSingleton(NewBuiltinLibrary(), Data());
  return kSingleton;
}

void RegisterBuiltinLibrary(distribute::ValueRegistrar* registrar,
                            memory::MemoryManager* memory_manager,
                            memory::MemoryRoot* memory_root) {
  down_cast<const NativeModule*>(GetBuiltinLibrary().GetLogic())
      ->Register(registrar, memory_manager, memory_root);
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
