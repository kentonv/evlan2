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

#include "evlan/parser/parser.h"

#include <string>
#include <unordered_map>
#include "evlan/common.h"
#include "evlan/proto/module.pb.h"
#include "evlan/parser/expression.h"
#include "evlan/parser/lexer.h"
#include "evlan/parser/lexer.pb.h"
#include "evlan/strutil.h"
#include "evlan/map-util.h"

namespace evlan {
namespace parser {
namespace {

struct Operator {
  int priority;
  string function;

  Operator() {}
  Operator(int priority, string function)
    : priority(priority), function(function) {}
};

unordered_map<string, Operator> MakeInfixOperatorTable() {
  unordered_map<string, Operator> result;

  result["+"] = Operator(5, "add");
  result["-"] = Operator(5, "subtract");
  result["*"] = Operator(6, "multiply");
  result["/"] = Operator(6, "divide");

  // TODO(kenton):  %, ^ operators.

  result["=="] = Operator(4, "equal");
  result["!="] = Operator(4, "notEqual");
  result["<" ] = Operator(4, "less");
  result[">" ] = Operator(4, "greater");
  result["<="] = Operator(4, "notGreater");
  result[">="] = Operator(4, "notLess");

  result["and"] = Operator(2, "and");
  result["or" ] = Operator(2, "or");

  return result;
}

unordered_map<string, Operator> MakePrefixOperatorTable() {
  unordered_map<string, Operator> result;

  result["-"] = Operator(6, "negate");
  result["not"] = Operator(3, "not");

  return result;
}

const unordered_map<string, Operator> kInfixOperators =
  MakeInfixOperatorTable();
const unordered_map<string, Operator> kPrefixOperators =
  MakePrefixOperatorTable();

// Iterates over the tokens in a statement.
class TokenIterator {
 public:
  TokenIterator(const Statement& statement)
    : statement_(&statement),
      index_(0),
      current_(statement.token_size() == 0 ?
               &kEmptyToken : &statement.token(0)) {
  }

  // Get the current token.
  const Token& current() const {
    return *current_;
  }

  // Advance to the next token.
  void Next() {
    GOOGLE_DCHECK_LT(index_, statement_->token_size());
    ++index_;

    if (index_ < statement_->token_size()) {
      current_ = &statement_->token(index_);
    } else {
      current_ = &kEmptyToken;
    }
  }


  // Returns true if we have consumed all identifiers in the statement, false
  // otherwise.
  bool AtEnd() const {
    return current_ == &kEmptyToken;
  }

  // If we are not at the end of the statement, report an error.
  void ExpectAtEnd() const {
    if (!AtEnd()) {
      GOOGLE_LOG(ERROR) << "Expected end of statement.";
    }
  }

  // If the next token is of the given type, return true, otherwise return
  // false.  Does not consume the token.
  bool LookingAtType(Token::Type type) const {
    return current_->type() == type;
  }

  // If the next token is the given keyword, return true, otherwise return
  // false.  Does not consume the token.
  bool LookingAtKeyword(const char* text) const {
    return LookingAtType(Token::KEYWORD) && current_->text() == text;
  }

  // If the next token is the given keyword, consumes it and returns true,
  // otherwise returns false.
  bool TryConsumeKeyword(const char* text) {
    if (!LookingAtKeyword(text)) return false;
    Next();
    return true;
  }

  // If the next token is an identifier, stores its name into *output and
  // returns true, otherwise returns false.
  bool TryConsumeIdentifier(string* output) {
    if (!LookingAtType(Token::IDENTIFIER)) return false;
    *output = current_->text();
    Next();
    return true;
  }

  // If the next token is an integer, stores its value into *output and
  // returns true, otherwise returns false.
  bool TryConsumeInteger(int* output) {
    if (!LookingAtType(Token::INTEGER)) return false;
    *output = strtol(current_->text().c_str(), NULL, 0);
    Next();
    return true;
  }

  // If the next token is a float, stores its value into *output and
  // returns true, otherwise returns false.
  bool TryConsumeFloat(double* output) {
    if (!LookingAtType(Token::FLOAT)) return false;
    *output = strtod(current_->text().c_str(), NULL);
    Next();
    return true;
  }

  // If the next token is a string literal, stores its (unescaped) value into
  // *output and returns true, otherwise returns false.
  bool TryConsumeString(string* output) {
    if (!LookingAtType(Token::STRING)) return false;
    strings::UnescapeCEscapeString(
        current_->text().substr(1, current_->text().size() - 2),
        output);
    Next();
    return true;
  }

  // If the next token is a prefix operator, returns a pointer to its
  // description, otherwise returns NULL.
  const Operator* TryConsumePrefixOperator() {
    if (!LookingAtType(Token::KEYWORD)) return NULL;
    const Operator* op = FindOrNull(kPrefixOperators, current_->text());
    if (op != NULL) Next();
    return op;
  }

