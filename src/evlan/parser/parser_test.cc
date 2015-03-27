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
//
// This test covers both parser.cc and expression.cc.
//
// TODO(kenton):  If we care to test this "properly", we could separate
//   parser.cc from expression.cc by creating some sort of ExpressionFactory
//   interface which we could then mock out.  But do we really care?
//
// TODO(kenton):  We need to test error handling better, but first we should
//   probably implement error handling in a way that doesn't involve
//   GOOGLE_LOG(ERROR).

#include "evlan/parser/parser.h"
#include "evlan/testing/test_util.h"
#include <google/protobuf/text_format.h>
#include <google/protobuf/testing/googletest.h>
#include "evlan/strutil.h"

namespace evlan {
namespace parser {
namespace {

using google::protobuf::ScopedMemoryLog;
using google::protobuf::ERROR;

class ParserTest : public EvlanTest {
 protected:
  void ExpectCodeTree(const string& code,
                      const int code_tree[], int code_tree_size) {
    Module module;

    {
      ScopedMemoryLog log;
      Parse(code, &module);
      EXPECT_EQ(0, log.GetMessages(ERROR).size());
    }

    EXPECT_EQ(
      IntArrayToString(code_tree, code_tree_size),
      IntArrayToString(module.code_tree().data(), module.code_tree_size()));
  }

  void ExpectConstants(const string& code, const string& expected_constants) {
    Module module;

    {
      ScopedMemoryLog log;
      Parse(code, &module);
      EXPECT_EQ(0, log.GetMessages(ERROR).size());
    }

    module.clear_code_tree();

    Module expected_module;
    if (google::protobuf::TextFormat::ParseFromString(expected_constants,
                                                      &expected_module)) {
      EXPECT_EQ(expected_module.DebugString(), module.DebugString());
    } else {
      FAIL() << "Couldn't parse expected constants: " << expected_constants;
    }
  }

  string IntArrayToString(const int array[], int size) {
    string result;

    for (int i = 0; i < size; i++) {
      if (i > 0) result.append(", ");
      result.append(SimpleItoa(array[i]));
    }

    return result;
  }

