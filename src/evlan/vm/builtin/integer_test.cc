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

#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/runtime.h"
#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class IntegerTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    AddImport("integer", Value(GetIntegerBuiltins(), Data()));
  }
};

TEST_F(IntegerTest, HelperFunctions) {
  Value value = MakeInteger(123);
  ASSERT_TRUE(IsInteger(value));
  EXPECT_EQ(123, GetIntegerValue(value));
}

TEST_F(IntegerTest, DebugString) {
  EXPECT_EQ("123", MakeInteger(123).DebugString());
}

TEST_F(IntegerTest, Arithmetic) {
  ExpectEvaluatesToInteger( 5 + 9, "import \"integer\".add     ( 5, 9)");
  ExpectEvaluatesToInteger(15 - 6, "import \"integer\".subtract(15, 6)");
  ExpectEvaluatesToInteger( 4 * 7, "import \"integer\".multiply( 4, 7)");
  ExpectEvaluatesToInteger(12 / 3, "import \"integer\".divide  (12, 3)");

  ExpectEvaluatesToInteger(-15, "import \"integer\".negate(15)");

  ExpectEvaluatesToError(
    "Integer divided by zero.",
    "import \"integer\".divide(1, 0)");
}

TEST_F(IntegerTest, Comparison) {
  ExpectEvaluatesToBoolean(false, "import \"integer\".equal(1, 2)");
  ExpectEvaluatesToBoolean(true , "import \"integer\".equal(2, 2)");
  ExpectEvaluatesToBoolean(false, "import \"integer\".equal(3, 2)");

  ExpectEvaluatesToBoolean(true , "import \"integer\".notEqual(1, 2)");
  ExpectEvaluatesToBoolean(false, "import \"integer\".notEqual(2, 2)");
  ExpectEvaluatesToBoolean(true , "import \"integer\".notEqual(3, 2)");

  ExpectEvaluatesToBoolean(true , "import \"integer\".less(1, 2)");
  ExpectEvaluatesToBoolean(false, "import \"integer\".less(2, 2)");
  ExpectEvaluatesToBoolean(false, "import \"integer\".less(3, 2)");

  ExpectEvaluatesToBoolean(false, "import \"integer\".greater(1, 2)");
  ExpectEvaluatesToBoolean(false, "import \"integer\".greater(2, 2)");
  ExpectEvaluatesToBoolean(true , "import \"integer\".greater(3, 2)");

  ExpectEvaluatesToBoolean(true , "import \"integer\".notGreater(1, 2)");
  ExpectEvaluatesToBoolean(true , "import \"integer\".notGreater(2, 2)");
  ExpectEvaluatesToBoolean(false, "import \"integer\".notGreater(3, 2)");

  ExpectEvaluatesToBoolean(false, "import \"integer\".notLess(1, 2)");
  ExpectEvaluatesToBoolean(true , "import \"integer\".notLess(2, 2)");
  ExpectEvaluatesToBoolean(true , "import \"integer\".notLess(3, 2)");
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
