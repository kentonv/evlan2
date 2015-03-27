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
// This file allows //evlan/api:unittest to be used as if it were an
// evlan_library.  Most evlan_test rules should list it among their deps.
//
// Evlan tests may be declared using the evlan_test build rule, e.g.:
//
//   subinclude("//evlan/api:build_defs")  # Needed to use evlan_test.
//
//   evlan_test(name = "foo_test",
//              size = "small",
//              library_under_test = ":foo",
//              srcs = [ "foo_test.evlan", "bar_test.evlan" ],
//              deps = [ "//evlan/api:unittest" ])
//
// The library_under_test parameter indicates the evlan_library which the test
// is testing.  All of that library's modules will be directly importable
// by the test (whereas if the library were simply listed in deps, only its
// public interface would be importable).  Additionally, these modules will
// be parsed at runtime rather than compile-time, so that you can rapidly
// iterate on them without having to re-compile the test for every change.
// This parameter is optional; you can omit it for tests that do not test any
// particular library.
//
// Each of the source files must evaluate to an Evlan test suite, which in turn
// contains a set of cases.  The unittest library may be used to construct test
// suites.  Example:
//
//   unittest.suite("Arithmetic", cases) where
//     unittest = import "//evlan/api:unittest"
//     cases = array of
//       unittest.case("Addition", 1 + 1 == 2)
//       unittest.case("Subtraction", 5 - 3 == 2)
//       unittest.case("Multiplication", 5 * 2 == 10)
//       unittest.case("Division", 12 / 4 == 3)
//       unittest.benchmark("OnePlusOne", f => f(1) + f(1))
//
// Each test case combines a name with a boolean expression.  If the expression
// evaluates true, the test passes.  Otherwise, it fails.
//
// Note that test suites are also cases, so you can nest suites within other
// suites.
//
// You can also add benchmarks to your test.  In the above example, we've
// defined the benchmark "Arithmetic.OnePlusOne" which tests the speed of
// adding two numbers.  Note the benchmark function looks a little weird:
//   f => f(1) + f(1)
// The benchmarking framework calls this repeatedly and reports the average
// time taken to evaluate the function.  The parameter -- f -- is a special
// form of the identity function which cannot be inlined.  This prevents the
// Evlan optimizer from optimizing the entire function body down to a constant,
// which would otherwise defeat the purpose of the benchmark.  Generally you
// should wrap all of the inputs to the computation which you want to measure
// with calls to f, so that the optimizer thinks that the computation cannot
// be performed until the benchmark function is actually called.
//
// To run benchmarks, run the test with the flag --benchmarks=pattern, where
// "pattern" is a regular expression matching benchmark names.  Use
// --benchmarks=all (or --benchmarks=.) to run all benchmarks.

#ifndef EVLAN_API_UNITTEST_H__
#define EVLAN_API_UNITTEST_H__

#include "evlan/public/test_runner.h"

inline evlan::Library* Evlan_Api_Unittest() {
  return evlan::TestRunner::GetUnittestLibrary();
}

#endif  // EVLAN_API_UNITTEST_H__
