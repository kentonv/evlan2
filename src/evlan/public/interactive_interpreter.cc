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

#include <unistd.h>
#include <unordered_map>
#include <string>
#include <iostream>

#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/parser/lexer.h"
#include "evlan/parser/lexer.pb.h"
#include "evlan/parser/parser.h"
#include "evlan/proto/module.pb.h"
#include "evlan/public/evaluator.h"
#include "evlan/strutil.h"
#include "evlan/map-util.h"
#include "evlan/stl-util.h"

namespace evlan {

using namespace std::placeholders;

// Helpers
//

namespace {

// This function returns true if 'code' is a statement of the form:
//   <varname> = <body>
// Or:
//   <varname>(<params>) = <body>
bool IsTopLevelAssignment(const parser::Statement& code) {
  // TODO(kenton):  Call on the parser to make this decision for us.  We should
  //   probably improve the parser first so that it doesn't double-report
  //   errors.

  // Must start with an identifier.
  if (code.token_size() == 0 ||
      code.token(0).type() != parser::Token::IDENTIFIER) {
    return false;
  }

  // May be followed by an argument list.
  int pos = 1;
  if (pos < code.token_size() &&
      code.token(pos).type() == parser::Token::KEYWORD &&
      code.token(pos).text() == "(") {
    // Saw an open parenthesis.  This could be a function definition.  That is,
    // it may have the form:
    //   f(...) = ...
    // In that case, it should be treated as an assignment.

    // Skip stuff inside parens.
    while(true) {
      ++pos;
      if (pos == code.token_size()) {
        // Did not find closing paren, so this can't be a function definition.
        return false;
      }
      if (code.token(pos).type() == parser::Token::KEYWORD &&
          code.token(pos).text() == ")") {
        // Found closing paren.
        ++pos;  // Advance over it.
        break;
      }
    }
  }

  return pos < code.token_size() &&
         code.token(pos).type() == parser::Token::KEYWORD &&
         code.token(pos).text() == "=";
}

// This function takes a code statement of the form:
//   <varname> = <body>
// and returns fills the target with a statement of the form:
//   <varname> where
//     <varname = <body>
void InsertWhereBlock(const parser::Statement& code,
                      parser::Statement* target) {
  target->Clear();
  // Add the name identifier.
  parser::Token* top_level_indentifier = target->add_token();
  top_level_indentifier->CopyFrom(code.token(0));
  // Add "where".
  parser::Token* where_token = target->add_token();
  where_token->set_type(parser::Token::KEYWORD);
  where_token->set_text("where");
  // Add the orginial statment, within a block.
  parser::Token* block_token = target->add_token();
  block_token->set_type(parser::Token::BLOCK);
  block_token->mutable_block()->add_statement()->CopyFrom(code);
}

string GetFirstIdentifier(const parser::Statement& assignment) {
  GOOGLE_CHECK_GT(assignment.token_size(), 0);
  GOOGLE_CHECK_EQ(assignment.token(0).type(), parser::Token::IDENTIFIER);
  return assignment.token(0).text();
}

void AddTextToken(const string& text,
                  parser::Token::Type type,
                  parser::Statement* statement) {
  parser::Token* next_token = statement->add_token();
  next_token->set_type(type);
  next_token->set_text(text);
}

// Takes a list of arguments, and builds the Statement which contains that
// argument list.
// vector<string> => "(<string0>, <string1>, ... <stringN>)"
void BuildArgList(const vector<string>& args,
                  parser::Statement* statement) {
  AddTextToken("(", parser::Token::KEYWORD, statement);
  if (args.size() != 0) {
    AddTextToken(args[0], parser::Token::IDENTIFIER, statement);
  }
  for (int i = 1; i < args.size(); ++i) {
    AddTextToken(",", parser::Token::KEYWORD, statement);
    AddTextToken(args[i], parser::Token::IDENTIFIER, statement);
  }
  AddTextToken(")", parser::Token::KEYWORD, statement);
}

}  // namespace

// InteractiveInterpreter
//

InteractiveInterpreter::InteractiveInterpreter(
    const EvlanEvaluator::Options& options)
    : owned_evaluator_(new EvlanEvaluator(options)),
      evaluator_(owned_evaluator_.get()) {}

InteractiveInterpreter::InteractiveInterpreter(EvlanEvaluator* evaluator)
    : evaluator_(evaluator) {}

InteractiveInterpreter::InteractiveInterpreter()
    : owned_evaluator_(new EvlanEvaluator()),
      evaluator_(owned_evaluator_.get()) {}

InteractiveInterpreter::~InteractiveInterpreter() {
  ClearTopLevelVariables();
}

bool InteractiveInterpreter::ReadStatementFromStdin(string* code) {
  bool interactive_mode = isatty(STDIN_FILENO);

  code->clear();
  while (code->empty()) {
    if (!cin) return false;

    // Read an expression, ending on a blank line of EOF.
    while (true) {
      if (interactive_mode) {
        if (code->empty()) {
          cout << ">>> ";
        } else {
          cout << "... ";
        }
      }

      string line;
      if (!getline(cin, line)) {
        // EOF
        if (interactive_mode) {
          // User pressed Ctrl+D, which does not cause a newline to be echoed.
          // Let's write one so that the next thing we print won't be attached
          // to the end of the prompt line.
          cout << endl;
        }
        break;
      }
      if (interactive_mode && line.empty()) {
        break;  // blank line = end of expression in interactive mode
      }
      code->append(line);
      code->push_back('\n');
    }
  }

  return true;
}

void InteractiveInterpreter::ParseStatement(
    const string& code,
    const vector<string>& input_variables,
    Module* output, string* assign_to) {
  parser::Statement function_statement;

  if (!input_variables.empty()) {
    // Gets the global values, and also starts building the function to evaluate.
    // So far:
    // ( <arg0> <arg1> .... <argN> )
    BuildArgList(input_variables, &function_statement);

    // ( <arg0> <arg1> .... <argN> ) =>
    AddTextToken("=>", parser::Token::KEYWORD, &function_statement);
  }

  // Parse
  parser::Statement body_statement;
  parser::Statement expanded_statement;
  parser::Lex(code, &body_statement);
  if (IsTopLevelAssignment(body_statement)) {
    InsertWhereBlock(body_statement, &expanded_statement);
    *assign_to = GetFirstIdentifier(expanded_statement);
  } else {
    expanded_statement.Swap(&body_statement);
    assign_to->clear();
  }
  // ( <arg0> <arg1> .... <argN> ) => <body>
  function_statement.MergeFrom(expanded_statement);

  evlan::parser::ParseStatement(function_statement, output);
}

string InteractiveInterpreter::Evaluate(const string& code) {
  return EvaluateWithDebugInfo(code, NULL);
}

string InteractiveInterpreter::EvaluateModule(
    const Module& module,
    const vector<string>& input_variables,
    const string& assign_to) {
  return EvaluateModuleWithDebugInfo(module, input_variables, assign_to, NULL);
}

string InteractiveInterpreter::EvaluateWithDebugInfo(const string& code,
                                                     string* debug_info) {
  vector<string> global_names;
  for (unordered_map<string, EvlanValue*>::const_iterator it =
           saved_variables_.begin();
       it != saved_variables_.end(); ++it) {
    global_names.push_back(it->first);
  }

  Module module;
  string new_global;
  ParseStatement(code, global_names, &module, &new_global);

  return EvaluateModuleWithDebugInfo(module, global_names,
                                     new_global, debug_info);
}

string InteractiveInterpreter::EvaluateModuleWithDebugInfo(
    const Module& module, const vector<string>& input_variables,
    const string& assign_to, string* debug_info) {
  vector<EvlanValue*> global_values;
  GetGlobals(input_variables, &global_values);
  MaybeGetDebugInfo(module, debug_info);

  // Evaluate
  scoped_ptr<EvlanValue> value_function(
      evaluator_->Evaluate(module, &import_resolver_));

  scoped_ptr<EvlanValue> value;
  if (global_values.empty()) {
    // No input variables, so the module actually evaluated to just a value,
    // not a function.
    value.reset(value_function.release());
  } else {
    value.reset(value_function->Call(global_values));
  }

  // Finish up.
  MaybeSaveResult(value.get(), assign_to, "");
  return value->DebugString();
}

void InteractiveInterpreter::EvaluateNonBlocking(
    const Module& module,
    const vector<string>& input_variables,
    const string& assign_to,
    const string& map_as_import,
    string* result,
    string* debug_info,
    Closure* done) {
  vector<EvlanValue*> global_values;
  GetGlobals(input_variables, &global_values);
  MaybeGetDebugInfo(module, debug_info);

  // Evaluate
  // TODO(kenton):  Evaluate asynchronously.
  scoped_ptr<EvlanValue> value_function(
      evaluator_->Evaluate(module, &import_resolver_));

  scoped_ptr<EvlanValue> value;
  if (global_values.empty()) {
    AsyncDone(assign_to, map_as_import, result, done,
              value_function.release());
  } else {
    value_function->CallNonBlocking(global_values,
        std::bind(&InteractiveInterpreter::AsyncDone, this,
                  assign_to, map_as_import, result, done, _1));
  }
}

void InteractiveInterpreter::AsyncDone(
    string assign_to, string map_as_import,
    string* result, Closure* done, EvlanValue* value) {
  // Finish up.
  MaybeSaveResult(value, assign_to, map_as_import);
  *result = value->DebugString();
  delete value;
  done->Run();
}

void InteractiveInterpreter::GetGlobals(
    const vector<string>& names, vector<EvlanValue*>* output) {
  for (int i = 0; i < names.size(); i++) {
    EvlanValue* value = FindPtrOrNull(saved_variables_, names[i]);
    if (value == NULL) {
      output->push_back(evaluator_->NewTypeError(
          strings::Substitute("No such variable: $0", names[i])));
    } else {
      output->push_back(value);
    }
  }
}

void InteractiveInterpreter::MaybeGetDebugInfo(
    const Module& module, string* debug_info) {
  if (debug_info != NULL) {
    debug_info->clear();
    debug_info->append("Module contents:\n");

    // Print the code tree.  The DebugString() output is too verbose so we
    // print it manually.
    debug_info->append("code_tree: [");
    for (int i = 0; i < module.code_tree_size(); i++) {
      if (i > 0) debug_info->append(",");
      debug_info->append(" " + SimpleItoa(module.code_tree(i)));
    }
    debug_info->append(" ]\n");

    // Print the rest of the Module.
    Module constants;
    constants.CopyFrom(module);
    constants.clear_code_tree();
    debug_info->append(constants.DebugString());

    debug_info->append(
        "============================================================\n"
        "Result:\n");
  }
}

void InteractiveInterpreter::MaybeSaveResult(
    EvlanValue* value, const string& assign_to, const string& map_as_import) {
  if (!assign_to.empty()) {
    EvlanValue** map_slot = &saved_variables_[assign_to];
    if (*map_slot != NULL) delete *map_slot;
    *map_slot = value->Clone();
  }
  if (!map_as_import.empty()) {
    import_resolver_.Map(map_as_import, value);
  }
}

void InteractiveInterpreter::ClearTopLevelVariables() {
  STLDeleteValues(&saved_variables_);
  saved_variables_.clear();
}

}  // namespace evlan
