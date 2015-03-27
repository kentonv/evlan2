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

#include "evlan/vm/builtin/boolean.h"
#include "evlan/vm/builtin/native.h"
#include "evlan/vm/builtin/support.h"

namespace evlan {
namespace vm {
namespace builtin {

BooleanUtils::BooleanLogic BooleanUtils::kLogic;
Value BooleanUtils::kTrue(&kLogic, Data(1));
Value BooleanUtils::kFalse(&kLogic, Data(0));

BooleanUtils::BooleanLogic::BooleanLogic() {}
BooleanUtils::BooleanLogic::~BooleanLogic() {}

void BooleanUtils::BooleanLogic::Run(Context* context) const {
  context->GetSupportCodeManager()
         ->GetSupportCode(SupportCodeManager::BOOLEAN)
         ->Run(context);
}

void BooleanUtils::BooleanLogic::Accept(
    const Data* data,
    memory::MemoryVisitor* visitor) const {
  // Nothing to do.
}

void BooleanUtils::BooleanLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  printer->Print(data.AsInteger() == 0 ? "false" : "true");
}

// -------------------------------------------------------------------

namespace {

bool And(bool a, bool b) { return a && b; }
bool Or (bool a, bool b) { return a || b; }
bool Xor(bool a, bool b) { return a != b; }

bool Not(bool a) { return !a; }

bool Equals(bool a, bool b) { return a == b; }

Value If(bool condition, Value true_clause, Value false_clause) {
  return condition ? true_clause : false_clause;
}

}  // namespace

const Logic* BooleanUtils::NewBuiltinModule() {
  NativeModule* result = new NativeModule("nativeBoolean");
  result->AddValue("true", kTrue);
  result->AddValue("false", kFalse);
  result->AddFunction("and", &And);
  result->AddFunction("or" , &Or );
  result->AddFunction("xor", &Xor);
  result->AddFunction("not", &Not);
  result->AddFunction("equals", &Equals);
  result->AddFunction("if", &If);
  return result;
}

const Logic* BooleanUtils::GetBuiltinModule() {
  static const Logic* kSingleton = NewBuiltinModule();
  return kSingleton;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
