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

#include "evlan/vm/builtin/native.h"
#include "evlan/proto/module.pb.h"
#include "evlan/testing/test_util.h"
#include "evlan/strutil.h"
#include <google/protobuf/descriptor.h>
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class NativeTest : public EvlanTest {
 protected:
  NativeTest(): native_module_("testModule") {}

  NativeModule native_module_;

  virtual void SetUp() {
    AddImport("test", Value(&native_module_, Data()));
  }
};

namespace {

// Test functions with inputs and outputs of each type.

int Square(int i) {
  return i * i;
}

double Divide(double x, double y) {
  return x / y;
}

bool Complement(bool b) {
  return !b;
}

string ToLower(const string& input) {
  string result(input);
  LowerString(&result);
  return result;
}

StringPiece ChooseStringPiece(StringPiece input) {
  if (input == "foo") return "FOO";
  if (input == "bar") return "BAR";
  return "other";
}

Value DebugStringSize(Value input) {
  return MakeInteger(input.DebugString().size());
}

StringPiece MessageTypeName(const google::protobuf::Message* message) {
  return message->GetDescriptor()->full_name();
}

}  // namespace

TEST_F(NativeTest, BasicTypes) {
  native_module_.AddFunction("divide", &Divide);
  native_module_.AddFunction("square", &Square);
  native_module_.AddFunction("complement", &Complement);
  native_module_.AddFunction("lower", &ToLower);
  native_module_.AddFunction("choose", &ChooseStringPiece);
  native_module_.AddFunction("debugStringSize", &DebugStringSize);
  native_module_.AddFunction("messageTypeName", &MessageTypeName);

  ExpectEvaluatesToDouble(1.5, "import \"test\".divide(3, 2)");
  ExpectEvaluatesToDouble(7, "import \"test\".divide(3.5, 0.5)");

  ExpectEvaluatesToInteger(25, "import \"test\".square(5)");
  ExpectEvaluatesToError(
    "Wrong type for parameter 0 to native function \"square\".  "
      "Expected Integer, got \"@foo\".",
    "import \"test\".square(@foo)");

  ExpectEvaluatesToBoolean(false, "import \"test\".complement(true)");
  ExpectEvaluatesToError(
    "Wrong type for parameter 0 to native function \"complement\".  "
      "Expected Boolean, got \"@foo\".",
    "import \"test\".complement(@foo)");

  ExpectEvaluatesToString("foo", "import \"test\".lower(\"FOO\")");
  ExpectEvaluatesToError(
    "Wrong type for parameter 0 to native function \"lower\".  "
      "Expected String, got \"@foo\".",
    "import \"test\".lower(@foo)");

  ExpectEvaluatesToString("FOO", "import \"test\".choose(\"foo\")");
  ExpectEvaluatesToError(
    "Wrong type for parameter 0 to native function \"choose\".  "
      "Expected String, got \"@foo\".",
    "import \"test\".choose(@foo)");

  ExpectEvaluatesToInteger(4, "import \"test\".debugStringSize(@foo)");

  AddImport("message",
            ProtobufUtils::WrapMessage(GetMemoryManager(),
                                       &Module::default_instance()));
  ExpectEvaluatesToString(Module::descriptor()->full_name(),
    "import \"test\".messageTypeName(import \"message\")");
}

// ===================================================================

namespace {

// Test functions with varying numbers of parameters.

int TwoParams(int i, int j) { return i + j; }
int ThreeParams(int i, int j, int k) { return i + j + k; }

void OneParamWithContext(Context* context, int i) {
  context->Return(MakeInteger(i * i));
}

void TwoParamsWithContext(Context* context, int i, int j) {
  context->Return(MakeInteger(i * j));
}

void ThreeParamsWithContext(Context* context, int i, int j, int k) {
  context->Return(MakeInteger(i * j * k));
}

}  // namespace

TEST_F(NativeTest, TwoParamFunction) {
  native_module_.AddFunction("f2", &TwoParams);
  native_module_.AddFunction("f3", &ThreeParams);
  native_module_.AddFunction("c1", &OneParamWithContext);
  native_module_.AddFunction("c2", &TwoParamsWithContext);
  native_module_.AddFunction("c3", &ThreeParamsWithContext);

  ExpectEvaluatesToInteger(12 + 34, "import \"test\".f2(12, 34)");
  ExpectEvaluatesToInteger(12 + 34 + 56, "import \"test\".f3(12, 34, 56)");
  ExpectEvaluatesToInteger(25, "import \"test\".c1(5)");
  ExpectEvaluatesToInteger(12 * 34, "import \"test\".c2(12, 34)");
  ExpectEvaluatesToInteger(12 * 34 * 56, "import \"test\".c3(12, 34, 56)");

  ExpectEvaluatesToError("Wrong number of arguments to native function \"f2\".",
                         "import \"test\".f2(12)");
  ExpectEvaluatesToError("Wrong number of arguments to native function \"f2\".",
                         "import \"test\".f2(12, 34, 56)");
  ExpectEvaluatesToError("Wrong number of arguments to native function \"f3\".",
                         "import \"test\".f3(12)");
  ExpectEvaluatesToError("Wrong number of arguments to native function \"f3\".",
                         "import \"test\".f3(12, 34)");
  ExpectEvaluatesToError("Wrong number of arguments to native function \"c2\".",
                         "import \"test\".c2(12)");
  ExpectEvaluatesToError("Wrong number of arguments to native function \"c2\".",
                         "import \"test\".c2(12, 34, 56)");
  ExpectEvaluatesToError("Wrong number of arguments to native function \"c3\".",
                         "import \"test\".c3(12)");
  ExpectEvaluatesToError("Wrong number of arguments to native function \"c3\".",
                         "import \"test\".c3(12, 34)");
}

// ===================================================================

TEST_F(NativeTest, ErrorsPropagate) {
  native_module_.AddFunction("square", &Square);
  AddImport("error",
    TypeErrorUtils::MakeError(GetMemoryManager(), GetMemoryRoot(), "test"));
  ExpectEvaluatesToError("test", "import \"test\".square(import \"error\")");
}

TEST_F(NativeTest, FirstErrorTakesPrecedence) {
  native_module_.AddFunction("f", &TwoParams);
  AddImport("error1",
    TypeErrorUtils::MakeError(GetMemoryManager(), GetMemoryRoot(), "first"));
  AddImport("error2",
    TypeErrorUtils::MakeError(GetMemoryManager(), GetMemoryRoot(), "second"));
  ExpectEvaluatesToError("first",
    "import \"test\".f(import \"error1\", import \"error2\")");
}

TEST_F(NativeTest, DebugString) {
  EXPECT_EQ("nativeModule(\"testModule\")",
            Value(&native_module_, Data()).DebugString());

  native_module_.AddFunction("square", &Square);
  ExpectEvaluatesToDebugString(
    "nativeFunction(square(Integer): Integer)",
    "import \"test\".square");
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
