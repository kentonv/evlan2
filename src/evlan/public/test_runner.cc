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

#include "evlan/public/test_runner.h"

#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <memory>

#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/public/evaluator.h"
#include "evlan/public/import_resolver.h"
#include <google/protobuf/testing/file.h>
#include <google/protobuf/testing/googletest.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include "evlan/strutil.h"
#include "evlan/stringpiece.h"
#include "evlan/stl-util.h"

// Number of threads in which to run benchmarks concurrently.
int FLAGS_benchmark_threads = 1;

namespace evlan {

// ===================================================================
// Implementation of the evlan/api/unittest library.
//
// The library is implemented in C++ to make it easy for TestRunner to
// read the results.

// The type returned by unittest.suite() and unittest.case().
class TestRunner::TestCase {
 public:
  // Constructs a test suite.
  TestCase(StringPiece name, vector<const TestCase*> children)
      : state_(new State) {
    state_->name_ = name.as_string();
    state_->passed_ = true;
    for (int i = 0; i < children.size(); i++) {
      if (!children[i]->passed()) {
        state_->passed_ = false;
        if (state_->failure_message_.empty()) {
          state_->failure_message_ = "Child cases failed: ";
        } else {
          state_->failure_message_ += ", ";
        }
        state_->failure_message_ += children[i]->name();
      }

      // We need to make a copy because we can't prevent the garbage collector
      // from deleting the TestCase.  It's only a shallow copy, though, due
      // to the shared_ptr.
      state_->children_.push_back(new TestCase(*children[i]));
    }
  }

  // Constructs a single ("leaf") test case.
  TestCase(StringPiece name, EvlanValue* condition)
      : state_(new State) {
    state_->name_ = name.as_string();
    if (condition->GetBooleanValue(&state_->passed_)) {
      if (!state_->passed_) {
        state_->failure_message_ = "Condition evaluated false.";
      }
    } else {
      state_->passed_ = false;
      StringPiece type_error;
      if (condition->GetTypeErrorDescription(&type_error)) {
        state_->failure_message_ = "Type error: ";
        state_->failure_message_.append(type_error.data(), type_error.size());
      } else {
        state_->failure_message_ = "Result was not boolean: ";
        state_->failure_message_ += condition->DebugString();
      }
    }
  }

  enum Benchmark { IS_BENCHMARK };

  // Constructs a benchmark case.
  TestCase(StringPiece name, EvlanValue* condition, Benchmark)
      : state_(new State) {
    state_->name_ = name.as_string();
    state_->passed_ = true;
    state_->benchmark_function_.reset(condition->Clone());
  }

  // Returns number of tests run.  Names of failing tests are added to
  // *failures.
  int CollectResults(const string& prefix,
                     vector<FailureInfo>* failures) const {
    if (state_->benchmark_function_ != NULL) {
      // Benchmarks don't count as test cases.
      return 0;
    } else if (state_->children_.empty()) {
      if (!state_->passed_) {
        FailureInfo failure;
        failure.test_name = prefix + state_->name_;
        failure.failure_message = state_->failure_message_;
        failures->push_back(failure);
      }
      return 1;
    } else {
      int total = 0;
      string sub_prefix = prefix + state_->name_ + '.';
      for (int i = 0; i < state_->children_.size(); i++) {
        total += state_->children_[i]->CollectResults(sub_prefix, failures);
      }
      return total;
    }
  }

  // Get all the benchmarks in this test suite.
  void CollectBenchmarks(
      const string& prefix,
      vector<pair<string, EvlanValue*> >* benchmarks) const {
    if (state_->benchmark_function_ != NULL) {
      benchmarks->push_back(make_pair(
        prefix + state_->name_, state_->benchmark_function_->Clone()));
    } else {
      string sub_prefix = prefix + state_->name_ + '.';
      for (int i = 0; i < state_->children_.size(); i++) {
        state_->children_[i]->CollectBenchmarks(sub_prefix, benchmarks);
      }
    }
  }

  bool passed() const { return state_->passed_; }
  const string& name() const { return state_->name_; }

 private:
  // We store a scoped_ptr to all the state so that copying TestCase objects
  // is cheap, which is useful for some of the code above.
  struct State {
    string name_;
    bool passed_;
    string failure_message_;
    vector<const TestCase*> children_;
    scoped_ptr<EvlanValue> benchmark_function_;

    ~State() {
      STLDeleteElements(&children_);
    }
  };
  shared_ptr<State> state_;
};

EVLAN_EXPORT_TYPE(TestRunner::TestCase) {}

// This is the public interface of the unittest library.  That is, when you
// import the library, the result is an instance of this class.
class TestRunner::TestModule {
 public:
  TestCase* Suite(StringPiece name,
                  const vector<const TestCase*>& children) const {
    return new TestCase(name, children);
  }

  TestCase* Case(StringPiece name, EvlanValue* condition) const {
    return new TestCase(name, condition);
  }

  TestCase* Benchmark(StringPiece name, EvlanValue* function) const {
    return new TestCase(name, function, TestCase::IS_BENCHMARK);
  }

  static TestModule kSingleton;
};

TestRunner::TestModule TestRunner::TestModule::kSingleton;

EVLAN_EXPORT_TYPE(TestRunner::TestModule) {
  EVLAN_EXPORT_METHOD(Suite);
  EVLAN_EXPORT_METHOD(Case);
  EVLAN_EXPORT_METHOD(Benchmark);
}

// The evlan::Library representing the unittest library.  This allows
// evlan_library and evlan_test targets to depend on it.  See also
// evlan/api/unittest.h.
class TestRunner::UnittestLibrary : public Library {
 public:
  virtual ~UnittestLibrary() {}

