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

Value ArrayUtils::AliasArray(
    memory::MemoryManager* memory_manager,
    memory::MemoryRoot* memory_root,
    int size, const Value elements[]) {
  struct RootPlusArray : public memory::MemoryRoot {
    int size;
    const Value** values;
    memory::MemoryRoot* next;

    // implements MemoryRoot -----------------------------------------
    void Accept(memory::MemoryVisitor* visitor) const {
      if (size > 0) {
        visitor->VisitArrayPointer(values, size);
      }
      next->Accept(visitor);
    }
  };

  RootPlusArray root;
  root.size = size;
  root.values = &elements;
  root.next = memory_root;

  ArrayData* array_data = memory_manager->Allocate<ArrayData>(&root);

  array_data->size = size;
  array_data->elements = elements;

  return Value(&kLogic, Data(array_data));
}

void ArrayUtils::GetArrayId(Data data, char prefix, ValueId* output) {
  const ArrayData* array_data = data.As<ArrayData>();

  Hasher hasher;
  hasher.AddValue(array_data->size);
  for (int i = 0; i < array_data->size; i++) {
    ValueId sub_id;
    array_data->elements[i].GetId(&sub_id);
    hasher.AddString(sub_id.GetBytes());
  }

  output->SetBytesFromHasher(prefix, &hasher);
}

int ArrayUtils::MarshalArray(
    Data data, bool is_tuple,
    distribute::Marshaller* marshaller,
    const ValueId* known_id) {
  const ArrayData* array_data = data.As<ArrayData>();

  // We expect that tuples are generally small and unique, so eliminating dups
  // may not be worth the effort.  Arrays, on the other hand, probably are
  // worth it.
  if (!is_tuple) {
    // TODO(kenton):  Decide when to try to send the ID only.
    distribute::Marshaller::ProvisionalIndex result =
        marshaller->TryAvoidMarshalling(
          ValueId(), Value(&kLogic, data),
          distribute::Marshaller::ELIMINATE_DUPS);
    if (result != distribute::Marshaller::kInvalidIndex) {
      return result;
    }
  }

  distribute::Marshaller::ProvisionalIndex result =
      marshaller->AddSequence(
        ValueId(),
        is_tuple ? distribute::Marshaller::TUPLE
                 : distribute::Marshaller::ARRAY,
        array_data->elements, array_data->size);

  if (!is_tuple) {
    marshaller->RecordForDupElimination(Value(&kLogic, data), result);
  }
  return result;
}

bool ArrayUtils::TryWithdraw(memory::MemoryManager* memory_manager,
                             const Value& value) {
  GOOGLE_DCHECK(IsArray(value) || IsTuple(value));

  const ArrayData* data = value.GetData().As<ArrayData>();

  if (data == &kZeroSizeArrayData) return true;
  if (!memory_manager->TryWithdraw(data)) return false;
  if (data->size == 0) return true;

  // Even if withdrawing the array fails, we need to return true here to
  // let the caller know that the array is no longer valid.
  EVLAN_EXPECT_SOMETIMES(
      memory_manager->TryWithdrawArray(data->elements, data->size));
  return true;
}

// ===================================================================

const ArrayUtils::ArrayLogic ArrayUtils::kLogic;
const ArrayUtils::TupleLogic ArrayUtils::kTupleLogic;
const ArrayUtils::ArrayData ArrayUtils::kZeroSizeArrayData(0);

int ArrayUtils::ArrayData::Accept(memory::MemoryVisitor* visitor) const {
  if (size > 0) {
    visitor->VisitArrayPointer(&elements, size);
  }
  return sizeof(*this);
}

ArrayUtils::ArrayLogic::ArrayLogic() {}
ArrayUtils::ArrayLogic::~ArrayLogic() {}

void ArrayUtils::ArrayLogic::Run(Context* context) const {
  context->GetSupportCodeManager()
         ->GetSupportCode(SupportCodeManager::ARRAY)
         ->Run(context);
}

void ArrayUtils::ArrayLogic::Accept(const Data* data,
                                    memory::MemoryVisitor* visitor) const {
  if (data->As<ArrayData>() != &kZeroSizeArrayData) {
    data->AcceptAs<ArrayData>(visitor);
  }
}

