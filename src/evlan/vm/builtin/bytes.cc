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
#include "evlan/vm/builtin/array.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/native.h"
#include "evlan/vm/builtin/support.h"
#include "evlan/vm/hasher.h"
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace builtin {

namespace {

struct RootPlusBytes : public memory::MemoryRoot {
  const memory::MemoryRoot* next;
  const byte* const* data_ptr;
  int size;

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    next->Accept(visitor);
    visitor->VisitRawBytesPointer(data_ptr, size);
  }
};

}  // namespace

Value ByteUtils::AliasTrackedBytes(memory::MemoryManager* memory_manager,
                                   const memory::MemoryRoot* memory_root,
                                   const byte* data, int size) {
  RootPlusBytes root;
  root.next = memory_root;
  root.data_ptr = &data;
  root.size = size;

  ByteArrayData* result = memory_manager->Allocate<ByteArrayData>(&root);
  result->size = size;
  result->tracked = true;
  result->data = data;
  return Value(&kLogic, Data(result));
}

Value ByteUtils::AliasUntrackedBytes(memory::MemoryManager* memory_manager,
                                     const memory::MemoryRoot* memory_root,
                                     const byte* data, int size) {
  ByteArrayData* result = memory_manager->Allocate<ByteArrayData>(memory_root);
  result->size = size;
  result->tracked = false;
  result->data = data;
  return Value(&kLogic, Data(result));
}

Value ByteUtils::CopyUntrackedBytes(memory::MemoryManager* memory_manager,
                                    const memory::MemoryRoot* memory_root,
                                    const byte* data, int size) {
  byte* copy = memory_manager->AllocateArray<byte>(size, memory_root);
  memcpy(copy, data, size);
  return AliasTrackedBytes(memory_manager, memory_root, copy, size);
}

// ===================================================================

int ByteUtils::ByteArrayData::Accept(memory::MemoryVisitor* visitor) const {
  if (tracked) {
    visitor->VisitRawBytesPointer(&data, size);
  }

  return sizeof(*this);
}

const ByteUtils::ByteArrayLogic ByteUtils::kLogic;

ByteUtils::ByteArrayLogic::ByteArrayLogic() {}
ByteUtils::ByteArrayLogic::~ByteArrayLogic() {}

void ByteUtils::ByteArrayLogic::Run(Context* context) const {
  context->GetSupportCodeManager()
         ->GetSupportCode(SupportCodeManager::STRING)
         ->Run(context);
}

void ByteUtils::ByteArrayLogic::Accept(const Data* data,
                                       memory::MemoryVisitor* visitor) const {
  data->AcceptAs<ByteArrayData>(visitor);
}

void ByteUtils::ByteArrayLogic::GetId(Data data, ValueId* output) const {
  StringPiece contents = DataToString(data);
  if (contents.size() > Hasher::kHashSize) {
    Hasher hasher;
    hasher.AddBytes(contents);
    output->SetBytesFromHasher('\"', &hasher);
  } else {
    output->SetBytesWithPrefix('\'', DataToString(data));
  }
}

int ByteUtils::ByteArrayLogic::Marshal(
    Data data, distribute::Marshaller* marshaller,
    const ValueId* known_id) const {
  // TODO(kenton):  Use BigData for large strings?  We could even use it with
  // a CordFunction to avoid copying the bytes at all.
  return marshaller->AddData(DataToString(data));
}

void ByteUtils::ByteArrayLogic::DebugPrint(Data data, int depth,
                                           google::protobuf::io::Printer* printer) const {
  const ByteArrayData* array_data = data.As<ByteArrayData>();

  printer->Print("\"$text$\"",
                 "text",
                 strings::CEscape(
                     string(reinterpret_cast<const char*>(array_data->data),
                            array_data->size)));
}

// ===================================================================

int ByteUtils::GetSize(StringPiece bytes) {
  return bytes.size();
}

