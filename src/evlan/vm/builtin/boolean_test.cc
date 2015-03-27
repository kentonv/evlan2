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

#include "evlan/vm/builtin/boolean.h"
#include "evlan/vm/runtime.h"
#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class BooleanTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    AddImport("boolean", Value(BooleanUtils::GetBuiltinModule(), Data()));
  }
};

TEST_F(BooleanTest, HelperFunctions) {
  Value true_value = BooleanUtils::MakeBoolean(true);
  Value false_value = BooleanUtils::MakeBoolean(false);
  ASSERT_TRUE(BooleanUtils::IsBoolean(true_value));
  ASSERT_TRUE(BooleanUtils::IsBoolean(false_value));
  ASSERT_TRUE(BooleanUtils::GetBooleanValue(true_value));
  ASSERT_FALSE(BooleanUtils::GetBooleanValue(false_value));
}

TEST_F(BooleanTest, DebugString) {
  EXPECT_EQ("false", BooleanUtils::MakeBoolean(false).DebugString());
  EXPECT_EQ("true" , BooleanUtils::MakeBoolean(true ).DebugString());
}

TEST_F(BooleanTest, Constants) {
  ExpectEvaluatesToBoolean(false, "import \"boolean\".$false");
  ExpectEvaluatesToBoolean(true , "import \"boolean\".$true");
}

TEST_F(BooleanTest, LogicOps) {
  ExpectEvaluatesToBoolean(false, "import \"boolean\".$and(false, false)");
  ExpectEvaluatesToBoolean(false, "import \"boolean\".$and(false, true )");
  ExpectEvaluatesToBoolean(false, "import \"boolean\".$and(true , false)");
  ExpectEvaluatesToBoolean(true , "import \"boolean\".$and(true , true )");

  ExpectEvaluatesToBoolean(false, "import \"boolean\".$or(false, false)");
  ExpectEvaluatesToBoolean(true , "import \"boolean\".$or(false, true )");
  ExpectEvaluatesToBoolean(true , "import \"boolean\".$or(true , false)");
  ExpectEvaluatesToBoolean(true , "import \"boolean\".$or(true , true )");

  ExpectEvaluatesToBoolean(false, "import \"boolean\".$xor(false, false)");
  ExpectEvaluatesToBoolean(true , "import \"boolean\".$xor(false, true )");
  ExpectEvaluatesToBoolean(true , "import \"boolean\".$xor(true , false)");
  ExpectEvaluatesToBoolean(false, "import \"boolean\".$xor(true , true )");

  ExpectEvaluatesToBoolean(true , "import \"boolean\".equals(false, false)");
  ExpectEvaluatesToBoolean(false, "import \"boolean\".equals(false, true )");
  ExpectEvaluatesToBoolean(false, "import \"boolean\".equals(true , false)");
  ExpectEvaluatesToBoolean(true , "import \"boolean\".equals(true , true )");

  ExpectEvaluatesToBoolean(true , "import \"boolean\".$not(false)");
  ExpectEvaluatesToBoolean(false, "import \"boolean\".$not(true )");
}

TEST_F(BooleanTest, Conditional) {
  ExpectEvaluatesToInteger(123, "import \"boolean\".$if(true , 123, 456)");
  ExpectEvaluatesToInteger(456, "import \"boolean\".$if(false, 123, 456)");
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