void ArrayUtils::ArrayLogic::GetId(Data data, ValueId* output) const {
  GetArrayId(data, '{', output);
}

int ArrayUtils::ArrayLogic::Marshal(
    Data data, distribute::Marshaller* marshaller,
    const ValueId* known_id) const {
  return MarshalArray(data, false, marshaller, known_id);
}

void ArrayUtils::ArrayLogic::DebugPrint(Data data, int depth,
                                        google::protobuf::io::Printer* printer) const {
  const ArrayData* array_data = data.As<ArrayData>();

  printer->Print("{");
  for (int i = 0; i < array_data->size; i++) {
    if (i > 0) printer->Print(", ");

    if (depth <= 0) {
      printer->Print("...");
    } else {
      array_data->elements[i].DebugPrint(depth - 1, printer);
    }
  }
  printer->Print("}");
}

ArrayUtils::TupleLogic::TupleLogic() {}
ArrayUtils::TupleLogic::~TupleLogic() {}

void ArrayUtils::TupleLogic::Run(Context* context) const {
  if (TypeErrorUtils::IsError(context->GetParameter())) {
    context->Return(context->GetParameter());
    return;
  }
  if (!IsInteger(context->GetParameter())) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Tuple index must be integer."));
    return;
  }

  const ArrayData* array_data = context->GetData().As<ArrayData>();
  int index = GetIntegerValue(context->GetParameter());
  if (index< 0 || index >= array_data->size) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Tuple index out of bounds."));
    return;
  }

  context->Return(array_data->elements[index]);
}

void ArrayUtils::TupleLogic::Accept(const Data* data,
                                    memory::MemoryVisitor* visitor) const {
  if (data->As<ArrayData>() != &kZeroSizeArrayData) {
    data->AcceptAs<ArrayData>(visitor);
  }
}

void ArrayUtils::TupleLogic::GetId(Data data, ValueId* output) const {
  GetArrayId(data, '(', output);
}

int ArrayUtils::TupleLogic::Marshal(
    Data data, distribute::Marshaller* marshaller,
    const ValueId* known_id) const {
  return MarshalArray(data, true, marshaller, known_id);
}

void ArrayUtils::TupleLogic::DebugPrint(Data data, int depth,
                                        google::protobuf::io::Printer* printer) const {
  const ArrayData* array_data = data.As<ArrayData>();

  printer->Print("(");
  for (int i = 0; i < array_data->size; i++) {
    if (i > 0) printer->Print(", ");

    if (depth <= 0) {
      printer->Print("...");
    } else {
      array_data->elements[i].DebugPrint(depth - 1, printer);
    }
  }
  printer->Print(")");
}

// ===================================================================
// Array builder
//
// The array built-in module contains a function called "builder".  This
// function takes as its parameter the size of the array to build.  It
// returns another function which should be called on the array's first
// element, which then returns another function to be called on the
// second, and so on, until all elements have been provided, at which
// point the final array is returned.  In other words, you could build
// the array {@a, @b, @c} like so:
//
//   import "builtin".array.builder(3)(@a)(@b)(@c)
//
// In practice, though, you would just write "{@a, @b, @c}" and the
// compiler front-end would automatically convert this into an appropriate
// array builder call.
//
// There is also a similar function called tupleBuilder which works exactly
// like above but returns a tuple.

// The Data for an array builder is a pointer to an ElementList listing
// the array's elements in reverse order.  Calling the builder adds another
// element to this list until all elements have been given.
struct ArrayUtils::ElementList {
  Value first;
  const ElementList* rest;

  int Accept(memory::MemoryVisitor* visitor) const {
    first.Accept(visitor);
    if (rest != NULL) {
      visitor->VisitPointer(&rest);
    }
    return sizeof(*this);
  }
};

// Used by BuildLargeArrayLogic.  Since the array could be arbitrarily large,
// we must keep track of the expected size in memory.
struct ArrayUtils::ListAndSize {
  // The ElementList so far.
  const ElementList* list;

  // The number of elements we should add to the list before building the
  // array.
  int remaining_size;

  int Accept(memory::MemoryVisitor* visitor) const {
    if (list != NULL) {
      visitor->VisitPointer(&list);
    }
    return sizeof(*this);
  }
};