  static const int abs = Module::ABSTRACTION;
  static const int app = Module::APPLICATION;
  inline int var(int i) { return Module::FIRST_VARIABLE + i; }
};

#define EXPECT_CODE_TREE(CODE, ARRAY...)                                     \
  {                                                                          \
    const int expected_code_tree[] = {ARRAY};                                \
    ExpectCodeTree(CODE, expected_code_tree, arraysize(expected_code_tree)); \
  }

// Direct tests that verify basic code trees.
TEST_F(ParserTest, CodeTrees) {
  // Constants.
  EXPECT_CODE_TREE("123", var(0));
  EXPECT_CODE_TREE("@foo", var(0));
  EXPECT_CODE_TREE("\"foo\"", var(0));
  EXPECT_CODE_TREE("import \"foo\"", var(0));

  // Applications.
  EXPECT_CODE_TREE("123(@foo)", app, var(0), var(1));
  EXPECT_CODE_TREE("import \"foo\"(123)(@foo)(\"foo\")",
                   app, app, app, var(0), var(1), var(2), var(3));
  EXPECT_CODE_TREE("123 of\n"
                   "  @foo",
                   app, var(0), var(1));

  // The order of constants in the constant table must always be
  // imports < ints < atoms < data.
  EXPECT_CODE_TREE("\"foo\"(@foo)(123)(import \"foo\")",
                   app, app, app, var(3), var(2), var(1), var(0));

  // Each individual constant type is in sorted order.
  EXPECT_CODE_TREE("@a(@d)(@b)(@c)",
                   app, app, app, var(0), var(3), var(1), var(2));

  // Abstractions.
  EXPECT_CODE_TREE("x => x", abs, var(0));
  EXPECT_CODE_TREE("x => y => x", abs, abs, var(1));
  EXPECT_CODE_TREE("x => y => y", abs, abs, var(0));

  // Abstractions and applications together.
  EXPECT_CODE_TREE("x => y => x(y)", abs, abs, app, var(1), var(0));
  EXPECT_CODE_TREE("x => x(1)", abs, app, var(0), var(1));
}

TEST_F(ParserTest, ConstantTable) {
  ExpectConstants(
    "import \"foo\"(123)(@bar)(\"baz\")",
    "imports: \"foo\" "
    "int_constants: 123 "
    "atom_constants: \"bar\" "
    "data_constants: \"baz\" ");

  // Each type in the constant table is in sorted order.
  ExpectConstants(
    "@a(@d)(@b)(@c)",
    "atom_constants: \"a\" "
    "atom_constants: \"b\" "
    "atom_constants: \"c\" "
    "atom_constants: \"d\" ");
}

// -------------------------------------------------------------------
// Some functionality is too complicated to be tested by simply verifying
// the code tree.  We need to actually execute it.

TEST_F(ParserTest, MultiArgFunction) {
  ExpectEvaluatesToInteger(123, "((x, y) => x)(123, 456)");
  ExpectEvaluatesToInteger(456, "((x, y) => y)(123, 456)");

  ExpectEvaluatesToInteger(123,
    "((x, y) => x) of\n"
    "  123\n"
    "  456\n");
  ExpectEvaluatesToInteger(456,
    "((x, y) => y) of\n"
    "  123\n"
    "  456\n");
}

TEST_F(ParserTest, WhereBlock) {
  ExpectEvaluatesToInteger(123,
    "x where\n"
    "  x = 123\n"
    "  y = 456\n");

  ExpectEvaluatesToInteger(456,
    "y where\n"
    "  x = 123\n"
    "  y = 456\n");

  ExpectEvaluatesToInteger(123,
    "y where\n"
    "  x = 123\n"
    "  y = x\n");

  ExpectEvaluatesToInteger(456,
    "x where\n"
    "  x = y\n"
    "  y = 456\n");

  ExpectEvaluatesToInteger(123,
    "a(c) where\n"
    "  c = d => d(e => e(f => 123))\n"
    "  a = b => b(a)\n");

  // "f(x) = ..." syntax.
  ExpectEvaluatesToInteger(123,
    "f(x => 123) where\n"
    "  f(g) = g(456)\n");

  // It's actually hard to test more advanced stuff here without loading up
  // the support code or making very verbose use of builtin functions.  We'll
  // write better tests in pure Evlan elsewhere.
}

TEST_F(ParserTest, Object) {
  ExpectEvaluatesToInteger(123,
    "o.x where\n"
    "  o = object of\n"
    "    x = 123\n"
    "    y = 456\n");

  ExpectEvaluatesToInteger(456,
    "o.y where\n"
    "  o = object of\n"
    "    x = 123\n"
    "    y = 456\n");

  ExpectEvaluatesToInteger(123,
    "o.y where\n"
    "  o = object of\n"
    "    x = 123\n"
    "    y = x\n");

  ExpectEvaluatesToInteger(456,
    "o.x where\n"
    "  o = object of\n"
    "    x = y\n"
    "    y = 456\n");

  // "f(x) = ..." syntax.
  ExpectEvaluatesToInteger(123,
    "o.f(x => 123) where\n"
    "  o = object of\n"
    "    f(g) = g(456)\n");

  // Inline objects.
  ExpectEvaluatesToInteger(123, "{x = 123, y = 456}.x");
  ExpectEvaluatesToInteger(456, "{x = 123, y = 456}.y");
}

TEST_F(ParserTest, Boolean) {
  ExpectEvaluatesToBoolean(true, "true");
  ExpectEvaluatesToBoolean(false, "false");
}

TEST_F(ParserTest, Array) {
  ExpectEvaluatesToDebugString("{12, 34, 56, 78}", "{12, 34, 56, 78}");

  ExpectEvaluatesToDebugString(
    "{12, 34, 56, 78}",
    "array of\n"
    "  12\n"
    "  34\n"
    "  56\n"
    "  78\n");
}

// -------------------------------------------------------------------

class AdvancedParserTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    // We need to load support code for these tests.  The support code is
    // complicated, so it bogs down the debug memory manager.  Use a fast
    // memory manager instead.
    UseFastMemoryManager();
    LoadStandardSupportCode();
  }
};

TEST_F(AdvancedParserTest, ArraySubscript) {
  ExpectEvaluatesToInteger(123, "{123, 456, 789}[0]");
  ExpectEvaluatesToInteger(456, "{123, 456, 789}[1]");
  ExpectEvaluatesToInteger(789, "{123, 456, 789}[2]");
}

TEST_F(AdvancedParserTest, InfixOperators) {
  ExpectEvaluatesToInteger(12 + 34, "12 + 34");
  ExpectEvaluatesToInteger(12 - 34, "12 - 34");
  ExpectEvaluatesToInteger(12 * 34, "12 * 34");
  ExpectEvaluatesToInteger(12 / 34, "12 / 34");

  ExpectEvaluatesToBoolean(false, "2 == 1");
  ExpectEvaluatesToBoolean(true , "2 == 2");
  ExpectEvaluatesToBoolean(false, "2 == 3");

  ExpectEvaluatesToBoolean(true , "2 != 1");
  ExpectEvaluatesToBoolean(false, "2 != 2");
  ExpectEvaluatesToBoolean(true , "2 != 3");

  ExpectEvaluatesToBoolean(false, "2 <  1");
  ExpectEvaluatesToBoolean(false, "2 <  2");
  ExpectEvaluatesToBoolean(true , "2 <  3");

  ExpectEvaluatesToBoolean(true , "2 >  1");
  ExpectEvaluatesToBoolean(false, "2 >  2");
  ExpectEvaluatesToBoolean(false, "2 >  3");

  ExpectEvaluatesToBoolean(false, "2 <= 1");
  ExpectEvaluatesToBoolean(true , "2 <= 2");
  ExpectEvaluatesToBoolean(true , "2 <= 3");

  ExpectEvaluatesToBoolean(true , "2 >= 1");
  ExpectEvaluatesToBoolean(true , "2 >= 2");
  ExpectEvaluatesToBoolean(false, "2 >= 3");

  ExpectEvaluatesToBoolean(false, "false and false");
  ExpectEvaluatesToBoolean(false, "false and true ");
  ExpectEvaluatesToBoolean(false, "true  and false");
  ExpectEvaluatesToBoolean(true , "true  and true ");

  ExpectEvaluatesToBoolean(false, "false or false");
  ExpectEvaluatesToBoolean(true , "false or true ");
  ExpectEvaluatesToBoolean(true , "true  or false");
  ExpectEvaluatesToBoolean(true , "true  or true ");
}

TEST_F(AdvancedParserTest, PrefixOperators) {
  ExpectEvaluatesToInteger(-123, "-123");
  ExpectEvaluatesToBoolean(false, "not true");
  ExpectEvaluatesToBoolean(true , "not false");
}

TEST_F(AdvancedParserTest, Conditional) {
  ExpectEvaluatesToInteger(123, "if true  then 123 else 456");
  ExpectEvaluatesToInteger(456, "if false then 123 else 456");
}

}  // namespace
}  // namespace parser
}  // namespace evlan
