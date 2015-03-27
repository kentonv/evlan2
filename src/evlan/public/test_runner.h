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
// This class is used automatically by the evlan_test build rule.  Users should
// not attempt to use it directly.  See evlan/api/unittest.h for more
// information on how to write tests.
//
// evlan_test actually generates a cc_test with a small main() function that
// uses evlan::TestRunner, usually looking something like this:
//
//   int main(int argc, char* argv[]) {
//     evlan::TestRunner runner("my_package", argc, argv);
//     runner.SetLibraryUnderTest(MyPackage_Foo());
//     runner.AddLibrary(Evlan_Api_Unittest());
//     runner.AddTestFile("my_package/foo_test.evlan");
//     runner.AddTestFile("my_package/bar_test.evlan");
//     return runner.RunMain();
//   }

#ifndef EVLAN_PUBLIC_TEST_RUNNER_H__
#define EVLAN_PUBLIC_TEST_RUNNER_H__

#include "evlan/public/import_resolver.h"
#include "evlan/public/evaluator.h"
#include "evlan/public/cpp_glue.h"
#include "evlan/public/library.h"
#include "evlan/stringpiece.h"

namespace evlan {

class TestRunner {
 public:
  TestRunner();
  TestRunner(int argc, char* argv[]);

  ~TestRunner();

  static Library* GetUnittestLibrary();

  // -----------------------------------------------------------------
  // Setting up libraries

  // Adds a library which is to be used by the test code.  It will be
  // importable within the test using its label.
  void AddLibrary(Library* library);

  // -----------------------------------------------------------------
  // Adding test suites

  // Evaluate the given Evlan source file and add the resulting test suite to
  // the list of tests to run.
  void AddTestFile(StringPiece filename);

  // Evaluate the given Evlan code and add the resulting test suite to the
  // list of tests to run.  This function mostly exists for test_runner_test.cc.
  void AddTestCode(StringPiece code);

  // Add the given test suite to the list of tests to run.  Does not take
  // ownership.
  void AddTestValue(EvlanValue* value);

  // Load the given library and interpret it as a test, adding it to the list of
  // test suites to run.
  void AddTestLibrary(Library* library);

  // -----------------------------------------------------------------
  // Running

  struct FailureInfo {
    string test_name;
    string failure_message;
  };

  // Run all tests.  Returns the number of test cases (leaves) evaluated.
  // Failed tests are added to *failures.  This function mostly exists for
  // test_runner_test.cc.
  int Run(vector<FailureInfo>* failures);

  // Run all benchmarks whose names match the given regular expression.
  void RunBenchmarks(StringPiece pattern);

  // For running from a main() function.  Prints results to stdout, and returns
  // an exit code appropriate for returning from main().  If --benchmarks
  // was passed, benchmarks will be run instead of tests.
  int RunMain();

 private:
  class TestCase;
  class TestModule;
  class UnittestLibrary;

  EVLAN_EXPORT_FRIEND(TestCase);
  EVLAN_EXPORT_FRIEND(TestModule);

  static TestModule kTestModule;

  EvlanEvaluator evaluator_;
  LibraryLoader library_loader_;
  MappedImportResolver import_resolver_;
  string benchmark_pattern_;

  vector<const TestCase*> suites_;

  // Used during benchmarks.
  scoped_ptr<EvlanValue> identity_function_;

  void RunBenchmark(EvlanValue* function, int iters);
};

}  // namespace evlan

#endif  // EVLAN_PUBLIC_TEST_RUNNER_H__