// -------------------------------------------------------------------

// The Logic for an array builder which expects a fixed number of inputs
// (in addition to the elements already in the ElementList).  Using a fixed
// size allows us to avoid storing the expected array size in memory.  That
// is, instead of having the array builder's Data contain information about
// the size of the array, we make this be part of its Logic by choosing a
// Logic that expects a particular size.
//
// Note that we only have BuildFixedArrayLogics for arrays up to a size of
// kMaxFixedArraySize (16).  This is intended to cover the common cases of
// small arrays and tuples, especially parameter lists.  Larger arrays will
// need to be built using some other mechanism.
class ArrayUtils::BuildFixedArrayLogic : public Logic {
 public:
  // Actually, instead of storing the number of elements, we store a pointer
  // to the next array builder Logic in sequence.  So, each time the builder
  // is called, it adds an element to the ElementList and then moves to the
  // next Logic in the sequence.  The final Logic actually returns the complete
  // array when called.
  explicit BuildFixedArrayLogic(const Logic* next);
  ~BuildFixedArrayLogic();

  // implements Logic ----------------------------------------------
  void Run(Context* context) const;
  void Accept(const Data* data,
              memory::MemoryVisitor* visitor) const;
  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const;

 private:
  const Logic* next_;
};

ArrayUtils::BuildFixedArrayLogic::BuildFixedArrayLogic(const Logic* next)
  : next_(next) {}
ArrayUtils::BuildFixedArrayLogic::~BuildFixedArrayLogic() {}

void ArrayUtils::BuildFixedArrayLogic::Run(Context* context) const {
  ElementList* list = context->Allocate<ElementList>();
  list->first = context->GetParameter();
  list->rest = context->GetData().As<ElementList>();
  context->Return(Value(next_, Data(list)));
}

void ArrayUtils::BuildFixedArrayLogic::Accept(
    const Data* data,
    memory::MemoryVisitor* visitor) const {
  if (data->As<ElementList>() != NULL) {
    data->AcceptAs<ElementList>(visitor);
  }
}

void ArrayUtils::BuildFixedArrayLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  // Note:  This code is reused by BuildLargeArrayLogic::DebugPrint() (below),
  //   so the array may actually be larger than kMaxFixedArraySize.
  printer->Print("partialArray({");

  const ElementList* elements = data.As<ElementList>();
  bool is_first = true;
  while (elements != NULL) {
    if (is_first) {
      is_first = false;
    } else {
      printer->Print(", ");
    }

    if (depth > 0) {
      elements->first.DebugPrint(depth - 1, printer);
    } else {
      printer->Print("...");
    }

    elements = elements->rest;
  }

  printer->Print("})");
}

// -------------------------------------------------------------------

// The last Logic in the chain of Logics described above is the
// FinishArrayLogic.  This takes one more element as its parameter and
// then builds an array out of it and the ElementList pointed to by its
// Data.
class ArrayUtils::FinishArrayLogic : public Logic {
 public:
  explicit FinishArrayLogic(const Logic* final_logic);
  ~FinishArrayLogic();

  struct TemporaryCopy;

  // implements Logic ----------------------------------------------
  void Run(Context* context) const;
  void Accept(const Data* data,
              memory::MemoryVisitor* visitor) const;
  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const;

 private:
  const Logic* final_logic_;
};

struct ArrayUtils::FinishArrayLogic::TemporaryCopy : public memory::MemoryRoot {
  // We fill in elements starting from the back of this array, because the
  // list we receive is in reverse order.
  Value elements[kMaxFixedArraySize];
  int size;
  Value callback;

  inline Value* GetFront() {
    return elements + arraysize(elements) - size;
  }

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    for (int i = arraysize(elements) - size; i < arraysize(elements); i++) {
      elements[i].Accept(visitor);
    }
    callback.Accept(visitor);
  }
};

ArrayUtils::FinishArrayLogic::FinishArrayLogic(const Logic* final_logic)
  : final_logic_(final_logic) {}
ArrayUtils::FinishArrayLogic::~FinishArrayLogic() {}

