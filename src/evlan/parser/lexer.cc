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

#include <unordered_set>
#include <vector>

#include "evlan/common.h"
#include "evlan/parser/lexer.pb.h"
#include "evlan/strutil.h"
#include "evlan/stl-util.h"

// HACK HACK HACK:  PCRE's version of Scanner lacks the ColumnNumber() method.
//   To implement it here, we cheat and access the private members.
#define private public
#include <pcre_scanner.h>
#undef private

namespace evlan {
namespace parser {
namespace {

using pcrecpp::Scanner;

int ColumnNumber(Scanner* scanner) {
  const char* pos = scanner->input_.data();
  const char* begin = scanner->data_.data();
  while (pos > begin && *(pos - 1) != '\n') {
    --pos;
  }

  return 1 + (scanner->input_.data() - pos);
}



unordered_set<string> GetKeywords() {
  unordered_set<string> result;

  result.insert("and");
  result.insert("array");
  result.insert("class");
  result.insert("else");
  result.insert("false");
  result.insert("if");
  result.insert("import");
  result.insert("not");
  result.insert("object");
  result.insert("of");
  result.insert("or");
  result.insert("then");
  result.insert("true");
  result.insert("where");

  return result;
}

unordered_set<string> GetBlockKeywords() {
  unordered_set<string> result;

  result.insert("of");
  result.insert("where");

  return result;
}

const unordered_set<string> kKeywords = GetKeywords();
const unordered_set<string> kBlockKeywords = GetBlockKeywords();

// Returns true if the last token in the statement is a block keyword.
bool LastTokenWasBlockKeyword(const Statement& statement) {
  if (statement.token_size() == 0) return false;

  const Token& token = statement.token(statement.token_size() - 1);
  return token.type() == Token::KEYWORD &&
    kBlockKeywords.count(token.text()) > 0;
}

int ParseStatement(Scanner* scanner, int indent_depth, Statement* statement);

// Parses a block whose first line is indented by the given number of spaces.
int ParseBlock(Scanner* scanner, int indent_depth, Block* block) {
  while (true) {
    int next_indent =
      ParseStatement(scanner, indent_depth, block->add_statement());
    if (next_indent < indent_depth) return next_indent;
  }
}

// Parse a statement whose first line is indented by the given number of spaces.
int ParseStatement(Scanner* scanner, int indent_depth, Statement* statement) {
  string text;

  while (scanner->LookingAt(".|\n")) {
    if (scanner->Consume("\n")) {
      // new line
      scanner->Consume(" *");
      if (!scanner->LookingAt("[\n#]")) {
        // This is a meaningful line.
        int new_indent = ColumnNumber(scanner) - 1;
        if (new_indent <= indent_depth) {
          // End of statement.
          return new_indent;
        }

        if (LastTokenWasBlockKeyword(*statement)) {
          Token* token = statement->add_token();
          token->set_type(Token::BLOCK);
          new_indent = ParseBlock(scanner, new_indent, token->mutable_block());
          if (new_indent <= indent_depth) {
            // End of statement.
            return new_indent;
          }
        }
      }
    } else if (scanner->Consume(" +|#[^\n]*")) {
      // ignore comment/whitespace
    } else if (scanner->Consume("([a-zA-Z_][a-zA-Z0-9_]*)", &text)) {
      // keyword or identifier
      Token* token = statement->add_token();
      token->set_text(text);
      token->set_type(kKeywords.count(text) > 0 ?
                      Token::KEYWORD : Token::IDENTIFIER);
    } else if (scanner->Consume("[$]([a-zA-Z_][a-zA-Z0-9_]*)", &text)) {
      // identifier
      Token* token = statement->add_token();
      token->set_text(text);
      token->set_type(Token::IDENTIFIER);
    } else if (scanner->Consume("([0-9]+([.][0-9]+([eE][+-]?[0-9]+)?"
                                "|([eE][+-]?[0-9]+)))", &text)) {
      // float
      Token* token = statement->add_token();
      token->set_type(Token::FLOAT);
      token->set_text(text);

      if (scanner->LookingAt("[a-zA-Z_]")) {
        GOOGLE_LOG(ERROR) << scanner->LineNumber() << ":"
                   << ColumnNumber(scanner)
                   << ": Missing space after number.";
      }

    } else if (scanner->Consume("([0-9]+)", &text)) {
      // integer
      Token* token = statement->add_token();
      token->set_type(Token::INTEGER);
      token->set_text(text);
      if (scanner->LookingAt("[a-zA-Z_]")) {
        GOOGLE_LOG(ERROR) << scanner->LineNumber() << ":"
                   << ColumnNumber(scanner)
                   << ": Missing space after number.";
      }
    } else if (scanner->Consume(
        "(>=|<=|==|!=|=>|->|[][!@%^*()+=<>{}/\\\\:.,-])", &text)) {
      // symbol
      Token* token = statement->add_token();
      token->set_type(Token::KEYWORD);
      token->set_text(text);
    } else if (scanner->Consume(
        "(\"([^\"\\\\]|[\\\\].)*\")", &text)) {
      // string
      Token* token = statement->add_token();
      token->set_type(Token::STRING);
      token->set_text(text);
    } else {
      GOOGLE_LOG(ERROR) << scanner->LineNumber() << ":"
                 << ColumnNumber(scanner)
                 << ": Illegal character.";

      // If this CHECK fails we're in an infinite loop anyway.
      GOOGLE_CHECK(scanner->Consume("."));
    }
  }

  return -1;
}

}  // namespace

void Lex(const string& input, Statement* output) {
  Scanner scanner(input);

  // We use an initial indentation of -1 rather than zero because if it were
  // zero then any line that is not indented would be seen as the start of a
  // new statement.  We want to parse the file as a single statement, even if
  // multiple lines in the file are not indented.
  ParseStatement(&scanner, -1, output);
}

}  // namespace parser
}  // namespace evlan