  // If the next token is an infix operator, returns a pointer to its
  // description, otherwise returns NULL.
  const Operator* TryConsumeInfixOperator(int min_priority) {
    if (!LookingAtType(Token::KEYWORD)) return NULL;
    const Operator* op = FindOrNull(kInfixOperators, current_->text());
    if (op == NULL || op->priority < min_priority) return NULL;
    Next();
    return op;
  }

  // If the next token is the given keyword, consume it, otherwise report an
  // error.
  void ConsumeKeyword(const char* text) {
    if (!TryConsumeKeyword(text)) {
      GOOGLE_LOG(ERROR) << "Expected \"" << text << "\".";
    }
  }

  // If the next token is an identifier, consume it and store its name in
  // *output, otherwise report an error.
  void ConsumeIdentifier(string* output) {
    if (!TryConsumeIdentifier(output)) {
      GOOGLE_LOG(ERROR) << "Expected identifier.";
    }
  }

  // If the next token is a string literal, consume it and store its unescaped
  // value in *output, otherwise report an error.
  void ConsumeString(string* output) {
    if (!TryConsumeString(output)) {
      GOOGLE_LOG(ERROR) << "Expected string.";
    }
  }

 private:
  const Statement* statement_;
  int index_;
  const Token* current_;

  static const Token kEmptyToken;
};

const Token TokenIterator::kEmptyToken;

// ===================================================================
// Recursive-descent parser.

Expression* ParseExpression(TokenIterator* input);
Expression* ParseTuple(TokenIterator* input);
Expression* ParseArray(TokenIterator* input);
Expression* TryParseFunction(TokenIterator* input);
Expression* ParseObject(TokenIterator* input);
Expression* ParseBlockArray(TokenIterator* input);
Expression* TryParseInlineObject(TokenIterator* input);

// This function handles parsing most simple, atomic expressions, as well as
// a few other things.
Expression* ParseItem(TokenIterator* input) {
  string identifier;
  string text;
  int i;
  double d;

  Expression* expression;

  if (input->TryConsumeIdentifier(&identifier)) {
    // Saw an identifier.  It could be a function definition, e.g.:
    //   x => x + 1
    // Or it might just be a variable.
    if (input->TryConsumeKeyword("=>")) {
      // It's a function definition.
      expression = new Abstraction(identifier, ParseExpression(input));
    } else {
      // Just a variable.
      expression = new Variable(identifier, 0);
    }
  } else if (input->TryConsumeFloat(&d)) {
    expression = MakeDoubleLiteral(d);
  } else if (input->TryConsumeInteger(&i)) {
    expression = new IntegerLiteral(i);
  } else if (input->TryConsumeString(&text)) {
    expression = new StringLiteral(text);
  } else if (input->TryConsumeKeyword("@")) {
    // An atom.
    string name;
    input->ConsumeIdentifier(&name);
    expression = new AtomLiteral(name);
  } else if (input->TryConsumeKeyword("(")) {
    // This may be a parenthesized expression or a function definition.  Try
    // parsing as a function definition first.
    TokenIterator backup(*input);
    expression = TryParseFunction(input);
    if (expression == NULL) {
      // Not a function.  Must be a simple parenthesized expression.
      *input = backup;
      expression = ParseExpression(input);
      input->ConsumeKeyword(")");
    }
  } else if (input->TryConsumeKeyword("{")) {
    // This may either be an array literal or an inline object literal.  Try
    // the latter first.
    TokenIterator backup(*input);
    expression = TryParseInlineObject(input);
    if (expression == NULL) {
      // Must just be an array.
      *input = backup;
      expression = ParseArray(input);
    }
  } else if (input->TryConsumeKeyword("import")) {
    string name;
    input->ConsumeString(&name);
    expression = new Import(name);
  } else if (input->TryConsumeKeyword("if")) {
    Expression* condition = ParseExpression(input);

    input->ConsumeKeyword("then");
    Expression* true_clause = ParseExpression(input);
    input->ConsumeKeyword("else");
    Expression* false_clause = ParseExpression(input);

    expression = MakeIf(condition, true_clause, false_clause);

  } else if (input->TryConsumeKeyword("object")) {
    expression = ParseObject(input);
  } else if (input->TryConsumeKeyword("array")) {
    expression = ParseBlockArray(input);
  } else if (input->TryConsumeKeyword("true")) {
    expression = MakeBooleanLiteral(true);
  } else if (input->TryConsumeKeyword("false")) {
    expression = MakeBooleanLiteral(false);
  } else if (input->TryConsumeKeyword("\\")) {
    // An escaped variable.  Example:
    //   makeVector = (x, y, z) => object of
    //     x = \x
    //     y = \y
    //     z = \z
    // Without the backslashes, the above code would be defining x, y, and z
    // to be equal to themselves rather than to the function parameters.
    int escape_level = 1;
    while (input->TryConsumeKeyword("\\")) ++escape_level;
    input->ConsumeIdentifier(&identifier);
    expression = new Variable(identifier, escape_level);
  } else {
    GOOGLE_LOG(ERROR) << "Expected item.";
    if (!input->AtEnd()) input->Next();  // Avoid infinite loops.
    expression = new IntegerLiteral(0);  // Return some dummy value.
  }

  return expression;
}

// Parse code which can be attached to the end of an item, like a member
// access, function call, or subscript.
Expression* ParsePostfix(Expression* base, TokenIterator* input) {
  if (input->TryConsumeKeyword("(")) {
    return new Application(base, ParseTuple(input));
  } else if (input->TryConsumeKeyword("of")) {
    // A multi-line application, e.g.:
    //   f of
    //     @foo
    //     "bar"
    // which is the same as:
    //   f(@foo, "bar")
    vector<Expression*> params;

    if (input->LookingAtType(Token::BLOCK)) {
      const Block& block = input->current().block();
      for (int i = 0; i < block.statement_size(); i++) {
        TokenIterator sub_input(block.statement(i));
        params.push_back(ParseExpression(&sub_input));
        sub_input.ExpectAtEnd();
      }
      input->Next();
    } else {
      GOOGLE_LOG(ERROR) << "Expected block after \"of\".";
    }

    Expression* param;

    // We don't bother wrapping in a tuple if there is only one parameter.
    if (params.size() == 1) {
      param = params[0];
    } else {
      param = MakeTuple(params);
    }

    return new Application(base, param);
  } else if (input->TryConsumeKeyword(".")) {
    // Member access.
    string member_name;
    input->ConsumeIdentifier(&member_name);
    return new Application(base, new AtomLiteral(member_name));
  } else if (input->TryConsumeKeyword("[")) {
    // Array subscript.
    Expression* subscript = ParseExpression(input);
    input->ConsumeKeyword("]");
    return MakeSubscript(base, subscript);
  } else {
    return NULL;
  }
}

// Parse an item and any number of postfixes.
Expression* ParseItemWithPostfix(TokenIterator* input) {
  Expression* expression = ParseItem(input);

  while (true) {
    Expression* with_postfix = ParsePostfix(expression, input);
    if (with_postfix == NULL) break;
    expression = with_postfix;
  }

  return expression;
}

// Parse arithmetic operators.
Expression* ParseOperators(TokenIterator* input, int min_priority) {
  Expression* left;

  const Operator* prefix_op = input->TryConsumePrefixOperator();
  if (prefix_op != NULL) {
    left = ParseOperators(input, max(min_priority, prefix_op->priority + 1));
    left = new Application(left, new AtomLiteral(prefix_op->function));
  } else {
    left = ParseItemWithPostfix(input);
  }

  while (true) {
    const Operator* infix_op = input->TryConsumeInfixOperator(min_priority);
    if (infix_op == NULL) {
      return left;
    }

    Expression* right = ParseOperators(input, infix_op->priority + 1);

    vector<Expression*> operands;
    operands.push_back(left);
    operands.push_back(right);

    left = new Application(
      new Application(left, new AtomLiteral(infix_op->function)),
      right);
  }
}

// Parse an assignment statement, e.g. a line of a where block or of an
// "object of" block.
Expression* ParseAssignment(TokenIterator* input, string* name) {
  input->ConsumeIdentifier(name);

  if (input->TryConsumeKeyword("(")) {
    // Declaring a function using "f(x) = ..." form.
    vector<string> variable_names;

    if (!input->TryConsumeKeyword(")")) {
      while (true) {
        string name;
        input->ConsumeIdentifier(&name);
        variable_names.push_back(name);

        if (input->TryConsumeKeyword(")")) break;
        input->ConsumeKeyword(",");
      }
    }

    input->ConsumeKeyword("=");
    return new Abstraction(variable_names, ParseExpression(input));
  } else {
    input->ConsumeKeyword("=");
    return ParseExpression(input);
  }
}

// Parse one line of a where block.
void ParseWhereStatement(TokenIterator* input, WhereBlock* output) {
  string name;
  Expression* value = ParseAssignment(input, &name);

  if (!output->AddBinding(name, value)) {
    GOOGLE_LOG(ERROR) << name << " is already defined in this where block.";
  }
}

// Parse a where block, assuming we just consumed the "where" keyword itself.
// |base| is the expression that was parsed before the "where" keyword.
Expression* ParseWhere(Expression* base, TokenIterator* input) {
  WhereBlock* result = new WhereBlock(base);

  if (input->LookingAtType(Token::BLOCK)) {
    const Block& block = input->current().block();
    for (int i = 0; i < block.statement_size(); i++) {
      TokenIterator sub_input(block.statement(i));
      ParseWhereStatement(&sub_input, result);
      sub_input.ExpectAtEnd();
    }
    input->Next();
  } else {
    ParseWhereStatement(input, result);
  }

  return result;
}

// Parse an object, assuming we just consumed the "object" keyword itself.
Expression* ParseObject(TokenIterator* input) {
  vector<pair<string, Expression*> > members;

  input->ConsumeKeyword("of");

  if (input->LookingAtType(Token::BLOCK)) {
    const Block& block = input->current().block();
    for (int i = 0; i < block.statement_size(); i++) {
      TokenIterator sub_input(block.statement(i));
      string name;
      Expression* value = ParseAssignment(&sub_input, &name);
      members.push_back(make_pair(name, value));
      sub_input.ExpectAtEnd();
    }
    input->Next();
  } else {
    GOOGLE_LOG(ERROR) << "Expected block.";
  }

  return MakeObject(members);
}

// Parse a block array, assuming we just consumed the "array" keyword itself.
Expression* ParseBlockArray(TokenIterator* input) {
  vector<Expression*> elements;

  input->ConsumeKeyword("of");

  if (input->LookingAtType(Token::BLOCK)) {
    const Block& block = input->current().block();
    for (int i = 0; i < block.statement_size(); i++) {
      TokenIterator sub_input(block.statement(i));
      elements.push_back(ParseExpression(&sub_input));
      sub_input.ExpectAtEnd();
    }
    input->Next();
  } else {
    GOOGLE_LOG(ERROR) << "Expected block.";
  }

  return MakeArray(elements);
}

// Try to parse an inline object, e.g. {x = 1, y = 2, z = 3}, but return
// NULL if it's possible that this is actually an array.
Expression* TryParseInlineObject(TokenIterator* input) {
  string first_name;
  if (!input->TryConsumeIdentifier(&first_name)) return NULL;
  if (!input->TryConsumeKeyword("=")) return NULL;

  // Definitely an object.
  vector<pair<string, Expression*> > members;
  members.push_back(make_pair(first_name, ParseExpression(input)));

  while (input->TryConsumeKeyword(",")) {
    string name;
    input->ConsumeIdentifier(&name);
    input->ConsumeKeyword("=");
    members.push_back(make_pair(name, ParseExpression(input)));
  }

  input->ConsumeKeyword("}");

  return MakeObject(members);
}

// Parse a complete expression.
Expression* ParseExpression(TokenIterator* input) {
  Expression* expression = ParseOperators(input, kint32min);

  if (input->TryConsumeKeyword("where")) {
    expression = ParseWhere(expression, input);
  }

  return expression;
}

// Parse a tuple (e.g. parameters to a function call).
Expression* ParseTuple(TokenIterator* input) {
  vector<Expression*> parts;

  if (!input->TryConsumeKeyword(")")) {
    parts.push_back(ParseExpression(input));

    while (input->TryConsumeKeyword(",")) {
      parts.push_back(ParseExpression(input));
    }

    input->ConsumeKeyword(")");
  }

  // We don't bother wrapping in a tuple if there is only one parameter.
  if (parts.size() == 1) {
    return parts[0];
  } else {
    return MakeTuple(parts);
  }
}

// Parse an array literal.
Expression* ParseArray(TokenIterator* input) {
  vector<Expression*> parts;

  if (!input->TryConsumeKeyword("}")) {
    parts.push_back(ParseExpression(input));

    while (input->TryConsumeKeyword(",")) {
      parts.push_back(ParseExpression(input));
    }

    input->ConsumeKeyword("}");
  }

  return MakeArray(parts);
}

// Try to parse a function definition that starts with an open parenthesis,
// e.g.:
//   (x, y) => x + y
// Assume we have already consumed the open parenthesis.
//
// If this doesn't appear to be a function (e.g. if it's just a parenthesized
// expression), return NULL.
Expression* TryParseFunction(TokenIterator* input) {
  vector<string> variable_names;

  if (!input->TryConsumeKeyword(")")) {
    while (true) {
      string name;
      if (!input->TryConsumeIdentifier(&name)) return NULL;
      variable_names.push_back(name);

      if (input->TryConsumeKeyword(")")) break;
      if (!input->TryConsumeKeyword(",")) return NULL;
    }
  }

  if (!input->TryConsumeKeyword("=>")) return NULL;

  return new Abstraction(variable_names, ParseExpression(input));
}

}

void ParseStatement(const Statement& code, Module* output) {
  TokenIterator input(code);
  ExpressionRoot root(ParseExpression(&input));
  input.ExpectAtEnd();
  root.Compile(output);
}

void Parse(const string& code, Module* output) {
  Statement statement;
  Lex(code, &statement);
  ParseStatement(statement, output);
}

}  // namespace parser
}  // namespace evlan