void ArrayUtils::FinishArrayLogic::Run(Context* context) const {
  TemporaryCopy temp;
  temp.size = 0;
  temp.callback = context->GetCallback();

  ++temp.size;
  *temp.GetFront() = context->GetParameter();

  const ElementList* list = context->GetData().As<ElementList>();

  while (list != NULL) {
    EVLAN_EXPECT_SOMETIMES(context->GetMemoryManager()->TryWithdraw(list));
    ++temp.size;
    *temp.GetFront() = list->first;
    list = list->rest;
  }

  Value* elements =
    context->GetMemoryManager()->AllocateArray<Value>(temp.size, &temp);
  for (int i = 0; i < temp.size; i++) {
    elements[i] = temp.GetFront()[i];
  }

  Value result = AliasArray(context->GetMemoryManager(),
                            &temp, temp.size, elements);
  result = Value(final_logic_, result.GetData());

  context->SetNext(temp.callback, result);
}

void ArrayUtils::FinishArrayLogic::Accept(
    const Data* data,
    memory::MemoryVisitor* visitor) const {
  if (data->As<ElementList>() != NULL) {
    data->AcceptAs<ElementList>(visitor);
  }
}

void ArrayUtils::FinishArrayLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  BuildFixedArrayLogic(NULL).DebugPrint(data, depth, printer);
}

// -------------------------------------------------------------------

const Logic* const ArrayUtils::kFixedArrayLogics[kMaxFixedArraySize] = {
  new FinishArrayLogic(&ArrayUtils::kLogic),
  new BuildFixedArrayLogic(kFixedArrayLogics[0]),
  new BuildFixedArrayLogic(kFixedArrayLogics[1]),
  new BuildFixedArrayLogic(kFixedArrayLogics[2]),
  new BuildFixedArrayLogic(kFixedArrayLogics[3]),
  new BuildFixedArrayLogic(kFixedArrayLogics[4]),
  new BuildFixedArrayLogic(kFixedArrayLogics[5]),
  new BuildFixedArrayLogic(kFixedArrayLogics[6]),
  new BuildFixedArrayLogic(kFixedArrayLogics[7]),
  new BuildFixedArrayLogic(kFixedArrayLogics[8]),
  new BuildFixedArrayLogic(kFixedArrayLogics[9]),
  new BuildFixedArrayLogic(kFixedArrayLogics[10]),
  new BuildFixedArrayLogic(kFixedArrayLogics[11]),
  new BuildFixedArrayLogic(kFixedArrayLogics[12]),
  new BuildFixedArrayLogic(kFixedArrayLogics[13]),
  new BuildFixedArrayLogic(kFixedArrayLogics[14]),
};

const Logic* const ArrayUtils::kFixedTupleLogics[kMaxFixedArraySize] = {
  new FinishArrayLogic(&ArrayUtils::kTupleLogic),
  new BuildFixedArrayLogic(kFixedTupleLogics[0]),
  new BuildFixedArrayLogic(kFixedTupleLogics[1]),
  new BuildFixedArrayLogic(kFixedTupleLogics[2]),
  new BuildFixedArrayLogic(kFixedTupleLogics[3]),
  new BuildFixedArrayLogic(kFixedTupleLogics[4]),
  new BuildFixedArrayLogic(kFixedTupleLogics[5]),
  new BuildFixedArrayLogic(kFixedTupleLogics[6]),
  new BuildFixedArrayLogic(kFixedTupleLogics[7]),
  new BuildFixedArrayLogic(kFixedTupleLogics[8]),
  new BuildFixedArrayLogic(kFixedTupleLogics[9]),
  new BuildFixedArrayLogic(kFixedTupleLogics[10]),
  new BuildFixedArrayLogic(kFixedTupleLogics[11]),
  new BuildFixedArrayLogic(kFixedTupleLogics[12]),
  new BuildFixedArrayLogic(kFixedTupleLogics[13]),
  new BuildFixedArrayLogic(kFixedTupleLogics[14]),
};

// -------------------------------------------------------------------

// This is like BuildFixedArrayLogic except that it could be any arbitrary
// size, and the data pointer points to a ListAndSize.
class ArrayUtils::BuildLargeArrayLogic : public Logic {
 public:
  BuildLargeArrayLogic();
  ~BuildLargeArrayLogic();

