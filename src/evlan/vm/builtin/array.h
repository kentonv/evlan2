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

#ifndef EVLAN_VM_ARRAY_H__
#define EVLAN_VM_ARRAY_H__

#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/vm/runtime.h"

namespace evlan {
namespace vm {
namespace builtin {

// Utilities for dealing with arrays.
//
// There are actually two similar but distinct types implemented here:  arrays
// and tuples.  Arrays are objects that have a set of members, such as "size"
// and "element()".  Tuples are simple functions that take an integer as input
// and return the corresponding element.  Tuples are mostly used when calling
// multi-parameter functions -- the parameters are packed into a tuple, and
// that is used as the function's input.  Since the function knows what its
// own inputs are, it has no need to check the size of the tuple.  Arrays are
// used everywhere else, particularly when the expected size of the array is
// not known ahead of time.
class ArrayUtils {
 public:
  // Get the Logic associated with array Values.
  static inline const Logic* GetArrayLogic();

  // Is this Value an array?
  static inline bool IsArray(const Value& value);

  // Is this Value a tuple?
  static inline bool IsTuple(const Value& value);

  // Get the size of the array or tuple.
  static inline int GetArraySize(const Value& value);

  // Get an element of the array or tuple.
  static inline const Value& GetElement(const Value& value, int index);

  // Get the location where the array or tuple's values are found in memory.
  // In other words, this converts the array into a C++-style array.
  static inline const Value* AsCArray(const Value& value);

  // Arrays and tuples use the same data, so converting between them just
  // means changing the logic pointer.  These functions do that for you.
  static inline Value ArrayToTuple(const Value& array);
  static inline Value TupleToArray(const Value& tuple);

  // Creates a new array Value that points at the given elements.  The
  // elements must reside in managed memory.
  static Value AliasArray(memory::MemoryManager* memory_manager,
                          memory::MemoryRoot* memory_root,
                          int size, const Value elements[]);

  // Get an empty array or tuple (no allocation needed).
  static inline Value EmptyArray();
  static inline Value EmptyTuple();

  // Try to withdraw the given array or tuple value from the top of the heap.
  // Returns true if the ArrayData and element array were both withdrawn.
  // Does not make any attempt to withdraw the things that the elements point
  // at.
  static bool TryWithdraw(memory::MemoryManager* memory_manager,
                          const Value& value);

  // Returns a Logic for an object containing various functions that operate on
  // arrays.  This Logic completely ignores its associated Data.
  static const Logic* GetBuiltinModule();

 private:
  struct ArrayData {
    int size;
    const Value* elements;

    int Accept(memory::MemoryVisitor* visitor) const;

    inline ArrayData() {}
    inline ArrayData(int size): size(size), elements(NULL) {}
  };

  class ArrayLogic : public Logic {
   public:
    ArrayLogic();
    ~ArrayLogic();

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

  class TupleLogic : public Logic {
   public:
    TupleLogic();
    ~TupleLogic();

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

  struct ElementList;
  struct ListAndSize;
  class BuildFixedArrayLogic;
  class FinishArrayLogic;
  class BuildLargeArrayLogic;

  static const ArrayLogic kLogic;
  static const TupleLogic kTupleLogic;
  static const ArrayData kZeroSizeArrayData;
  static const int kMaxFixedArraySize = 16;
  static const Logic* const kFixedArrayLogics[kMaxFixedArraySize];
  static const Logic* const kFixedTupleLogics[kMaxFixedArraySize];
  static BuildLargeArrayLogic kLargeArrayBuilderLogic;
  static BuildLargeArrayLogic kLargeTupleBuilderLogic;

  static void GetArrayId(Data data, char prefix, ValueId* output);
  static int MarshalArray(Data data, bool is_tuple,
                          distribute::Marshaller* marshaller,
                          const ValueId* known_id = NULL);

  // -----------------------------------------------------------------
  // Built-in functions.

  static void MakeArrayBuilder(Context* context, int size);
  static void MakeTupleBuilder(Context* context, int size);
  static void BuiltinArraySize(Context* context, Value array);
  static void BuiltinGetElement(Context* context, Value array, int index);
  static void GetInterval(Context* context, Value array, int start, int end);
  static void Join(Context* context, Value array);

  static const Logic* NewBuiltinModule();
};

// ===================================================================

inline const Logic* ArrayUtils::GetArrayLogic() {
  return &kLogic;
}

inline bool ArrayUtils::IsArray(const Value& value) {
  return value.GetLogic() == &kLogic;
}

inline bool ArrayUtils::IsTuple(const Value& value) {
  return value.GetLogic() == &kTupleLogic;
}

inline int ArrayUtils::GetArraySize(const Value& value) {
  GOOGLE_DCHECK(IsArray(value) || IsTuple(value));
  return value.GetData().As<ArrayData>()->size;
}

inline const Value& ArrayUtils::GetElement(const Value& value, int index) {
  GOOGLE_DCHECK(IsArray(value) || IsTuple(value));
  GOOGLE_DCHECK_GE(index, 0);
  GOOGLE_DCHECK_LT(index, value.GetData().As<ArrayData>()->size);

  return value.GetData().As<ArrayData>()->elements[index];
}

inline const Value* ArrayUtils::AsCArray(const Value& value) {
  GOOGLE_DCHECK(IsArray(value) || IsTuple(value));
  return value.GetData().As<ArrayData>()->elements;
}

inline Value ArrayUtils::ArrayToTuple(const Value& array) {
  GOOGLE_DCHECK(IsArray(array));
  return Value(&kTupleLogic, array.GetData());
}

inline Value ArrayUtils::TupleToArray(const Value& tuple) {
  GOOGLE_DCHECK(IsTuple(tuple));
  return Value(&kLogic, tuple.GetData());
}

inline Value ArrayUtils::EmptyArray() {
  return Value(&kLogic, Data(&kZeroSizeArrayData));
}

inline Value ArrayUtils::EmptyTuple() {
  return Value(&kTupleLogic, Data(&kZeroSizeArrayData));
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_ARRAY_H__
