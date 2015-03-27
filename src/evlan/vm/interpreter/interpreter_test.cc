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

#include "evlan/vm/interpreter/interpreter.h"
#include <functional>
#include "evlan/vm/runtime.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace interpreter {
namespace {

using namespace std::placeholders;

class InterpreterTest : public EvlanTest {};

// Frankly, there isn't a whole lot of testing to do here.  Although there is
// a lot of code in interpreter.cc, it only actually implements a small number
// of concepts, and the complication comes from the need to fit into the
// Evlan execution and memory management models.

TEST_F(InterpreterTest, Basics) {
  // Constants.
  ExpectEvaluatesToInteger(123, "123");
  ExpectEvaluatesToAtom("foo", "@foo");
  ExpectEvaluatesToString("foo", "\"foo\"");

  ExpectEvaluatesToDebugString("{123, @foo, \"foo\"}", "{123, @foo, \"foo\"}");

  // Abstractions and variables.
  ExpectEvaluatesToDebugString(
    "function(variable(0))",
    "x => x");
  ExpectEvaluatesToDebugString(
    "function(abstraction(variable(1)))",
    "y => x => y");

  // Applications.
  ExpectEvaluatesToDebugString(
    "function(application(variable(0), constant(1)))",
    "x => x(1)");
  ExpectEvaluatesToDebugString(
    "function(abstraction("
      "application(application(variable(0), variable(1)), constant(1))))",
    "y => x => x(y)(1)");

  // Complete evaluation.
  ExpectEvaluatesToInteger(123, "(y => x => y)(123)(456)");
  ExpectEvaluatesToInteger(456, "(y => x => x)(123)(456)");
}

TEST_F(InterpreterTest, ConstantExpressions) {
  UseFastMemoryManager();
  LoadStandardSupportCode();

  ExpectEvaluatesToDebugString(
      "function(constant(46))",
      "x => 12 + 34");
  ExpectEvaluatesToDebugString(
      "function(application(variable(0), constant(46)))",
      "x => x(12 + 34)");

  // TODO(kenton):  As a further optimization we should detect when all the
  //   variable references within a function point at the function's own
  //   parameter, and then treat that function as a constant.  This would
  //   cause the following tests to pass.
//  ExpectEvaluatesToDebugString(
//    "function(constant(46))",
//    "y => (x => x + 12)(34)");
//  ExpectEvaluatesToDebugString(
//    "function(application(constant(46), variable(0))",
//    "y => (x => x + 12)(34)(y)");
}

TEST_F(InterpreterTest, Import) {
  AddImport("foo", builtin::MakeInteger(123));
  AddImport("bar", builtin::MakeInteger(456));

  ExpectEvaluatesToInteger(123, "import \"foo\"");
  ExpectEvaluatesToInteger(456, "import \"bar\"");
}

static void SubFunctionTestBody(
    Value parent, int offset, Context* context, Value callback) {
  Value sub_environment = builtin::MakeInteger(123);
  GetSubFunction(context, parent, offset, &sub_environment, 1, false, callback);
}

TEST_F(InterpreterTest, SubFunction) {
  Module module;

  // x => x(y => x)
  module.add_code_tree(Module::ABSTRACTION);         // x =>
  module.add_code_tree(Module::APPLICATION);
  module.add_code_tree(Module::FIRST_VARIABLE);      // x
  module.add_code_tree(Module::ABSTRACTION);         // y =>
  module.add_code_tree(Module::FIRST_VARIABLE + 1);  // x

  Value compiled = ExecuteModule(module);

  // Extract y => x sub-function, setting x to 123.
  Value sub_function = RunNative(
      std::bind(&SubFunctionTestBody, compiled, 3, _1, _2));

  // Load and run it.
  AddImport("foo", sub_function);
  ExpectEvaluatesToInteger(123, "import \"foo\"(456)");
}

TEST_F(InterpreterTest, SubFunctionConstantTable) {
  Module module;

  // x => x(y => 789)
  module.add_code_tree(Module::ABSTRACTION);         // x =>
  module.add_code_tree(Module::APPLICATION);
  module.add_code_tree(Module::FIRST_VARIABLE);      // x
  module.add_code_tree(Module::ABSTRACTION);         // y =>
  module.add_code_tree(Module::FIRST_VARIABLE + 2);  // 789
  module.add_int_constants(789);

  Value compiled = ExecuteModule(module);

  // Extract y => 789 sub-function, setting x to 123.
  Value sub_function = RunNative(
      std::bind(&SubFunctionTestBody, compiled, 3, _1, _2));

  // Load and run it.
  AddImport("foo", sub_function);
  ExpectEvaluatesToInteger(789, "import \"foo\"(456)");
}

}  // namespace
}  // namespace interpreter
}  // namespace vm
}  // namespace evlan
