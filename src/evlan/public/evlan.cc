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
// A command-line interactive interpreter for Evlan code.

#include <unistd.h>
#include <iostream>
#include <string>

#include "evlan/proto/module.pb.h"
#include "evlan/parser/parser.h"
#include "evlan/public/evaluator.h"
#include "evlan/public/interactive_interpreter.h"
#include <google/protobuf/testing/file.h>

// Use DebugMemoryManager for garbage collection.  May be very slow.
bool FLAGS_debug_memory_manager = false;
// Set true to print debug info, such as the contents of the parsed module.
bool FLAGS_verbose = false;

namespace evlan {

int InterpreterMain(int argc, char* argv[]) {
  for (int i = 1; i < argc; i++) {
    if (StringPiece(argv[i]) == "--debug_memory_manager") {
      FLAGS_debug_memory_manager = true;
    } else if (StringPiece(argv[i]) == "--verbose") {
      FLAGS_verbose = true;
    } else {
      cerr << "Unknown parameter: " << argv[i] << endl;
      return 1;
    }
  }

  bool interactive_mode = isatty(STDIN_FILENO);

  if (interactive_mode) {
    cout <<
      "Evlan Interactive Interpreter\n"
      "Type an expression to be evaluated.  End each expression with a "
        "blank line.\n"
      "Use Ctrl-D (EOF) to quit.  For command history, run using rlwrap, "
        "e.g.:\n"
      "  $ sudo apt-get install rlwrap\n"
      "  $ rlwrap " << argv[0] << endl;
  }

  EvlanEvaluator::Options options;
  if (FLAGS_debug_memory_manager) {
    options.UseDebugMemoryManager();

    // DebugMemoryManager isn't fast enough for support code.
    options.UseNoSupportCode();
  }
  InteractiveInterpreter interpreter(options);

  // The read-eval-print loop.
  string code;
  while (InteractiveInterpreter::ReadStatementFromStdin(&code)) {
    string result;
    if (FLAGS_verbose) {
      cout << "============================================================\n"
              "Parsing..." << endl;

      string debug_info;
      result = interpreter.EvaluateWithDebugInfo(code, &debug_info);

      cout << debug_info
           << "============================================================\n"
           << "Result:" << endl;
    } else {
      result = interpreter.Evaluate(code);
    }

    // Print
    cout << result << endl;
  }

  return 0;
}

}  // namespace evlan

int main(int argc, char** argv) {
  return evlan::InterpreterMain(argc, argv);
}