  // implements Logic ----------------------------------------------
  void Run(Context* context) const;
  void Accept(const Data* data,
              memory::MemoryVisitor* visitor) const;
  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const;

};

ArrayUtils::BuildLargeArrayLogic ArrayUtils::kLargeArrayBuilderLogic;
ArrayUtils::BuildLargeArrayLogic ArrayUtils::kLargeTupleBuilderLogic;

ArrayUtils::BuildLargeArrayLogic::BuildLargeArrayLogic() {}
ArrayUtils::BuildLargeArrayLogic::~BuildLargeArrayLogic() {}

void ArrayUtils::BuildLargeArrayLogic::Run(Context* context) const {
  const ListAndSize* old_list_and_size = context->GetData().As<ListAndSize>();
  int remaining_size = old_list_and_size->remaining_size;

  if (remaining_size == 1) {
    // Build the final array.

    // Compute the array size.
    int size = 1;
    const ElementList* list = old_list_and_size->list;
    while (list != NULL) {
      ++size;
      list = list->rest;
    }

    // Allocate the array.
    Value* array = context->AllocateArray<Value>(size);

    // Note: old_list_and_size may no longer be valid, so we have to call
    //   GetData() again.
    list = context->GetData().As<ListAndSize>()->list;

    int pos = size;
    array[--pos] = context->GetParameter();

    while (list != NULL) {
      array[--pos] = list->first;
      list = list->rest;
    }
    GOOGLE_DCHECK_EQ(pos, 0);

    class FinalArrayRoot : public memory::MemoryRoot {
     public:
      Value callback_;

      // implements MemoryRoot -----------------------------------------
      void Accept(memory::MemoryVisitor* visitor) const {
        callback_.Accept(visitor);
      }
    };
    FinalArrayRoot root;
    root.callback_ = context->GetCallback();

    Value result = AliasArray(context->GetMemoryManager(), &root, size, array);
    if (this == &kLargeTupleBuilderLogic) {
      result = ArrayToTuple(result);
    }
    context->SetNext(root.callback_, result);

  } else {
    // Add an element to the list.
    class ElementListRoot : public memory::MemoryRoot {
     public:
      Value parameter_;
      Value callback_;
      const ElementList* list_;

      // implements MemoryRoot -----------------------------------------
      void Accept(memory::MemoryVisitor* visitor) const {
        parameter_.Accept(visitor);
        callback_.Accept(visitor);
        if (list_ != NULL) {
          visitor->VisitPointer(&list_);
        }
      }
    };
    ElementListRoot root;

    root.parameter_ = context->GetParameter();
    root.callback_ = context->GetCallback();
    root.list_ = old_list_and_size->list;

    // This is no longer needed.  Remove it to save heap space.
    EVLAN_EXPECT_SOMETIMES(
        context->GetMemoryManager()->TryWithdraw(old_list_and_size));

    // Create the new list node.
    ElementList* new_list =
      context->GetMemoryManager()->Allocate<ElementList>(&root);
    new_list->first = context->GetParameter();
    new_list->rest = root.list_;
    root.list_ = new_list;

    // Create the new ListAndSize.
    ListAndSize* new_data =
      context->GetMemoryManager()->Allocate<ListAndSize>(&root);
    new_data->list = root.list_;
    new_data->remaining_size = remaining_size - 1;

    // Done.
    context->SetNext(root.callback_, Value(this, Data(new_data)));
  }
}

void ArrayUtils::BuildLargeArrayLogic::Accept(
    const Data* data,
    memory::MemoryVisitor* visitor) const {
  data->AcceptAs<ListAndSize>(visitor);
}

void ArrayUtils::BuildLargeArrayLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  // Reuse BuildFixedArrayLogic::DebugPrint().
  BuildFixedArrayLogic dummy(NULL);
  dummy.DebugPrint(Data(data.As<ListAndSize>()->list), depth, printer);
}

// -------------------------------------------------------------------