  // implements Library ----------------------------------------------
  virtual StringPiece GetLabel() { return "evlan/api/unittest"; }

  virtual void GetDependencies(vector<Library*>* output) {}

  virtual EvlanValue* Load(EvlanEvaluator* evaluator,
                           const vector<EvlanValue*>& dependencies) {
    return evaluator->NewCppObjectReference(TestModule::kSingleton);
  }
};

Library* TestRunner::GetUnittestLibrary() {
  static UnittestLibrary singleton;
  return &singleton;
}

// ===================================================================
// TestRunner class itself.

TestRunner::TestRunner()
  : library_loader_(&evaluator_),
    identity_function_(evaluator_.Evaluate("x => x")) {
}

TestRunner::TestRunner(int argc, char* argv[])
  : library_loader_(&evaluator_),
    identity_function_(evaluator_.Evaluate("x => x")) {

  for (int i = 1; i < argc; i++) {
    string arg(argv[i]);
    if (HasPrefixString(arg, "--benchmarks=")) {
      benchmark_pattern_ = StripPrefixString(arg, "--benchmarks=");
    }
  }
}

TestRunner::~TestRunner() {
  STLDeleteElements(&suites_);
}

// -------------------------------------------------------------------

void TestRunner::AddLibrary(Library* library) {
  scoped_ptr<EvlanValue> value(library_loader_.Load(library));
  import_resolver_.Map(library->GetLabel(), value.get());
}

// -------------------------------------------------------------------

void TestRunner::AddTestFile(StringPiece filename) {
  string code;
  google::protobuf::File::ReadFileToStringOrDie(
      google::protobuf::TestSourceDir() + filename.as_string(), &code);
  AddTestCode(code);
}

void TestRunner::AddTestCode(StringPiece code) {
  scoped_ptr<EvlanValue> value(evaluator_.Evaluate(code, &import_resolver_));
  AddTestValue(value.get());
}

void TestRunner::AddTestValue(EvlanValue* value) {
  const TestCase* suite;
  GOOGLE_CHECK(value->GetCppObjectValue(&suite))
    << "Test code did not evaluate to a TestCase: " << value->DebugString();

  // Make a copy in case the garbage collector cleans up the existing object.
  suites_.push_back(new TestCase(*suite));
}

void TestRunner::AddTestLibrary(Library* library) {
  scoped_ptr<EvlanValue> value(library_loader_.Load(library));
  AddTestValue(value.get());
}

// -------------------------------------------------------------------

int TestRunner::Run(vector<FailureInfo>* failures) {
  int total = 0;
  for (int i = 0; i < suites_.size(); i++) {
    total += suites_[i]->CollectResults("", failures);
  }
  return total;
}

void TestRunner::RunBenchmarks(StringPiece pattern) {
  vector<pair<string, EvlanValue*> > benchmarks;
  for (int i = 0; i < suites_.size(); i++) {
    suites_[i]->CollectBenchmarks("", &benchmarks);
  }

  // TODO(kenton):  Benchmarking framework.
  GOOGLE_LOG(FATAL) << "Unimplemeted:  benchmarks";

  STLDeleteValues(&benchmarks);
}

void TestRunner::RunBenchmark(EvlanValue* function, int iters) {
  scoped_ptr<EvlanEvaluator> forked_evaluator(evaluator_.Fork());
  scoped_ptr<EvlanValue> cloned_function(
      forked_evaluator->CloneValue(function));

  scoped_ptr<EvlanValue> result;

  // Do one iteration first to warm up the memory manager, then record the
  // "initial" memory stats.  If we don't do this, then even tests which
  // withdraw all the memory they allocate will be charged with however
  // much memory is allocated initially.
  //
  // This means that we technically under-count memory usage, but at least
  // it is clear when a test is avoiding GC entirely.
  result.reset(cloned_function->Call(identity_function_.get()));
  EvlanEvaluator::MemoryStats original_memory_stats =
      forked_evaluator->GetMemoryStats();

  // Now do the rest of the iterations.
  for (int i = 1; i < iters; i++) {
    result.reset(cloned_function->Call(identity_function_.get()));
  }

  StringPiece error;
  if (result->GetTypeErrorDescription(&error)) {
    GOOGLE_LOG(FATAL) << "Benchmark function returned type error: " << error.as_string();
  }

  EvlanEvaluator::MemoryStats final_memory_stats =
      forked_evaluator->GetMemoryStats();
  size_t memory_difference = final_memory_stats.total_bytes -
                             original_memory_stats.total_bytes;
  // SetBenchmarkLabel("mem:" + SimpleItoa(memory_difference / (iters - 1)));
}

int TestRunner::RunMain() {
  if (benchmark_pattern_.empty()) {
    vector<FailureInfo> failures;
    int total = Run(&failures);
    int passed = total - failures.size();

    const char* green = "";
    const char* red = "";
    const char* normal = "";

    if (isatty(STDOUT_FILENO)) {
      // When run interactively, highlight the "PASSED" and "FAILED" messages
      // in green and red, respectively.  The following strings are standard
      // terminal escape sequences to change the text color.
      green  = "\033[32m";
      red    = "\033[31m";
      normal = "\033[0m";
    }

    printf("%s[ PASSED ]%s %d of %d tests\n", green, normal, passed, total);

    if (failures.empty()) {
      return 0;
    } else {
      for (int i = 0; i < failures.size(); i++) {
        printf("%s[ FAILED ]%s %s: %s\n", red, normal,
               failures[i].test_name.c_str(),
               failures[i].failure_message.c_str());
      }
      return 1;
    }
  } else {
    RunBenchmarks(benchmark_pattern_);
    return 0;
  }
}

}  // namespace evlan
