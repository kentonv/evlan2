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

#include "evlan/vm/builtin/type_error.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/runtime.h"
#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class TypeErrorTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    Value error =
      TypeErrorUtils::MakeError(GetMemoryManager(), GetMemoryRoot(), "test");
    AddImport("error", error);
  }
};

TEST_F(TypeErrorTest, HelperFunctions) {
  Value error =
    TypeErrorUtils::MakeError(GetMemoryManager(), GetMemoryRoot(), "foo");

  EXPECT_TRUE(TypeErrorUtils::IsError(error));
  EXPECT_EQ(StringPiece("foo"), TypeErrorUtils::GetDescription(error));
  EXPECT_EQ(TypeErrorUtils::GetLogic(), error.GetLogic());
}

TEST_F(TypeErrorTest, DebugString) {
  ExpectEvaluatesToDebugString("typeError(\"test\")", "import \"error\"");
}

// A type error, when applied as a function to anything, should return itself.
TEST_F(TypeErrorTest, ApplyReturnsSelf) {
  ExpectEvaluatesToError("test", "import \"error\"(123)");
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