void ArrayUtils::MakeArrayBuilder(Context* context, int size) {
  if (size < 0) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Tried to build negative-sized array."));
    return;
  }

  if (size == 0) {
    context->Return(Value(&kLogic, Data(&kZeroSizeArrayData)));
  } else if (size <= kMaxFixedArraySize) {
    context->Return(Value(kFixedArrayLogics[size - 1], Data()));
  } else {
    ListAndSize* list_and_size = context->Allocate<ListAndSize>();
    list_and_size->list = NULL;
    list_and_size->remaining_size = size;
    context->Return(Value(&kLargeArrayBuilderLogic, Data(list_and_size)));
  }
}

void ArrayUtils::MakeTupleBuilder(Context* context, int size) {
  if (size < 0) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Tried to build negative-sized tuple."));
    return;
  }

  if (size == 0) {
    context->Return(Value(&kTupleLogic, Data(&kZeroSizeArrayData)));
  } else if (size <= kMaxFixedArraySize) {
    context->Return(Value(kFixedTupleLogics[size - 1], Data()));
  } else {
    ListAndSize* list_and_size = context->Allocate<ListAndSize>();
    list_and_size->list = NULL;
    list_and_size->remaining_size = size;
    context->Return(Value(&kLargeTupleBuilderLogic, Data(list_and_size)));
  }
}

void ArrayUtils::BuiltinArraySize(Context* context, Value array) {
  if (!IsArray(array)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to native function arraySize().  Expected array, "
      "got: " + array.DebugString()));
    return;
  }

  context->Return(MakeInteger(GetArraySize(array)));
}

void ArrayUtils::BuiltinGetElement(Context* context, Value array, int index) {
  if (!IsArray(array)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to native function arrayGetElement().  Expected "
      "array, got: " + array.DebugString()));
    return;
  }

  const ArrayData* array_data = array.GetData().As<ArrayData>();
  if (index < 0 || index >= array_data->size) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Array index out of bounds."));
    return;
  }

  context->Return(array_data->elements[index]);
}

void ArrayUtils::GetInterval(Context* context,
                             Value array, int start, int end) {
  if (!IsArray(array)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to native function arrayGetInterval().  Expected "
      "array, got: " + array.DebugString()));
    return;
  }

  int size = GetArraySize(array);
  if (start < 0 || start > end || end > size) {
    context->Return(TypeErrorUtils::MakeError(context,
      strings::Substitute(
        "Invalid array interval [$0, $1) for array of size $2.",
        start, end, size)));
    return;
  }

  context->Return(
    AliasArray(context->GetMemoryManager(), context,
               end - start, AsCArray(array) + start));
}

void ArrayUtils::Join(Context* context, Value array) {
  if (!IsArray(array)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to native function arrayJoin().  Expected "
      "array, got: " + array.DebugString()));
    return;
  }

  int array_count = GetArraySize(array);
  const Value* arrays = AsCArray(array);

  int size = 0;
  for (int i = 0; i < array_count; i++) {
    if (!IsArray(arrays[i])) {
      context->Return(TypeErrorUtils::MakeError(context,
        "Wrong type for element to native function arrayJoin().  Expected "
        "array, got: " + arrays[i].DebugString()));
      return;
    }
    size += GetArraySize(arrays[i]);
  }

  Value* elements = context->AllocateArray<Value>(size);

  // may have moved.
  array = context->GetParameter();
  arrays = AsCArray(array);

  int pos = 0;
  for (int i = 0; i < array_count; i++) {
    int array_size = GetArraySize(arrays[i]);
    memcpy(elements + pos, AsCArray(arrays[i]), sizeof(Value) * array_size);
    pos += array_size;
  }

  context->Return(AliasArray(context->GetMemoryManager(), context,
                             size, elements));
}

const Logic* ArrayUtils::NewBuiltinModule() {
  NativeModule* result = new NativeModule("nativeArray");
  result->AddFunction("builder", &MakeArrayBuilder);
  result->AddFunction("tupleBuilder", &MakeTupleBuilder);
  result->AddFunction("size", &BuiltinArraySize);
  result->AddFunction("element", &BuiltinGetElement);
  result->AddFunction("interval", &GetInterval);
  result->AddFunction("join", &Join);
  return result;
}

const Logic* ArrayUtils::GetBuiltinModule() {
  static const Logic* const kSingleton = NewBuiltinModule();
  return kSingleton;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
