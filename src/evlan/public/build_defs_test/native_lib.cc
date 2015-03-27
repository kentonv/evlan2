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

#include "evlan/public/build_defs_test/native_lib.h"

#include "evlan/public/evaluator.h"
#include "evlan/public/library.h"
#include "evlan/stringpiece.h"
#include <stdlib.h>

namespace evlan {
namespace {

class NativeLib : public Library {
 public:
  virtual ~NativeLib() {}

  // implements Library ----------------------------------------------
  virtual StringPiece GetLabel() { return "//evlan/api:unittest"; }

  virtual void GetDependencies(vector<Library*>* output) {}

  virtual EvlanValue* Load(EvlanEvaluator* evaluator,
                           const vector<EvlanValue*>& dependencies) {
    // This allows qux_test_test to force qux_test to fail, verifying that it
    // really is executing the test code and producing appropriate output on
    // failure.
    if (getenv("INJECT_TEST_FAILURE") != NULL) {
      return evaluator->Evaluate("{name = @nativeLib, shouldFail = true}");
    } else {
      return evaluator->Evaluate("{name = @nativeLib, shouldFail = false}");
    }
  }
};

}  // namespace
}  // namespace evlan

evlan::Library* Evlan_Public_BuildDefsTest_NativeLib() {
  static evlan::NativeLib singleton;
  return &singleton;
}
