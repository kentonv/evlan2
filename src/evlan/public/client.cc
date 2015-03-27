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
// Evlan client, which interactively prompts the user for code and then sends
// that code to an Evlan server to be evaluated.

#include <unistd.h>
#include <unordered_set>
#include <iostream>

#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/proto/module.pb.h"
#include "evlan/proto/evaluator.pb.h"
#include "evlan/public/interactive_interpreter.h"
#include "evlan/parser/parser.h"
#include "evlan/strutil.h"
#include "evlan/rpc.h"
#include "evlan/fake_rpc.h"

// Set true to print debug info, such as the contents of the parsed module.
bool FLAGS_verbose = false;
// Evlan server address.
string FLAGS_server = "";

namespace evlan {
namespace {

class EvaluatorClient {
 public:
  EvaluatorClient(const string& server_address, bool verbose)
      : channel_(rpc_system->NewChannel(ParseAddress(server_address))),
        stub_(new EvaluatorService::Stub(channel_.get())),
        context_id_(NewGlobalID()),
        verbose_(verbose) {}

  string Evaluate(const string& code) {
    scoped_ptr<google::protobuf::RpcController> rpc(rpc_system_.NewController());
    EvaluatorRequest request;
    EvaluatorResponse response;

    vector<string> input_variables(variable_names_.begin(),
                                   variable_names_.end());
    for (int i = 0; i < input_variables.size(); i++) {
      request.add_input_variable(input_variables[i]);
    }

    InteractiveInterpreter::ParseStatement(code, input_variables,
                                           request.mutable_module(),
                                           request.mutable_assign_to());
    if (request.assign_to().empty()) {
      request.clear_assign_to();
    }

    for (int i = 0; i < request.module().imports_size(); i++) {
      MaybeImport(request.module().imports(i));
    }

    request.set_context_id(context_id_);

    if (verbose_) {
      request.set_debug_mode(true);
    }

    double start = CurrentTime();
    stub_->Evaluate(rpc.get(), &request, &response, NULL);
    double time = CurrentTime() - start;
    cout << "time: " << time << endl;

    if (rpc->Failed()) {
      GOOGLE_LOG(ERROR) << rpc->ErrorText();
      return "";
    }

    if (request.has_assign_to()) {
      variable_names_.insert(request.assign_to());
    }

    if (response.has_debug_info()) {
      GOOGLE_LOG(INFO)
          << "==============================================================\n"
          << response.debug_info()
          << "==============================================================\n";
    }

    return response.result();
  }

  void MaybeImport(const string& filename) {
    if (!HasSuffixString(filename, ".evlan")) return;
    if (!File::Exists(filename)) return;

    // Has the file changed since last import?
    FileStat stat;
    GOOGLE_CHECK(File::Stat(filename, &stat));
    if (imported_files_[filename] == stat.mtime) return;

    // TODO(kenton):  Detect cycles (and raise an error)?

    scoped_ptr<google::protobuf::RpcController> rpc(rpc_system_.NewController());
    EvaluatorRequest request;
    EvaluatorResponse response;

    string code;
    File::ReadFileToStringOrDie(filename, &code);
    parser::Parse(code, request.mutable_module());

    for (int i = 0; i < request.module().imports_size(); i++) {
      MaybeImport(request.module().imports(i));
    }

    request.set_context_id(context_id_);
    request.set_map_as_import(filename);

    if (verbose_) {
      request.set_debug_mode(true);
    }

    stub_->Evaluate(rpc.get(), &request, &response, NULL);

    if (rpc->Failed()) {
      GOOGLE_LOG(ERROR) << rpc->ErrorText();
      return;
    }

    imported_files_[filename] = stat.mtime;

    if (response.has_debug_info()) {
      GOOGLE_LOG(INFO)
          << "==============================================================\n"
          << response.debug_info()
          << "==============================================================\n";
    }
  }

  void Loop() {
    string code;
    while (InteractiveInterpreter::ReadStatementFromStdin(&code)) {
      string result = Evaluate(code);
      if (!result.empty()) {
        cout << result << endl;
      }
    }
  }

 private:
  FakeRpcSystem rpc_system_;
  scoped_ptr<google::protobuf::RpcChannel> channel_;
  scoped_ptr<EvaluatorService> const stub_;
  uint64 const context_id_;
  bool const verbose_;
  unordered_set<string> variable_names_;
  unordered_map<string, time_t> imported_files_;
};

}  // namespace
}  // namespace evlan

int main(int argc, char* argv[]) {
  ParseFlags("USAGE", &argc, &argv, true);
  GOOGLE_CHECK_EQ(argc, 1) << "Unrecognized arg: " << argv[1];
  GOOGLE_CHECK(!FLAGS_server.empty()) << "--server is required";

  if (isatty(STDIN_FILENO)) {
    std::cout <<
      "Evlan Interactive Interpreter Client\n"
      "Type an expression to be evaluated.  End each expression with a "
        "blank line.\n"
      "Use Ctrl-D (EOF) to quit.  For command history, run using rlwrap, "
        "e.g.:\n"
      "  $ sudo apt-get install rlwrap\n"
      "  $ rlwrap " << argv[0] << std::endl;
  }

  evlan::EvaluatorClient client(FLAGS_server, FLAGS_verbose);
  client.Loop();
}
