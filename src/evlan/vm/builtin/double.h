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

// Author: Joseph Schorr
//
// Based significantly on the integer.h file (Thanks Kenton!)

#ifndef EVLAN_VM_BUILTIN_DOUBLE_H__
#define EVLAN_VM_BUILTIN_DOUBLE_H__

#include "evlan/common.h"
#include "evlan/vm/runtime.h"
#include "evlan/common.h"

namespace evlan {
namespace vm {
namespace builtin {

// Get the Logic associated with double values.
const Logic* GetDoubleLogic();

// Convert a Value to a native double.  Requires that the input value is,
// in fact, a double.
inline double GetDoubleValue(const Value& value) {
  GOOGLE_DCHECK_EQ(value.GetLogic(), GetDoubleLogic());
  return value.GetData().AsDouble();
}

// Convert a native double to a Value.
inline Value MakeDouble(double d) {
  return Value(GetDoubleLogic(), Data(d));
}

// Check if a Value represents a double.
inline bool IsDouble(const Value& value) {
  return value.GetLogic() == GetDoubleLogic();
}

// Returns a Logic for an object containing various functions that operate on
// doubles.  This Logic completely ignores its associated Data.
const Logic* GetDoubleBuiltins();

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_BUILTIN_DOUBLE_H__
