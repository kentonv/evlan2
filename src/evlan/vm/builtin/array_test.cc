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
#include "evlan/vm/runtime.h"
#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class ArrayTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    AddImport("array", Value(ArrayUtils::GetBuiltinModule(), Data()));

    // Annoyingly, we cannot evaluate the code "-1" without loading the support
    // code for integers, because it translates to 1.negate().  So, we make -1
    // available as an import instead.
    AddImport("-1", MakeInteger(-1));
  }
};

TEST_F(ArrayTest, Helpers) {
  Value* elements =
    GetMemoryManager()->AllocateArray<Value>(4, GetMemoryRoot());

  elements[0] = MakeInteger(12);
  elements[1] = MakeInteger(34);
  elements[2] = MakeInteger(56);
  elements[3] = MakeInteger(78);

  Value array =
    ArrayUtils::AliasArray(GetMemoryManager(), GetMemoryRoot(), 4, elements);

  EXPECT_TRUE(ArrayUtils::IsArray(array));
  EXPECT_EQ(ArrayUtils::GetArrayLogic(), array.GetLogic());
  EXPECT_EQ(4, ArrayUtils::GetArraySize(array));
  EXPECT_EQ(56, GetIntegerValue(ArrayUtils::GetElement(array, 2)));
  EXPECT_EQ(34, GetIntegerValue(ArrayUtils::AsCArray(array)[1]));

  Value tuple = ArrayUtils::ArrayToTuple(array);

  EXPECT_TRUE(ArrayUtils::IsTuple(tuple));
  EXPECT_EQ(4, ArrayUtils::GetArraySize(tuple));
  EXPECT_EQ(ArrayUtils::AsCArray(array), ArrayUtils::AsCArray(tuple));
  EXPECT_EQ(56, GetIntegerValue(ArrayUtils::GetElement(tuple, 2)));

  Value back_to_array = ArrayUtils::TupleToArray(tuple);

  EXPECT_EQ(array.GetLogic(), back_to_array.GetLogic());
  EXPECT_EQ(array.GetData().As<void>(), back_to_array.GetData().As<void>());
}

TEST_F(ArrayTest, Builder) {
  ExpectEvaluatesToDebugString("{12, 34, 56, 78}",
    "import \"array\".builder(4)(12)(34)(56)(78)");

  ExpectEvaluatesToDebugString("{}", "import \"array\".builder(0)");

  // Test an array large enough that it must use BuildLargeArrayLogic.
  ExpectEvaluatesToDebugString(
    "{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17}",
    "import \"array\".builder(18)(0)(1)(2)(3)(4)(5)(6)(7)(8)(9)"
                                "(10)(11)(12)(13)(14)(15)(16)(17)");

  // See SetUp() for why we import -1 instead of just writing it normally.
  ExpectEvaluatesToError("Tried to build negative-sized array.",
    "import \"array\".builder(import \"-1\")");

  ExpectEvaluatesToDebugString("(12, 34, 56, 78)",
    "import \"array\".tupleBuilder(4)(12)(34)(56)(78)");
  ExpectEvaluatesToDebugString(
    "(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17)",
    "import \"array\".tupleBuilder(18)(0)(1)(2)(3)(4)(5)(6)(7)(8)(9)"
                                     "(10)(11)(12)(13)(14)(15)(16)(17)");
}

TEST_F(ArrayTest, Size) {
  ExpectEvaluatesToInteger(3, "import \"array\".size({1, 2, 3})");

  ExpectEvaluatesToError(
    "Wrong type passed to native function arraySize().  "
      "Expected array, got: 123",
    "import \"array\".size(123)");
}

TEST_F(ArrayTest, Element) {
  ExpectEvaluatesToInteger(56, "import \"array\".element({12, 34, 56, 78}, 2)");

  ExpectEvaluatesToError(
    "Array index out of bounds.",
    "import \"array\".element({12, 34, 56, 78}, 4)");

  // See SetUp() for why we import -1 instead of just writing it normally.
  ExpectEvaluatesToError(
    "Array index out of bounds.",
    "import \"array\".element({12, 34, 56, 78}, import \"-1\")");

  ExpectEvaluatesToError(
    "Wrong type passed to native function arrayGetElement().  "
      "Expected array, got: 123",
    "import \"array\".element(123, 2)");
}

TEST_F(ArrayTest, Interval) {
  ExpectEvaluatesToDebugString("{34, 56}",
    "import \"array\".interval({12, 34, 56, 78}, 1, 3)");

  ExpectEvaluatesToDebugString("{12, 34, 56, 78}",
    "import \"array\".interval({12, 34, 56, 78}, 0, 4)");

  // See SetUp() for why we import -1 instead of just writing it normally.
  ExpectEvaluatesToError(
    "Invalid array interval [-1, 2) for array of size 4.",
    "import \"array\".interval({12, 34, 56, 78}, import \"-1\", 2)");

  ExpectEvaluatesToError(
    "Invalid array interval [2, 5) for array of size 4.",
    "import \"array\".interval({12, 34, 56, 78}, 2, 5)");

  ExpectEvaluatesToError(
    "Wrong type passed to native function arrayGetInterval().  "
      "Expected array, got: 123",
    "import \"array\".interval(123, 1, 2)");
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
