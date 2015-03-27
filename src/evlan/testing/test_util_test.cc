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

#include "evlan/testing/test_util.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/builtin.h"
#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

namespace evlan {
namespace {

class TestUtilTest : public EvlanTest {};

// -------------------------------------------------------------------

TEST_F(TestUtilTest, ExpectEvaluatesToBoolean) {
  ExpectEvaluatesToBoolean(true, "true");

  ExpectEvaluatesToBoolean(false, "false");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToBoolean(true, "false"),
    "Final result didn't match.\n"
    "  Actual: false\n"
    "Expected: true");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToBoolean(false, "true"),
    "Final result didn't match.\n"
    "  Actual: true\n"
    "Expected: false");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToBoolean(true, "@abc"),
    "Final result didn't match.\n"
    "  Actual: @abc\n"
    "Expected: true");
}

TEST_F(TestUtilTest, ExpectEvaluatesToInteger) {
  ExpectEvaluatesToInteger(123, "123");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToInteger(123, "456"),
    "Final result didn't match.\n"
    "  Actual: 456\n"
    "Expected: 123");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToInteger(123, "@foo"),
    "Final result didn't match.\n"
    "  Actual: @foo\n"
    "Expected: 123");
}

TEST_F(TestUtilTest, ExpectEvaluatesToAtom) {
  ExpectEvaluatesToAtom("foo", "@foo");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToAtom("foo", "@bar"),
    "Final result didn't match.\n"
    "  Actual: @bar\n"
    "Expected: @foo");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToAtom("foo", "123"),
    "Final result didn't match.\n"
    "  Actual: 123\n"
    "Expected: @foo");
}

TEST_F(TestUtilTest, ExpectEvaluatesToString) {
  ExpectEvaluatesToString("foo", "\"foo\"");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToString("foo", "\"bar\""),
    "Final result didn't match.\n"
    "  Actual: \"bar\"\n"
    "Expected: \"foo\"");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToString("foo", "123"),
    "Final result didn't match.\n"
    "  Actual: 123\n"
    "Expected: \"foo\"");
}

TEST_F(TestUtilTest, ExpectEvaluatesToError) {
  ExpectEvaluatesToError("Called placeholder logic.", "@a(1)");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToError("Some other error.", "@a(1)"),
    "Error description didn't match.\n"
    "  Actual: Called placeholder logic.\n"
    "Expected: Some other error.");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToError("Some error.", "123"),
    "Final result didn't match.\n"
    "  Actual: 123\n"
    "Expected: typeError(\"Some error.\")");
}

TEST_F(TestUtilTest, ExpectEvaluatesToDebugString) {
  ExpectEvaluatesToDebugString("{@foo, 123}", "{@foo, 123}");
  ExpectEvaluatesToDebugString(1, "{@foo, 123}", "{@foo, 123}");
  ExpectEvaluatesToDebugString(0, "{..., ...}", "{@foo, 123}");

  EXPECT_NONFATAL_FAILURE(
    ExpectEvaluatesToDebugString(1, "123", "{@foo, 123}"),
    "Debug string didn't match.\n"
    "  Actual: {@foo, 123}\n"
    "Expected: 123");
}

// -------------------------------------------------------------------

TEST_F(TestUtilTest, AddImport) {
  AddImport("foo", vm::builtin::MakeInteger(123));
  ExpectEvaluatesToInteger(123, "import \"foo\"");
}

// Add an import which points at managed memory.  This verifies that said
// memory stays valid until the code is executed.
TEST_F(TestUtilTest, AddTrackedImport) {
  AddImport("foo",
    vm::builtin::ByteUtils::CopyString(
      GetMemoryManager(), GetMemoryRoot(), "bar"));

  // Allocate same random bytes to force a sweep (assuming we're using
  // DebugMemoryManager).
  GetMemoryManager()->AllocateBytes(16, GetMemoryRoot());

  ExpectEvaluatesToString("bar", "import \"foo\"");
}

// -------------------------------------------------------------------

TEST_F(TestUtilTest, StandardSupportCode) {
  // The support code is complicated enough to bog down the debug memory
  // manager.
  UseFastMemoryManager();

  // This is tested more elsewhere, but just make sure everything seems to
  // operate.
  LoadStandardSupportCode();

  ExpectEvaluatesToInteger(12 + 34, "12 + 34");
}

}  // namespace
}  // namespace evlan
