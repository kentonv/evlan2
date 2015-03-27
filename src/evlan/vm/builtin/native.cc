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

#include "evlan/vm/builtin/native.h"
#include "evlan/vm/builtin/atom.h"
#include "evlan/vm/distribute/value_registry.h"
#include <google/protobuf/io/printer.h>
#include "evlan/stl-util.h"
#include "evlan/map-util.h"

namespace evlan {
namespace vm {
namespace builtin {

NativeModule::NativeModule(const StringPiece& name)
  : name_(name.as_string()) {
}

NativeModule::~NativeModule() {
  STLDeleteElements(&logics_to_delete_);
}

void NativeModule::AddLogic(const StringPiece& name, const Logic* logic) {
  AddValue(name, Value(logic, Data()));
}

void NativeModule::AddAndAdoptLogic(const StringPiece& name, Logic* logic) {
  AddLogic(name, logic);
  logics_to_delete_.push_back(logic);
}

void NativeModule::AddValue(const StringPiece& name, const Value& value) {
  GOOGLE_CHECK(InsertIfNotPresent(&value_map_, name.as_string(), value));
}

void NativeModule::Run(Context* context) const {
  if (!IsAtom(context->GetParameter())) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Tried to invoke a native module as a function (on a parameter that "
      "was not at atom)."));
    return;
  }

  string name = GetAtomName(context->GetParameter().GetData()).as_string();
  const Value* value = FindOrNull(value_map_, name);

  if (value == NULL) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Native module has no member \"" + name + "\"."));
    return;
  }

  context->Return(*value);
}

void NativeModule::Accept(const Data* data,
                          memory::MemoryVisitor* visitor) const {
  // Nothing to do.
}

void NativeModule::GetId(Data data, ValueId* output) const {
  output->SetBytesWithPrefix('<', name_);
}

void NativeModule::DebugPrint(Data data, int depth,
                              google::protobuf::io::Printer* printer) const {
  printer->Print("nativeModule(\"$name$\")", "name", name_);
}

void NativeModule::Register(
    distribute::ValueRegistrar* registrar,
    memory::MemoryManager* memory_manager,
    memory::MemoryRoot* memory_root) const {
  ValueId id;
  GetId(Data(), &id);
  registrar->Register(id, Value(this, Data()), memory_manager, memory_root);

  RegisterNestedValues(registrar, memory_manager, memory_root);
}

void NativeModule::RegisterNestedValues(
    distribute::ValueRegistrar* registrar,
    memory::MemoryManager* memory_manager,
    memory::MemoryRoot* memory_root) const {
  for (ValueMap::const_iterator iter = value_map_.begin();
       iter != value_map_.end(); ++iter) {
    ValueId id;
    iter->second.GetId(&id);
    registrar->Register(id, iter->second, memory_manager, memory_root);

    // TODO(kenton):  Find a better solution than dynamic_cast.
    const NativeModule* sub_module =
        dynamic_cast<const NativeModule*>(iter->second.GetLogic());
    if (sub_module != NULL) {
      sub_module->RegisterNestedValues(
          registrar, memory_manager, memory_root);
    }
  }
}

// ===================================================================

NativeModule::NativeLogic::NativeLogic(
    const NativeModule* module, StringPiece name)
  : module_(module), name_(name.as_string()) {}

NativeModule::NativeLogic::~NativeLogic() {}

void NativeModule::NativeLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  // Nothing to do.
}

void NativeModule::NativeLogic::GetId(Data data, ValueId* output) const {
  output->SetBytesWithPrefix('<', strings::Substitute("$0.$1", module_->name_, name_));
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
