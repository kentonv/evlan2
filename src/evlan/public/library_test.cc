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
// See also build_defs_test for higher-level testing of Evlan libraries and
// the evlan_library build rule.

#include "evlan/public/library.h"

#include "evlan/parser/parser.h"
#include "evlan/proto/module.pb.h"
#include "evlan/public/evaluator.h"
#include <google/protobuf/io/zero_copy_stream.h>
#include "evlan/stringpiece.h"
#include "evlan/strutil.h"
#include <gtest/gtest.h>
#include "evlan/stl-util.h"

namespace evlan {
namespace {

// ===================================================================
// LibraryLoader

class MockLibrary : public Library {
 public:
  MockLibrary(StringPiece label, Library* dep1 = NULL, Library* dep2 = NULL)
    : label_(label), dep1_(dep1), dep2_(dep2), load_called_(false) {}
  virtual ~MockLibrary() {}

  // implements Library ----------------------------------------------
  virtual StringPiece GetLabel() { return label_; }

  virtual void GetDependencies(vector<Library*>* output) {
    if (dep1_ != NULL) output->push_back(dep1_);
    if (dep2_ != NULL) output->push_back(dep2_);
  }

  virtual EvlanValue* Load(EvlanEvaluator* evaluator,
                           const vector<EvlanValue*>& dependencies) {
    EXPECT_FALSE(load_called_)
      << "Library \"" << label_ << "\" loaded twice.";
    load_called_ = true;

    // Return a tuple containing the library label (as an atom) and an array
    // of dependencies.
    vector<EvlanValue*> elements;
    elements.push_back(evaluator->NewAtom(label_));
    elements.push_back(evaluator->NewArray(dependencies));
    EvlanValue* result = evaluator->NewTuple(elements);
    STLDeleteElements(&elements);
    return result;
  }

 private:
  StringPiece label_;
  Library* dep1_;
  Library* dep2_;
  bool load_called_;
};

TEST(LibraryLoaderTest, DiamondDependencies) {
  MockLibrary foo("foo");
  MockLibrary bar("bar", &foo);
  MockLibrary baz("baz", &foo);
  MockLibrary qux("qux", &bar, &baz);
  EvlanEvaluator evaluator;
  LibraryLoader loader(&evaluator);

  scoped_ptr<EvlanValue> value(loader.Load(&qux));
  EXPECT_EQ("(@qux, {(@bar, {(@foo, {})}), (@baz, {(@foo, {})})})",
            value->DebugString(6));

  value.reset(loader.Load(&foo));
  EXPECT_EQ("(@foo, {})", value->DebugString());
}

}  // namespace
}  // namespace evlan
