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

#ifndef EVLAN_PUBLIC_INTERACTIVE_INTERPRETER_H_
#define EVLAN_PUBLIC_INTERACTIVE_INTERPRETER_H_

#include <unordered_map>
#include <string>
#include <vector>

#include "evlan/common.h"
#include "evlan/proto/module.pb.h"
#include "evlan/public/evaluator.h"
#include "evlan/public/import_resolver.h"

namespace evlan {

class EvlanValue;
class ImportResolver;

class InteractiveInterpreter {
 public:
  explicit InteractiveInterpreter(const EvlanEvaluator::Options& options);
  explicit InteractiveInterpreter(EvlanEvaluator* evaluator);
  InteractiveInterpreter();
  ~InteractiveInterpreter();

  // Read a statement from stdin.  If stdin is a terminal, it is read
  // interactively.  The statement may cover multiple lines.  In interactive
  // mode, the statement is terminated by entering a blank line (though this
  // may change in the future).  In non-interactive mode, the statement is
  // terminated by EOF.
  //
  // Returns false if there are no more statements to read (e.g. the user
  // pressed ctrl+D).
  static bool ReadStatementFromStdin(string* code);

  // Parse a statement to produce a Module.  input_variables, if non-empty, is
  // a list of variable names currently defined in the interpreter environment.
  // The module will evaluate to a function which takes a tuple containing the
  // values of these variables as a parameter.  If input_variables is empty,
  // then the module will not evaluate to any such function.  Hint:  Pass the
  // same input_variables to this and to EvaluateModule(), below.
  //
  // If the statement is an assignment, *assign_to will be filled in with the
  // variable name being assigned.  Otherwise, assign_to is not touched.
  static void ParseStatement(const string& code,
                             const vector<string>& input_variables,
                             Module* output, string* assign_to);

  // Evaluate the given code and print the DebugString() of the result.  If
  // debug_info is provided, store verbose debug information to it which the
  // caller may want to print.
  string Evaluate(const string& code);
  string EvaluateWithDebugInfo(const string& code, string* debug_info);

  // Evaluate a pre-parsed module.  If input_variables is non-empty, then the
  // module should evaluate to a function which takes input_variables.size()
  // inputs.  This will be called with the values of the named variables, and
  // the result of that call is what will actually be returned.  If
  // input_variables is empty, then the module is evaluated and the result
  // returned directly without trying to call it as a function (so, the
  // module is NOT expected to evaluate to a zero-arg function).
  string EvaluateModule(const Module& module,
                        const vector<string>& input_variables,
                        const string& assign_to);
  string EvaluateModuleWithDebugInfo(const Module& module,
                                     const vector<string>& input_variables,
                                     const string& assign_to,
                                     string* debug_info);

  // Like EvaluateModuleWithDebugInfo but does not block.  |done| will be
  // executed when evaluation is complete.  Note that if evaluation completes
  // immediately, |done| may be called synchronously before EvaluateNonBlocking
  // returns.
  // TODO(kenton):  Do something about proliferation of parameters.  The above
  //   methods should probably support map_as_import as well...
  void EvaluateNonBlocking(const Module& module,
                           const vector<string>& input_variables,
                           const string& assign_to,
                           const string& map_as_import,
                           string* result,
                           string* debug_info,
                           Closure* done);

  void ClearTopLevelVariables();

  void MapImportPrefix(const string& prefix, ImportResolver* sub_resolver) {
    import_resolver_.MapPrefix(prefix, sub_resolver);
  }

 private:
  scoped_ptr<EvlanEvaluator> owned_evaluator_;
  EvlanEvaluator* evaluator_;
  unordered_map<string, EvlanValue*> saved_variables_;
  MappedImportResolver import_resolver_;

  void AsyncDone(string assign_to, string map_as_import,
                 string* result, Closure* done, EvlanValue* value);
  void GetGlobals(const vector<string>& names, vector<EvlanValue*>* output);
  void MaybeGetDebugInfo(const Module& module, string* debug_info);
  void MaybeSaveResult(EvlanValue* value, const string& assign_to,
                       const string& map_as_import);
};

}  // namespace evlan

#endif  // EVLAN_PUBLIC_INTERACTIVE_INTERPRETER_H_
