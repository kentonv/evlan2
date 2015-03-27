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

#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/native.h"
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace builtin {

Value TypeErrorUtils::MakeError(memory::MemoryManager* memory_manager,
                                const memory::MemoryRoot* memory_root,
                                const StringPiece& description) {
  Value text = ByteUtils::CopyString(memory_manager, memory_root, description);
  return Value(&kLogic, text.GetData());
}

// ===================================================================

const TypeErrorUtils::ErrorLogic TypeErrorUtils::kLogic;

TypeErrorUtils::ErrorLogic::ErrorLogic() {}
TypeErrorUtils::ErrorLogic::~ErrorLogic() {}

void TypeErrorUtils::ErrorLogic::Run(Context* context) const {
  // A type error applied to anything is itself.
  context->Return(Value(this, context->GetData()));
}

void TypeErrorUtils::ErrorLogic::Accept(const Data* data,
                                        memory::MemoryVisitor* visitor) const {
  ByteUtils::GetByteArrayLogic()->Accept(data, visitor);
}

void TypeErrorUtils::ErrorLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  printer->Print("typeError(\"$text$\")",
    "text", strings::CEscape(ByteUtils::DataToString(data).as_string()));
}

// ===================================================================

static Value PropagateError(Value maybe_error, Value result) {
  if (TypeErrorUtils::IsError(maybe_error)) {
    return maybe_error;
  } else {
    return result;
  }
}

const Logic* TypeErrorUtils::NewBuiltinModule() {
  NativeModule* result = new NativeModule("nativeTypeError");
  result->AddFunction("propagateError", &PropagateError);
  return result;
}

const Logic* TypeErrorUtils::GetBuiltinModule() {
  static const Logic* const kSingleton = NewBuiltinModule();
  return kSingleton;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
