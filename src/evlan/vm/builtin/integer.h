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

#ifndef EVLAN_VM_BUILTIN_INTEGER_H__
#define EVLAN_VM_BUILTIN_INTEGER_H__

#include "evlan/common.h"
#include "evlan/vm/runtime.h"
#include "evlan/common.h"

namespace evlan {
namespace vm {
namespace builtin {

// Get the Logic associated with integer values.
const Logic* GetIntegerLogic();

// Convert a Value to a native integer.  Requires that the input value is,
// in fact, an integer.
inline int GetIntegerValue(const Value& value) {
  GOOGLE_DCHECK_EQ(value.GetLogic(), GetIntegerLogic());
  return value.GetData().AsInteger();
}

// Convert a native integer to a Value.
inline Value MakeInteger(int i) {
  return Value(GetIntegerLogic(), Data(i));
}

// Check if a Value represents an integer.
inline bool IsInteger(const Value& value) {
  return value.GetLogic() == GetIntegerLogic();
}

// Returns a Logic for an object containing various functions that operate on
// integers.  This Logic completely ignores its associated Data.
const Logic* GetIntegerBuiltins();

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_BUILTIN_INTEGER_H__
