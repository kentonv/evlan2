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
// Compiles Evlan source code into module files, and also generates code needed
// to compile evlan_library and evlan_test build rules.

#include <unistd.h>
#include <functional>

#include "evlan/common.h"
#include "evlan/proto/module.pb.h"
#include "evlan/parser/parser.h"
#include <google/protobuf/testing/file.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include "evlan/strutil.h"
#include "evlan/stl-util.h"
#include "evlan/stringpiece.h"

namespace evlan {

using google::protobuf::File;
using namespace std::placeholders;

// ===================================================================
// Helpers

// Convert a string containing underscores to its capitalized-camel-case
// equivalent.  E.g. "foo_bar" becomes "FooBar".
string ToCamelCase(const string& name) {
  string result;

  bool capitalize_next_letter = true;
  for (int i = 0; i < name.size(); i++) {
    if (!isalnum(name[i])) {
      // Presumably this is an underscore.  Don't use it in the Evlan name,
      // but capitalize the next letter.
      capitalize_next_letter = true;
    } else if (capitalize_next_letter) {
      result.push_back(toupper(name[i]));
      capitalize_next_letter = false;
    } else {
      result.push_back(name[i]);
    }
  }

  return result;
}

// Remove all leading slashes from a string.  E.g. "//evlan" becomes "evlan".
string RemoveLeadingSlashes(const string& input) {
  int pos = 0;
  while (pos < input.size() && input[pos] == '/') ++pos;
  return input.substr(pos);
}

// Get the function name which returns the evlan::Library representing the
// library with the given label.  E.g. "//evlan/api:unittest" becomes
// "Evlan_Api_Unittest".
string LabelToFunctionName(const string& label) {
  vector<string> path;
  SplitStringUsing(label, "/", &path);

  string result;
  for (int i = 0; i < path.size(); i++) {
    if (!result.empty()) result += "_";
    result += ToCamelCase(path[i]);
  }

  return result;
}

// Get the include guard for this library's header file.
string IncludeGuard(const string& label) {
  string result = label + ".h";

  for (int i = 0; i < result.size(); i++) {
    if (isalnum(result[i])) {
      result[i] = toupper(result[i]);
    } else {
      result[i] = '_';
    }
  }
  result += '_';

  return result;
}

// For now, the parser only reports errors via GOOGLE_LOG(ERROR).  Let's detect this
// with a custom LogSink.
// TODO(kenton):  Make the parser not suck at error reporting.
class ErrorDetector {
 public:
  ErrorDetector()
      : had_errors_(false) {
    GOOGLE_CHECK(current_detector_ == NULL);
    current_detector_ = this;
    old_log_handler_ = google::protobuf::SetLogHandler(&HandleLog);
  }
  ~ErrorDetector() {
    google::protobuf::SetLogHandler(old_log_handler_);
    current_detector_ = NULL;
  }

  bool had_errors_;

 private:
  static ErrorDetector* current_detector_;
  google::protobuf::LogHandler* old_log_handler_;

