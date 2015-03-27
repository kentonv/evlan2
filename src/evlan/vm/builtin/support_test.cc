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

#include "evlan/vm/builtin/support.h"
#include "evlan/vm/runtime.h"
#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class SupportCodeTest : public EvlanTest {};

TEST_F(SupportCodeTest, NotInitialized) {
  ExpectEvaluatesToError("Called placeholder logic.", "123(456)");
}

TEST_F(SupportCodeTest, BasicTypes) {
  SetSupportCode("type => value => param => {type, value, param}");

  ExpectEvaluatesToDebugString("{@integer, 123, 456}", "123(456)");
  ExpectEvaluatesToDebugString("{@boolean, true, 123}", "true(123)");
  ExpectEvaluatesToDebugString("{@atom, @foo, 123}", "@foo(123)");
  ExpectEvaluatesToDebugString("{@string, \"foo\", 123}", "\"foo\"(123)");
  ExpectEvaluatesToDebugString("{@array, {1, 2}, 123}", "{1, 2}(123)");
}

// Protocol buffers are tested in protobuf_test.cc.

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
