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

// Copyright 2008 Google Inc. All Rights Reserved.
// Author: Dan Hartmann

#include "evlan/public/interactive_interpreter.h"

#include <gtest/gtest.h>

namespace evlan {

class InteractiveInterpreterTest : public testing::Test {
 protected:
  InteractiveInterpreter interpreter_;
};

TEST_F(InteractiveInterpreterTest, WrapsEvaluator) {
  EXPECT_EQ("3", interpreter_.Evaluate("3"));
  EXPECT_EQ("5", interpreter_.Evaluate("1 + 4"));
  EXPECT_EQ("@foo", interpreter_.Evaluate("@foo"));
  EXPECT_EQ("13", interpreter_.Evaluate(
                      "f(7) where "
                      "  f = x => if x < 2 then x else f(x-1) + f(x-2)"));
}

TEST_F(InteractiveInterpreterTest, HasPersistence) {
  EXPECT_EQ("3", interpreter_.Evaluate("x = 3"));
  EXPECT_EQ("3", interpreter_.Evaluate("x"));
  interpreter_.Evaluate("f(x) = if x < 2 then x else f(x-1) + f(x-2)");
  EXPECT_EQ("13", interpreter_.Evaluate("f(7)"));
  EXPECT_EQ("@foo", interpreter_.Evaluate("y = @foo"));
  EXPECT_EQ("true", interpreter_.Evaluate("y == @foo"));
}

}  // namespace