void ByteUtils::GetElement(Context* context, StringPiece bytes, int index) {
  if (index < 0 || index >= bytes.size()) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Array index out of bounds."));
    return;
  }

  // static_cast to make sure we don't sign-extend.
  context->Return(MakeInteger(static_cast<uint8>(bytes.data()[index])));
}

void ByteUtils::GetInterval(Context* context,
                            StringPiece bytes, int start, int end) {
  if (start < 0 || start > end || end > bytes.size()) {
    context->Return(TypeErrorUtils::MakeError(context,
      strings::Substitute(
        "Invalid byte array interval [$0, $1) for array of size $2.",
        start, end, bytes.size())));
    return;
  }

  context->Return(
    AliasTrackedBytes(context->GetMemoryManager(), context,
      reinterpret_cast<const byte*>(bytes.data()) + start, end - start));
}

void ByteUtils::Split(Context* context, StringPiece data, StringPiece delim) {
  vector<int> split_points;

  for (int i = delim.size(); i <= data.size(); i++) {
    if (substr(data, i - delim.size(), delim.size()) == delim) {
      split_points.push_back(i - delim.size());
    }
  }

  split_points.push_back(data.size());
  int size = split_points.size();

  // TODO(kenton):  Find a better way to allocate multiple things at once.
  byte* space = context->GetMemoryManager() ->AllocateBytes(
      (sizeof(ByteArrayData) + sizeof(Value)) * size, context);

  ByteArrayData* byte_array_datas = reinterpret_cast<ByteArrayData*>(space);
  Value* byte_array_values = reinterpret_cast<Value*>(byte_array_datas + size);

  // Data may have moved.
  data = ToString(ArrayUtils::GetElement(context->GetParameter(), 0));

  int prev = 0;
  for (int i = 0; i < split_points.size(); i++) {
    StringPiece piece = substr(data, prev, split_points[i] - prev);
    prev = split_points[i] + delim.size();

    byte_array_datas[i].size = piece.size();
    byte_array_datas[i].tracked = true;
    byte_array_datas[i].data = reinterpret_cast<const byte*>(piece.data());

    byte_array_values[i] = Value(&kLogic, Data(byte_array_datas + i));
  }

  context->Return(
      ArrayUtils::AliasArray(context->GetMemoryManager(), context,
                             size, byte_array_values));
}


namespace {

string Concatenate(StringPiece a, StringPiece b) {
  // TODO(kenton):  This could be optimized a lot.
  return a.as_string() + b.as_string();
}

bool Equals          (StringPiece a, StringPiece b) { return a == b; }
bool NotEquals       (StringPiece a, StringPiece b) { return a != b; }
bool IsLessThan      (StringPiece a, StringPiece b) { return a <  b; }
bool IsGreaterThan   (StringPiece a, StringPiece b) { return a >  b; }
bool IsLessOrEqual   (StringPiece a, StringPiece b) { return a <= b; }
bool IsGreaterOrEqual(StringPiece a, StringPiece b) { return a >= b; }

}

const Logic* ByteUtils::NewBuiltinModule() {
  NativeModule* result = new NativeModule("nativeByteArray");
  result->AddFunction("size", &GetSize);
  result->AddFunction("element", &GetElement);
  result->AddFunction("interval", &GetInterval);
  result->AddFunction("split", &Split);

  result->AddFunction("concatenate", &Concatenate);

  result->AddFunction("equal"     , &Equals          );
  result->AddFunction("notEqual"  , &NotEquals       );
  result->AddFunction("less"      , &IsLessThan      );
  result->AddFunction("greater"   , &IsGreaterThan   );
  result->AddFunction("notGreater", &IsLessOrEqual   );
  result->AddFunction("notLess"   , &IsGreaterOrEqual);

  return result;
}

const Logic* ByteUtils::GetBuiltinModule() {
  static const Logic* const kSingleton = NewBuiltinModule();
  return kSingleton;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
