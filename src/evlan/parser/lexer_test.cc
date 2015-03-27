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

#include "evlan/parser/lexer.h"
#include "evlan/common.h"
#include "evlan/parser/lexer.pb.h"
#include <gtest/gtest.h>
#include <google/protobuf/text_format.h>

namespace evlan {
namespace parser {
namespace {

testing::AssertionResult ProtocolMessageEquals(const char* expected_expr,
                                               const char* actual_expr,
                                               const string& expected,
                                               const google::protobuf::Message& actual) {
  scoped_ptr<google::protobuf::Message> expected_message(actual.New());
  if (!google::protobuf::TextFormat::ParseFromString(expected, expected_message.get())) {
    return testing::AssertionFailure(
        testing::Message() << "Could not parse expected message value.");
  } else if (expected_message->DebugString() != actual.DebugString()) {
    // TODO(kenton):  Show diff or something.
    return testing::AssertionFailure(
        testing::Message() << "Messages did not match:\nExpected:\n"
                           << expected_message->DebugString()
                           << "Actual:\n" << actual.DebugString());
  } else {
    return testing::AssertionSuccess();
  }
}

TEST(LexerTest, SimpleTokens) {
  string input =
    "x => x + 1 + 6.7";

  Statement statement;

  Lex(input, &statement);

  EXPECT_PRED_FORMAT2(ProtocolMessageEquals,
    "token: {type: IDENTIFIER text: \"x\"}"
    "token: {type: KEYWORD text: \"=>\"}"
    "token: {type: IDENTIFIER text: \"x\"}"
    "token: {type: KEYWORD text: \"+\"}"
    "token: {type: INTEGER text: \"1\"}"
    "token: {type: KEYWORD text: \"+\"}"
    "token: {type: FLOAT text: \"6.7\"}",
    statement);
}

TEST(LexerTest, FloatFormats) {
  string input =
    "x => 1.0 + 10e+78 - 8e-72";

  Statement statement;

  Lex(input, &statement);

  EXPECT_PRED_FORMAT2(ProtocolMessageEquals,
    "token: {type: IDENTIFIER text: \"x\"}"
    "token: {type: KEYWORD text: \"=>\"}"
    "token: {type: FLOAT text: \"1.0\"}"
    "token: {type: KEYWORD text: \"+\"}"
    "token: {type: FLOAT text: \"10e+78\"}"
    "token: {type: KEYWORD text: \"-\"}"
    "token: {type: FLOAT text: \"8e-72\"}",
    statement);
}

TEST(LexerTest, SingleFloat) {
  string input = "1.0";

  Statement statement;

  Lex(input, &statement);

  EXPECT_PRED_FORMAT2(ProtocolMessageEquals,
    "token: {type: FLOAT text: \"1.0\"}",
    statement);
}

TEST(LexerTest, Strings) {
  string input =
    "\"abcd\" \"ef\\\"gh\" \"ij\\\\\"";

  Statement statement;

  Lex(input, &statement);

  EXPECT_PRED_FORMAT2(ProtocolMessageEquals,
    "token: {type: STRING text: \"\\\"abcd\\\"\"}"
    "token: {type: STRING text: \"\\\"ef\\\\\\\"gh\\\"\"}"
    "token: {type: STRING text: \"\\\"ij\\\\\\\\\\\"\"}",
    statement);
}

TEST(LexerTest, Blocks) {
  string input =
    "x + y where\n"
    "  x = 1\n"
    "  y = 2";

  Statement statement;

  Lex(input, &statement);

  EXPECT_PRED_FORMAT2(ProtocolMessageEquals,
    "token: {type: IDENTIFIER text: \"x\"}"
    "token: {type: KEYWORD text: \"+\"}"
    "token: {type: IDENTIFIER text: \"y\"}"
    "token: {type: KEYWORD text: \"where\"}"
    "token: {type: BLOCK block: {"
      "statement: {"
        "token: {type: IDENTIFIER text: \"x\"}"
        "token: {type: KEYWORD text: \"=\"}"
        "token: {type: INTEGER text: \"1\"}"
      "}"
      "statement: {"
        "token: {type: IDENTIFIER text: \"y\"}"
        "token: {type: KEYWORD text: \"=\"}"
        "token: {type: INTEGER text: \"2\"}"
      "}"
    "}}",
    statement);
}

TEST(LexerTest, MultiLineStatements) {
  string input =
    "x + y\n"
    "where\n"
    "  x = 1 +\n"
    "   2\n"
    "  y = 2";

  Statement statement;

  Lex(input, &statement);

  EXPECT_PRED_FORMAT2(ProtocolMessageEquals,
    "token: {type: IDENTIFIER text: \"x\"}"
    "token: {type: KEYWORD text: \"+\"}"
    "token: {type: IDENTIFIER text: \"y\"}"
    "token: {type: KEYWORD text: \"where\"}"
    "token: {type: BLOCK block: {"
      "statement: {"
        "token: {type: IDENTIFIER text: \"x\"}"
        "token: {type: KEYWORD text: \"=\"}"
        "token: {type: INTEGER text: \"1\"}"
        "token: {type: KEYWORD text: \"+\"}"
        "token: {type: INTEGER text: \"2\"}"
      "}"
      "statement: {"
        "token: {type: IDENTIFIER text: \"y\"}"
        "token: {type: KEYWORD text: \"=\"}"
        "token: {type: INTEGER text: \"2\"}"
      "}"
    "}}",
    statement);
}

TEST(LexerTest, Comments) {
  string input =
    "x + y where  #this shouldn't affect anything\n"
    "#this line shouldn't matter\n"
    "  x = 1\n"
    "#nor should this\n"
    "  y = 2\n"
    "  z = 1 +\n"
    "#nor this\n"
    "   2";

  Statement statement;

  Lex(input, &statement);

  EXPECT_PRED_FORMAT2(ProtocolMessageEquals,
    "token: {type: IDENTIFIER text: \"x\"}"
    "token: {type: KEYWORD text: \"+\"}"
    "token: {type: IDENTIFIER text: \"y\"}"
    "token: {type: KEYWORD text: \"where\"}"
    "token: {type: BLOCK block: {"
      "statement: {"
        "token: {type: IDENTIFIER text: \"x\"}"
        "token: {type: KEYWORD text: \"=\"}"
        "token: {type: INTEGER text: \"1\"}"
      "}"
      "statement: {"
        "token: {type: IDENTIFIER text: \"y\"}"
        "token: {type: KEYWORD text: \"=\"}"
        "token: {type: INTEGER text: \"2\"}"
      "}"
      "statement: {"
        "token: {type: IDENTIFIER text: \"z\"}"
        "token: {type: KEYWORD text: \"=\"}"
        "token: {type: INTEGER text: \"1\"}"
        "token: {type: KEYWORD text: \"+\"}"
        "token: {type: INTEGER text: \"2\"}"
      "}"
    "}}",
    statement);
}

}  // namespace
}  // namespace parser
}  // namespace evlan
