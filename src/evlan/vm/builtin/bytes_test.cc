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

#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/runtime.h"
#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class BytesTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    AddImport("bytes", Value(ByteUtils::GetBuiltinModule(), Data()));

    // Annoyingly, we cannot evaluate the code "-1" without loading the support
    // code for integers, because it translates to 1.negate().  So, we make -1
    // available as an import instead.
    AddImport("-1", MakeInteger(-1));
  }
};

TEST_F(BytesTest, AliasString) {
  StringPiece original = "foo";

  Value aliased_string =
    ByteUtils::AliasString(GetMemoryManager(), GetMemoryRoot(), original);

  ASSERT_TRUE(ByteUtils::IsByteArray(aliased_string));

  StringPiece final = ByteUtils::ToString(aliased_string);

  ASSERT_EQ(original, final);
  EXPECT_EQ(original.data(), final.data());
}

TEST_F(BytesTest, CopyString) {
  StringPiece original = "foo";

  Value aliased_string =
    ByteUtils::CopyString(GetMemoryManager(), GetMemoryRoot(), original);

  ASSERT_TRUE(ByteUtils::IsByteArray(aliased_string));

  StringPiece final = ByteUtils::ToString(aliased_string);

  ASSERT_EQ(original, final);
  EXPECT_NE(original.data(), final.data());
}

TEST_F(BytesTest, Size) {
  ExpectEvaluatesToInteger(3, "import \"bytes\".size(\"foo\")");
}

TEST_F(BytesTest, Element) {
  ExpectEvaluatesToInteger('o', "import \"bytes\".element(\"foo\", 1)");

  ExpectEvaluatesToError(
    "Array index out of bounds.",
    "import \"bytes\".element(\"foo\", 3)");

  // See SetUp() for why we import -1 instead of just writing it normally.
  ExpectEvaluatesToError(
    "Array index out of bounds.",
    "import \"bytes\".element(\"foo\", import \"-1\")");
}

TEST_F(BytesTest, Interval) {
  ExpectEvaluatesToString("ooba",
    "import \"bytes\".interval(\"foobar\", 1, 5)");

  ExpectEvaluatesToString("foobar",
    "import \"bytes\".interval(\"foobar\", 0, 6)");

  // See SetUp() for why we import -1 instead of just writing it normally.
  ExpectEvaluatesToError(
    "Invalid byte array interval [-1, 3) for array of size 6.",
    "import \"bytes\".interval(\"foobar\", import \"-1\", 3)");

  ExpectEvaluatesToError(
    "Invalid byte array interval [3, 7) for array of size 6.",
    "import \"bytes\".interval(\"foobar\", 3, 7)");
}

TEST_F(BytesTest, Concatenate) {
  ExpectEvaluatesToString("foobar",
    "import \"bytes\".concatenate(\"foo\", \"bar\")");
}

TEST_F(BytesTest, Comparison) {
  ExpectEvaluatesToBoolean(true , "import \"bytes\".equal(\"foo\", \"foo\")");
  ExpectEvaluatesToBoolean(false, "import \"bytes\".equal(\"foo\", \"bar\")");

  ExpectEvaluatesToBoolean(false, "import \"bytes\".notEqual(\"foo\",\"foo\")");
  ExpectEvaluatesToBoolean(true , "import \"bytes\".notEqual(\"foo\",\"bar\")");

  ExpectEvaluatesToBoolean(false, "import \"bytes\".less(\"foo\", \"foo\")");
  ExpectEvaluatesToBoolean(false, "import \"bytes\".less(\"foo\", \"bar\")");
  ExpectEvaluatesToBoolean(true , "import \"bytes\".less(\"bar\", \"foo\")");

  ExpectEvaluatesToBoolean(false, "import \"bytes\".greater(\"foo\", \"foo\")");
  ExpectEvaluatesToBoolean(true , "import \"bytes\".greater(\"foo\", \"bar\")");
  ExpectEvaluatesToBoolean(false, "import \"bytes\".greater(\"bar\", \"foo\")");

  ExpectEvaluatesToBoolean(true , "import \"bytes\".notLess(\"foo\", \"foo\")");
  ExpectEvaluatesToBoolean(true , "import \"bytes\".notLess(\"foo\", \"bar\")");
  ExpectEvaluatesToBoolean(false, "import \"bytes\".notLess(\"bar\", \"foo\")");

  ExpectEvaluatesToBoolean(true ,"import\"bytes\".notGreater(\"foo\",\"foo\")");
  ExpectEvaluatesToBoolean(false,"import\"bytes\".notGreater(\"foo\",\"bar\")");
  ExpectEvaluatesToBoolean(true ,"import\"bytes\".notGreater(\"bar\",\"foo\")");
}

TEST_F(BytesTest, DebugString) {
  ExpectEvaluatesToDebugString("\"foo\"", "\"foo\"");
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
