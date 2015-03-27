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
// Based significantly on integer_test.cc (Thanks Kenton!)

#include "evlan/vm/builtin/double.h"
#include <limits>
#include "evlan/vm/runtime.h"
#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class DoubleTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    AddImport("double", Value(GetDoubleBuiltins(), Data()));
  }
};

TEST_F(DoubleTest, HelperFunctions) {
  Value value = MakeDouble(123);
  ASSERT_TRUE(IsDouble(value));
  EXPECT_EQ(123, GetDoubleValue(value));
}

TEST_F(DoubleTest, DebugString) {
  EXPECT_EQ("123.5", MakeDouble(123.5).DebugString());
  EXPECT_EQ("123.0", MakeDouble(123).DebugString());
  EXPECT_EQ("1.235e+102", MakeDouble(1.235e+102).DebugString());
}

TEST_F(DoubleTest, BasicTest) {
  ExpectEvaluatesToInteger(1, "1");
  ExpectEvaluatesToDouble(1.0, "1.0");
  ExpectEvaluatesToDouble(1.3, "1.3");
  ExpectEvaluatesToDouble(1.3e+10, "1.3e+10");
  ExpectEvaluatesToDouble(1.3e-10, "1.3e-10");
}

TEST_F(DoubleTest, Arithmetic) {
  ExpectEvaluatesToDouble( 5.5 + 9.5, "import \"double\".add     ( 5.5, 9.5)");
  ExpectEvaluatesToDouble(15.5 - 6.5, "import \"double\".subtract(15.5, 6.5)");
  ExpectEvaluatesToDouble( 4.5 * 7.5, "import \"double\".multiply( 4.5, 7.5)");
  ExpectEvaluatesToDouble(12.5 / 3.5, "import \"double\".divide  (12.5, 3.5)");

  ExpectEvaluatesToDouble(-15.5, "import \"double\".negate(15.5)");

  ExpectEvaluatesToDouble(numeric_limits<double>::infinity(),
                           "import \"double\".divide(1, 0)");
}

TEST_F(DoubleTest, Comparison) {
  ExpectEvaluatesToBoolean(false, "import \"double\".equal(1.5, 2.5)");
  ExpectEvaluatesToBoolean(true , "import \"double\".equal(2.5, 2.5)");
  ExpectEvaluatesToBoolean(false, "import \"double\".equal(3.5, 2.5)");

  ExpectEvaluatesToBoolean(true , "import \"double\".notEqual(1.5, 2.5)");
  ExpectEvaluatesToBoolean(false, "import \"double\".notEqual(2.5, 2.5)");
  ExpectEvaluatesToBoolean(true , "import \"double\".notEqual(3.5, 2.5)");

  ExpectEvaluatesToBoolean(true , "import \"double\".less(1.5, 2.5)");
  ExpectEvaluatesToBoolean(false, "import \"double\".less(2.5, 2.5)");
  ExpectEvaluatesToBoolean(false, "import \"double\".less(3.5, 2.5)");

  ExpectEvaluatesToBoolean(false, "import \"double\".greater(1.5, 2.5)");
  ExpectEvaluatesToBoolean(false, "import \"double\".greater(2.5, 2.5)");
  ExpectEvaluatesToBoolean(true , "import \"double\".greater(3.5, 2.5)");

  ExpectEvaluatesToBoolean(true , "import \"double\".notGreater(1.5, 2.5)");
  ExpectEvaluatesToBoolean(true , "import \"double\".notGreater(2.5, 2.5)");
  ExpectEvaluatesToBoolean(false, "import \"double\".notGreater(3.5, 2.5)");

  ExpectEvaluatesToBoolean(false, "import \"double\".notLess(1.5, 2.5)");
  ExpectEvaluatesToBoolean(true , "import \"double\".notLess(2.5, 2.5)");
  ExpectEvaluatesToBoolean(true , "import \"double\".notLess(3.5, 2.5)");
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
