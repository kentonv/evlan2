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
// See also build_defs_test for higher-level testing of Evlan unit tests and
// the evlan_test build rule.

#include "evlan/public/test_runner.h"
#include "evlan/stringpiece.h"
#include "evlan/strutil.h"
#include <google/protobuf/testing/googletest.h>
#include <gtest/gtest.h>
#include <stdio.h>

namespace evlan {
namespace {

using google::protobuf::CaptureTestStdout;
using google::protobuf::GetCapturedTestStdout;

class TestRunnerTest : public testing::Test {
 protected:
  TestRunnerTest() {
    runner_.AddLibrary(TestRunner::GetUnittestLibrary());
  }

  TestRunner runner_;
};

TEST_F(TestRunnerTest, Simple) {
  runner_.AddTestCode(
    "unittest.suite(\"Foo\", cases) where\n"
    "  unittest = import \"evlan/api/unittest\"\n"
    "  cases = array of\n"
    "    unittest.suite(\"Bar\", cases) where\n"
    "      cases = array of\n"
    "        unittest.case(\"Baz\", true)\n"
    "        unittest.case(\"Qux\", false)\n"
    "    unittest.case(\"Quux\", @a)\n"
    "    unittest.case(\"Corge\", true)\n"
    "    unittest.case(\"Grault\", 123 == @b)\n");

  vector<TestRunner::FailureInfo> failures;
  EXPECT_EQ(5, runner_.Run(&failures));
  ASSERT_EQ(3, failures.size());
  EXPECT_EQ("Foo.Bar.Qux", failures[0].test_name);
  EXPECT_EQ("Condition evaluated false.", failures[0].failure_message);
  EXPECT_EQ("Foo.Quux", failures[1].test_name);
  EXPECT_EQ("Result was not boolean: @a", failures[1].failure_message);
  EXPECT_EQ("Foo.Grault", failures[2].test_name);
  EXPECT_PRED2(HasPrefixString, failures[2].failure_message, "Type error: ");
}

TEST_F(TestRunnerTest, MultipleTests) {
  runner_.AddTestCode("import \"evlan/api/unittest\".case(\"Foo\", true)");
  runner_.AddTestCode("import \"evlan/api/unittest\".case(\"Bar\", false)");
  runner_.AddTestCode("import \"evlan/api/unittest\".case(\"Baz\", false)");
  runner_.AddTestCode("import \"evlan/api/unittest\".case(\"Qux\", true)");

  vector<TestRunner::FailureInfo> failures;
  EXPECT_EQ(4, runner_.Run(&failures));
  ASSERT_EQ(2, failures.size());
  EXPECT_EQ("Bar", failures[0].test_name);
  EXPECT_EQ("Baz", failures[1].test_name);
}

TEST_F(TestRunnerTest, RunMainPassed) {
  runner_.AddTestCode("import \"evlan/api/unittest\".case(\"Foo\", true)");
  runner_.AddTestCode("import \"evlan/api/unittest\".case(\"Bar\", true)");

  fflush(stdout);
  CaptureTestStdout();
  int exit_code = runner_.RunMain();
  fflush(stdout);
  string output = GetCapturedTestStdout();
  EXPECT_NE("", output);

  EXPECT_EQ(0, exit_code);

  EXPECT_NE(string::npos, output.find("[ PASSED ] 2 of 2 tests"));
  EXPECT_EQ(string::npos, output.find("FAILED"));
}

TEST_F(TestRunnerTest, RunMainFailed) {
  runner_.AddTestCode("import \"evlan/api/unittest\".case(\"Foo\", true)");
  runner_.AddTestCode("import \"evlan/api/unittest\".case(\"Bar\", false)");

  fflush(stdout);
  CaptureTestStdout();
  int exit_code = runner_.RunMain();
  fflush(stdout);
  string output = GetCapturedTestStdout();
  EXPECT_NE("", output);

  EXPECT_NE(0, exit_code);

  EXPECT_NE(string::npos, output.find("[ PASSED ] 1 of 2 tests"));
  EXPECT_NE(string::npos, output.find("[ FAILED ] Bar: Condition evaluated false."));
}

#if 0
// TODO(kenton):  Implement benchmark framework.

TEST_F(TestRunnerTest, Benchmarks) {
  // Test benchmarks.

  // This library provides a function which, when called, records the call
  // to call_counter_.  We use this to verify that the benchmarks are actually
  // executed.
  class MockFunctionLibrary : public Library {
   public:
    virtual StringPiece GetLabel() {
      return "//mock_library";
    }
    virtual void GetDependencies(vector<Library*>* output) {}
    virtual EvlanValue* Load(EvlanEvaluator* evaluator,
                             const vector<EvlanValue*>& dependencies) {
      return evaluator->NewCustomValue(new MockFunctionValue(this));
    }

    class MockFunctionValue : public CustomValue {
     public:
      MockFunctionValue(MockFunctionLibrary* library): library_(library) {}

      virtual EvlanValue* Call(EvlanEvaluator* evaluator,
                               EvlanValue* self,
                               EvlanValue* parameter) {
        ++library_->call_counter_[parameter->DebugString()];
        return evaluator->NewInteger(123);
      }

      MockFunctionLibrary* library_;
    };

    // The key is the DebugString() of the parameter passed to the function,
    // so that different usages can be identified.
    map<string, int> call_counter_;
  };
  MockFunctionLibrary mock_library;
  runner_.AddLibrary(&mock_library);

  runner_.AddTestCode(
    "unittest.benchmark(\"Foo\", f => mockFunc(f(@foo))) where\n"
    "  unittest = import \"evlan/api/unittest\"\n"
    "  mockFunc = import \"//mock_library\"\n");
  runner_.AddTestCode(
    "unittest.suite(\"Bar\", cases) where\n"
    "  unittest = import \"evlan/api/unittest\"\n"
    "  mockFunc = import \"//mock_library\"\n"
    "  cases = array of\n"
    "    unittest.benchmark(\"Baz\", f => mockFunc(f(@baz)))\n"
    "    unittest.case(\"Qux\", true)\n"
    "    unittest.benchmark(\"Corge\", f => mockFunc(f(@corge)))\n");

  // Make sure running tests works and ignores benchmarks.
  vector<TestRunner::FailureInfo> failures;
  EXPECT_EQ(1, runner_.Run(&failures));
  EXPECT_TRUE(failures.empty());
  EXPECT_TRUE(mock_library.call_counter_.empty());

  // Run the benchmarks.
  MockBenchmarkReporter reporter;
  runner_.RunBenchmarks("all", &reporter);

  ASSERT_EQ(3, reporter.names_.size());
  EXPECT_EQ("Foo", reporter.names_[0]);
  EXPECT_EQ("Bar.Baz", reporter.names_[1]);
  EXPECT_EQ("Bar.Corge", reporter.names_[2]);

  EXPECT_EQ(3, mock_library.call_counter_.size());
  EXPECT_EQ(7, mock_library.call_counter_["@foo"]);
  EXPECT_EQ(7, mock_library.call_counter_["@baz"]);
  EXPECT_EQ(7, mock_library.call_counter_["@corge"]);

  mock_library.call_counter_.clear();
  reporter.names_.clear();

  // Try running just benchmarks that match a pattern.
  runner_.RunBenchmarks("Bar\\..*", &reporter);

  ASSERT_EQ(2, reporter.names_.size());
  EXPECT_EQ("Bar.Baz", reporter.names_[0]);
  EXPECT_EQ("Bar.Corge", reporter.names_[1]);

  EXPECT_EQ(2, mock_library.call_counter_.size());
  EXPECT_EQ(7, mock_library.call_counter_["@baz"]);
  EXPECT_EQ(7, mock_library.call_counter_["@corge"]);
}

TEST_F(TestRunnerTest, BenchmarkError) {
  runner_.AddTestCode(
    "import \"evlan/api/unittest\".benchmark(\"Foo\", f => 1 + @a)\n");

  MockBenchmarkReporter reporter;
  EXPECT_DEATH(runner_.RunBenchmarks(".", &reporter),
               "Benchmark function returned type error: ");
}

#endif

}  // namespace
}  // namespace evlan
