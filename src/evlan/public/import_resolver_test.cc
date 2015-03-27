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

#include "evlan/public/import_resolver.h"

#include "evlan/public/evaluator.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include "evlan/stringpiece.h"
#include <gtest/gtest.h>

namespace evlan {
namespace {

TEST(NullImportResolverTest, ReturnsNull) {
  NullImportResolver resolver;
  EXPECT_TRUE(resolver.Import(NULL, "foo") == NULL);
}

class MappedImportResolverTest : public testing::Test {
 protected:
  MappedImportResolverTest()
    : foo_(evaluator_.NewAtom("foo")),
      bar_(evaluator_.NewAtom("bar")),
      baz_(evaluator_.NewAtom("baz")),
      qux_(evaluator_.NewAtom("qux")) {}

  string TryImport(const StringPiece& name) {
    scoped_ptr<EvlanValue> value(resolver_.Import(&evaluator_, name));
    StringPiece atom_name;
    if (value == NULL) {
      return "NULL";
    } else if (value->GetAtomValue(&atom_name)) {
      return atom_name.as_string();
    } else {
      return "Unexpected value: " + value->DebugString();
    }
  }

  MappedImportResolver resolver_;
  MappedImportResolver sub_resolver_;
  MappedImportResolver sub_resolver2_;
  EvlanEvaluator evaluator_;
  scoped_ptr<EvlanValue> foo_;
  scoped_ptr<EvlanValue> bar_;
  scoped_ptr<EvlanValue> baz_;
  scoped_ptr<EvlanValue> qux_;
};

TEST_F(MappedImportResolverTest, Map) {
  resolver_.Map("abc", foo_.get());

  EXPECT_EQ("foo", TryImport("abc"));
  EXPECT_EQ("NULL", TryImport("abc/def"));
  EXPECT_EQ("NULL", TryImport("ab"));
}

TEST_F(MappedImportResolverTest, MapMultiple) {
  resolver_.Map("abc", foo_.get());
  resolver_.Map("abc/def", bar_.get());
  resolver_.Map("ghi", baz_.get());

  EXPECT_EQ("foo", TryImport("abc"));
  EXPECT_EQ("bar", TryImport("abc/def"));
  EXPECT_EQ("baz", TryImport("ghi"));
  EXPECT_EQ("NULL", TryImport("jkl"));
}

TEST_F(MappedImportResolverTest, MapPrefix) {
  resolver_.Map("abc", foo_.get());
  resolver_.MapPrefix("abc/", &sub_resolver_);
  sub_resolver_.Map("def", bar_.get());

  EXPECT_EQ("foo", TryImport("abc"));
  EXPECT_EQ("bar", TryImport("abc/def"));
  EXPECT_EQ("NULL", TryImport("abc/ghi"));
}

TEST_F(MappedImportResolverTest, MapPrefixMultiple) {
  resolver_.MapPrefix("abc/", &sub_resolver_);
  resolver_.MapPrefix("def/", &sub_resolver2_);
  sub_resolver_.Map("ghi", foo_.get());
  sub_resolver2_.Map("ghi", bar_.get());

  EXPECT_EQ("foo", TryImport("abc/ghi"));
  EXPECT_EQ("bar", TryImport("def/ghi"));
}

TEST_F(MappedImportResolverTest, MapPrefixMostSpecific) {
  resolver_.MapPrefix("abc/", &sub_resolver_);
  resolver_.MapPrefix("abc/def/", &sub_resolver2_);
  sub_resolver_.Map("def/ghi", foo_.get());
  sub_resolver_.Map("def/jkl", bar_.get());
  sub_resolver_.Map("jkl", baz_.get());
  sub_resolver2_.Map("ghi", qux_.get());

  EXPECT_EQ("qux", TryImport("abc/def/ghi"));
  EXPECT_EQ("NULL", TryImport("abc/def/jkl"));
  EXPECT_EQ("baz", TryImport("abc/jkl"));
}

// ===================================================================

TEST(ProtoImportResolverTest, ImportProto) {
  ProtoImportResolver resolver(
    google::protobuf::DescriptorPool::generated_pool(),
    google::protobuf::MessageFactory::generated_factory(),
    NULL);
  EvlanEvaluator evaluator;

  scoped_ptr<EvlanValue> proto(
    resolver.Import(&evaluator, "no/such/proto.proto"));
  EXPECT_TRUE(proto == NULL);

  proto.reset(resolver.Import(&evaluator, "evlan/proto/module.proto"));
  ASSERT_TRUE(proto != NULL);

  // We can't rely on proto->DebugString() returning anything in particular,
  // so we need to actually use the file somehow.
  scoped_ptr<EvlanValue> code(evaluator.Evaluate(
    "proto => proto.Module.defaultInstance.codeTree.size"));
  scoped_ptr<EvlanValue> result(code->Call(proto.get()));
  EXPECT_EQ("0", result->DebugString());
}

}  // namespace
}  // namespace evlan
