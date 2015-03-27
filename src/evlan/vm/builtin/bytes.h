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

#ifndef EVLAN_VM_BYTES_H__
#define EVLAN_VM_BYTES_H__

#include "evlan/common.h"
#include "evlan/stringpiece.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/vm/runtime.h"

namespace evlan {
namespace vm {
namespace builtin {

// Utilities for dealing with byte arrays.  At present, byte arrays and strings
// are the same thing.  Later on, we may want to add a layer of Evlan code
// around strings to make them more unicode-friendly, but they will still
// probably wrap a UTF-8 byte array.
class ByteUtils {
 public:
  // Get the Logic used with byte array values.
  inline static const Logic* GetByteArrayLogic();

  // Is this value a byte array?
  inline static bool IsByteArray(const Value& value);

  // Get the bytes from a byte array Value or Data.  Requires that the Value
  // is a byte array.  Note that the returned StringPiece may point at memory
  // that is tracked by the MemoryManager and may disappear when the
  // MemoryManager is next called.
  inline static StringPiece ToString(const Value& byte_array);
  inline static StringPiece DataToString(const Data& byte_array);

  // Construct a byte array Value which points at (aliases) an existing set of
  // bytes or copies them.  In both cases, |data| must point at memory that is
  // *not* tracked by the MemoryManager.
  inline static Value AliasString(memory::MemoryManager* memory_manager,
                                  const memory::MemoryRoot* memory_root,
                                  const StringPiece& data);
  inline static Value CopyString(memory::MemoryManager* memory_manager,
                                 const memory::MemoryRoot* memory_root,
                                 const StringPiece& data);

  // Construct a byte array Value by aliasing or copying bytes that may or may
  // not point at memory tracked by the MemoryManager.  The MemoryRoot does not
  // need to ensure that these bytes stay reachable; the functions will take
  // care of that.
  static Value AliasTrackedBytes(memory::MemoryManager* memory_manager,
                                 const memory::MemoryRoot* memory_root,
                                 const byte* data, int size);
  static Value AliasUntrackedBytes(memory::MemoryManager* memory_manager,
                                   const memory::MemoryRoot* memory_root,
                                   const byte* data, int size);
  static Value CopyUntrackedBytes(memory::MemoryManager* memory_manager,
                                  const memory::MemoryRoot* memory_root,
                                  const byte* data, int size);

  // Returns a Logic for an object containing various functions that operate on
  // byte arrays.  This Logic completely ignores its associated Data.
  static const Logic* GetBuiltinModule();

 private:
  // The Data part of a byte array Value contains a pointer to a ByteArrayData.
  struct ByteArrayData {
    int size;
    bool tracked;
    const byte* data;

    int Accept(memory::MemoryVisitor* visitor) const;
  };

  class ByteArrayLogic : public Logic {
   public:
    ByteArrayLogic();
    ~ByteArrayLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void GetId(Data data, ValueId* output) const;
    int Marshal(Data data, distribute::Marshaller* marshaller,
                const ValueId* known_id = NULL) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  // Builtin functions.
  static int GetSize(StringPiece bytes);
  static void GetElement(Context* context, StringPiece bytes, int index);
  static void GetInterval(Context* context,
                          StringPiece bytes, int start, int end);
  static void Split(Context* context, StringPiece data, StringPiece delim);

  static const Logic* NewBuiltinModule();

  static const ByteArrayLogic kLogic;
  static const ByteArrayData kZeroSizeArrayData;
};

// ===================================================================

inline const Logic* ByteUtils::GetByteArrayLogic() { return &kLogic; }

inline bool ByteUtils::IsByteArray(const Value& value) {
  return value.GetLogic() == &kLogic;
}

inline StringPiece ByteUtils::ToString(const Value& byte_array) {
  const ByteArrayData* array_data = byte_array.GetData().As<ByteArrayData>();
  return StringPiece(reinterpret_cast<const char*>(array_data->data),
                     array_data->size);
}

inline StringPiece ByteUtils::DataToString(const Data& byte_array) {
  const ByteArrayData* array_data = byte_array.As<ByteArrayData>();
  return StringPiece(reinterpret_cast<const char*>(array_data->data),
                     array_data->size);
}

inline Value ByteUtils::AliasString(
    memory::MemoryManager* memory_manager,
    const memory::MemoryRoot* memory_root,
    const StringPiece& data) {
  return AliasUntrackedBytes(memory_manager, memory_root,
    reinterpret_cast<const byte*>(data.data()), data.size());
}

inline Value ByteUtils::CopyString(
    memory::MemoryManager* memory_manager,
    const memory::MemoryRoot* memory_root,
    const StringPiece& data) {
  return CopyUntrackedBytes(memory_manager, memory_root,
    reinterpret_cast<const byte*>(data.data()), data.size());
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_BYTES_H__
