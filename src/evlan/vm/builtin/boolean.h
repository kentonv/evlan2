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

#ifndef EVLAN_VM_BUILTIN_BOOLEAN_H__
#define EVLAN_VM_BUILTIN_BOOLEAN_H__

#include "evlan/common.h"
#include "evlan/vm/runtime.h"

namespace evlan {
namespace vm {
namespace builtin {

class BooleanUtils {
 public:
  // Get the Logic used with boolean values.
  static inline const Logic* GetBooleanLogic();

  // Is this value a boolean?
  static inline bool IsBoolean(const Value& value);
  // Convert a Value to a native boolean.
  static inline bool GetBooleanValue(const Value& value);
  // Convert a native boolean to a Value.
  static inline const Value& MakeBoolean(bool value);

  // Returns a Logic for an object containing various functions that operate on
  // booleans.  This Logic completely ignores its associated Data.
  static const Logic* GetBuiltinModule();

 private:
  class BooleanLogic : public Logic {
   public:
    BooleanLogic();
    ~BooleanLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  static const Logic* NewBuiltinModule();

  static BooleanLogic kLogic;
  static Value kTrue;
  static Value kFalse;
};

inline const Logic* BooleanUtils::GetBooleanLogic() {
  return &kLogic;
}

inline bool BooleanUtils::IsBoolean(const Value& value) {
  return value.GetLogic() == &kLogic;
}

inline bool BooleanUtils::GetBooleanValue(const Value& value) {
  GOOGLE_DCHECK(IsBoolean(value));
  return value.GetData().AsInteger() != 0;
}

inline const Value& BooleanUtils::MakeBoolean(bool value) {
  return value ? kTrue : kFalse;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_BUILTIN_BOOLEAN_H__

