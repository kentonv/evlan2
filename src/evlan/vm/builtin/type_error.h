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

#ifndef EVLAN_VM_TYPE_ERROR_H__
#define EVLAN_VM_TYPE_ERROR_H__

#include "evlan/common.h"
#include "evlan/stringpiece.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/vm/runtime.h"
#include "evlan/vm/builtin/bytes.h"

namespace evlan {
namespace vm {
namespace builtin {

// Functions for dealing with type errors.
//
// Type errors are returned when a function is called with inputs of the
// wrong type.  Furthermore, if one of the inputs to a function *is* a type
// error, then the function should, in fact, return that type error verbatim.
// Thus, type errors propagate like NaNs in floating-point operations.
//
// By propagating type errors in this way rather than using a mechanism like
// exceptions which directly unwind the stack, we avoid placing restrictions
// on the evaluation order of expressions.  For example, when evaluating:
//   a(b(x), c(x))
// If both b(x) and c(x) produce type errors, under an exception model we would
// catch the exception thrown by whichever function is evaluated first, but
// with the error-value model we can deterministically take the error returned
// by b(x) even if we compute c(x) first.  This also gives the VM more freedom
// to choose between lazy and eager evaluation.
//
// Note that, as the name implies, type errors should only be used to report
// errors that will eventually be caught by the type system.  So, type error
// values should only ever exist when executing dynamically-typed code.
class TypeErrorUtils {
 public:
  // Get the Logic used with type error values.
  inline static const Logic* GetLogic();

  // Is this value a type error?
  inline static bool IsError(const Value& value);

  // Get the human-readable description of a type error value.
  inline static StringPiece GetDescription(const Value& value);

  // Allocate a new type error value with the given description.
  static Value MakeError(memory::MemoryManager* memory_manager,
                         const memory::MemoryRoot* memory_root,
                         const StringPiece& description);

  // Convenience function that gets the memory_manager and memory_root from
  // the given Context.
  inline static Value MakeError(Context* context,
                                const StringPiece& description);

  // Returns a Logic for an object containing various functions that operate on
  // type errors.  This Logic completely ignores its associated Data.
  static const Logic* GetBuiltinModule();

 private:
  // The Logic for a type error.  The Data is the same as a byte array
  // (which contains the description of the error).
  class ErrorLogic : public Logic {
   public:
    ErrorLogic();
    ~ErrorLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  static const ErrorLogic kLogic;

  static const Logic* NewBuiltinModule();
};

// ===================================================================

inline const Logic* TypeErrorUtils::GetLogic() { return &kLogic; }

inline bool TypeErrorUtils::IsError(const Value& value) {
  return value.GetLogic() == &kLogic;
}

inline StringPiece TypeErrorUtils::GetDescription(const Value& value) {
  GOOGLE_DCHECK(IsError(value));
  return ByteUtils::DataToString(value.GetData());
}

inline Value TypeErrorUtils::MakeError(Context* context,
                                       const StringPiece& description) {
  return MakeError(context->GetMemoryManager(), context, description);
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_TYPE_ERROR_H__