  static void HandleLog(google::protobuf::LogLevel level, const char* filename,
                        int line, const string& message) {
    if (level >= google::protobuf::LOGLEVEL_ERROR) {
      current_detector_->had_errors_ = true;
    }
  }
};

ErrorDetector* ErrorDetector::current_detector_ = NULL;

// ===================================================================
// Output generation

// Generate the .h file for an evlan_library rule.  See library.h for
// comments on what this file should look like.
void WriteHeaderFile(const string& label,
                     google::protobuf::io::Printer* printer) {
  printer->Print(
    "#ifndef $guard$\n"
    "#define $guard$\n"
    "\n"
    "namespace evlan { class Library; }\n"
    "\n"
    "evlan::Library* $func$();\n"
    "\n"
    "#endif  // $guard$\n",
    "guard", IncludeGuard(label),
    "func", LabelToFunctionName(label));
}

// Generate the .cc file for an evlan_library rule.  See library.h for
// comments on what this file should look like.
void WriteCcFile(const string& label,
                 const Module& module,
                 google::protobuf::io::Printer* printer) {
  vector<string> deps;
  for (int i = 0; i < module.imports_size(); i++) {
    if (module.imports(i) != "builtin") {
      deps.push_back(module.imports(i));
    }
  }

  // Generate includes.
  printer->Print(
    "#include \"$label$.h\"\n"
    "#include \"evlan/public/library.h\"\n"
    "#include \"evlan/public/evaluator.h\"\n"
    "#include \"evlan/proto/module.pb.h\"\n",
    "label", label);
  if (HasSuffixString(label, "_test")) {
    printer->Print(
      "#include \"evlan/public/test_runner.h\"\n");
  }

  for (auto& dep : deps) {
    printer->Print(
      "#include \"$label$.h\"\n",
      "label", dep);
  }

  // Write the raw module data.
  printer->Print(
    "\n"
    "namespace {\n"
    "\n"
    "using namespace evlan;\n"
    "\n"
    "static const char kModuleData[] =");

  string module_data = module.SerializeAsString();
  for (int i = 0; i < module_data.size(); i += 40) {
    printer->Print(
      "\n    \"$line$\"",
      "line", strings::CEscape(module_data.substr(i, 40)));
  }

  // Start LibraryImpl class.
  printer->Print(";\n"
    "\n"
    "class LibraryImpl : public evlan::Library {\n"
    " public:\n"
    "  virtual ~LibraryImpl() {}\n"
    "  virtual StringPiece GetLabel() {\n"
    "    return \"$label$\";\n"
    "  }\n"
    "  \n",
    "label", label);

  printer->Indent();

  // -----------------------------------------------------------------
  // LibraryImpl::GetDependencies()

  printer->Print(
    "virtual void GetDependencies(vector<Library*>* output) {\n");

  for (auto& dep : deps) {
    printer->Print(
      "  output->push_back($dep$());\n",
      "dep", LabelToFunctionName(dep));
  }

  printer->Print(
    "}\n"
    "\n");

  // -----------------------------------------------------------------
  // LibraryImpl::Load()

  printer->Print(
    "virtual evlan::EvlanValue* Load(\n"
    "    evlan::EvlanEvaluator* evaluator,\n"
    "    const vector<evlan::EvlanValue*>& dependencies) {\n"
    "  evlan::MappedImportResolver resolver;\n");

  // Map each of the dependencies into the resolver.
  for (int i = 0; i < deps.size(); i++) {
    printer->Print(
      "  resolver.Map(\"$dep$\", dependencies[$i$]);\n",
      "dep", deps[i],
      "i", SimpleItoa(i));
  }

  // Now map the module.
  printer->Print(
    "  ::evlan::Module module;\n"
    "  GOOGLE_CHECK(module.ParseFromArray(kModuleData, sizeof(kModuleData) - 1));\n"
    "  return evaluator->Evaluate(module, &resolver);\n"
    "}\n"
    "\n");

  printer->Outdent();

  // -----------------------------------------------------------------
  // Footer

  printer->Print(
    "};\n"
    "\n"
    "}  // namespace\n"
    "\n"
    "evlan::Library* $function$() {\n"
    "  static LibraryImpl singleton;\n"
    "  return &singleton;\n"
    "}\n",
    "function", LabelToFunctionName(label));

  if (label == "evlan/api/builtinSupport") {
    // Hack:  Allow direct access to code from evaluator.cc.
    printer->Print(
      "\n"
      "namespace evlan {\n"
      "const StringPiece GetBuiltinSupportBytecode() {\n"
      "  return StringPiece(kModuleData, sizeof(kModuleData) - 1);\n"
      "}\n"
      "}  // namespace evlan\n");
  }

  // -----------------------------------------------------------------
  // Test

  if (HasSuffixString(label, "_test")) {
    printer->Print(
        "\n"
        "int main(int argc, char* argv[]) {\n"
        "  ::evlan::TestRunner runner(argc, argv);\n"
        "  runner.AddTestLibrary($function$());\n"
        "  return runner.RunMain();\n"
        "}\n",
        "function", LabelToFunctionName(label));
  }
}

// Write an Evlan Module file for the library (just a protocol buffer as
// defined in evlan/proto/module.proto).
void WriteModuleFile(const string& filename, const Module& module) {
  string data;
  module.SerializeToString(&data);
  File::WriteStringToFileOrDie(data, filename);
}

// Generate a text file by calling the given callback, storing the contents
// to the given file name.
void GenerateTextFile(const string& filename,
                      function<void(google::protobuf::io::Printer*)> write_file) {
  if (filename.empty()) return;

  string contents;

  {
    google::protobuf::io::StringOutputStream output(&contents);
    google::protobuf::io::Printer printer(&output, '$');
    write_file(&printer);
  }

  if (filename.empty()) {
    write(STDOUT_FILENO, contents.data(), contents.size());
  } else {
    File::WriteStringToFileOrDie(contents, filename);
  }
}

void Compile(const string& filename) {
  string code;
  if (!File::ReadFileToString(filename, &code)) {
    fprintf(stderr, "Could not read input file: %s\n", filename.c_str());
    exit(1);
  }

  Module module;
  {
    ErrorDetector error_detector;
    parser::Parse(code, &module);
    if (error_detector.had_errors_) {
      fprintf(stderr, "Compile failed due to parse errors.\n");
      exit(1);
    }
  }

  string module_name = StripSuffixString(filename, ".evlan");

  WriteModuleFile(module_name + ".evmod", module);

  GenerateTextFile(module_name + ".h",
      std::bind(&WriteHeaderFile, module_name, _1));
  GenerateTextFile(module_name + ".cc",
      std::bind(&WriteCcFile, module_name, module, _1));
}

// ===================================================================
// main()

int EvlanCMain(int argc, char* argv[]) {
  for (int i = 1; i < argc; i++) {
    Compile(argv[i++]);
  }

  return 0;
}

}  // namespace evlan

int main(int argc, char* argv[]) {
  return evlan::EvlanCMain(argc, argv);
}
