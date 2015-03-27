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

#include "evlan/public/evaluator.h"
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/public/import_resolver.h"
#include "evlan/stringpiece.h"
#include <gtest/gtest.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include "evlan/stl-util.h"

namespace evlan {
namespace {

using namespace std::placeholders;

TEST(EvlanEvaluatorTest, BasicTypes) {
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> result;

  bool bool_value = false;
  int int_value = -1;
  double double_value = -1;
  StringPiece string_value;
  const google::protobuf::Message* message_value = NULL;

  // Boolean
  result.reset(evaluator.NewBoolean(true));
  EXPECT_EQ(EvlanValue::BOOLEAN, result->GetType());
  EXPECT_TRUE(result->GetBooleanValue(&bool_value));
  EXPECT_EQ(true, bool_value);
  EXPECT_FALSE(result->GetIntegerValue(&int_value));

  // Integer
  result.reset(evaluator.NewInteger(123));
  EXPECT_EQ(EvlanValue::INTEGER, result->GetType());
  EXPECT_TRUE(result->GetIntegerValue(&int_value));
  EXPECT_EQ(123, int_value);
  EXPECT_TRUE(result->GetDoubleValue(&double_value));
  EXPECT_EQ(123, double_value);
  EXPECT_FALSE(result->GetAtomValue(&string_value));

  // Double
  result.reset(evaluator.NewDouble(1.5));
  EXPECT_EQ(EvlanValue::DOUBLE, result->GetType());
  EXPECT_TRUE(result->GetDoubleValue(&double_value));
  EXPECT_EQ(1.5, double_value);
  EXPECT_FALSE(result->GetIntegerValue(&int_value));
  EXPECT_FALSE(result->GetAtomValue(&string_value));

  // Atom
  result.reset(evaluator.NewAtom("foo"));
  EXPECT_EQ(EvlanValue::ATOM, result->GetType());
  EXPECT_TRUE(result->GetAtomValue(&string_value));
  EXPECT_EQ(StringPiece("foo"), string_value);
  EXPECT_FALSE(result->GetStringValue(&string_value));

  // String
  result.reset(evaluator.NewString("foo"));
  EXPECT_EQ(EvlanValue::STRING, result->GetType());
  EXPECT_TRUE(result->GetStringValue(&string_value));
  EXPECT_EQ(StringPiece("foo"), string_value);
  EXPECT_FALSE(result->GetProtobufValue(&message_value));

  // Protobuf
  result.reset(evaluator.NewProtobufReference(
    google::protobuf::DescriptorProto::default_instance()));
  EXPECT_EQ(EvlanValue::PROTOBUF, result->GetType());
  EXPECT_TRUE(result->GetProtobufValue(&message_value));
  EXPECT_EQ(&google::protobuf::DescriptorProto::default_instance(), message_value);
  EXPECT_FALSE(result->GetTypeErrorDescription(&string_value));

  // Owned protobuf.
  google::protobuf::DescriptorProto* original_message = new google::protobuf::DescriptorProto;
  result.reset(evaluator.NewProtobuf(original_message));
  EXPECT_EQ(EvlanValue::PROTOBUF, result->GetType());
  EXPECT_TRUE(result->GetProtobufValue(&message_value));
  EXPECT_EQ(original_message, message_value);
  CustomValue* dummy;
  EXPECT_FALSE(result->GetCustomValue(&dummy));

  // TypeError
  result.reset(evaluator.NewTypeError("foo"));
  EXPECT_EQ(EvlanValue::TYPE_ERROR, result->GetType());
  EXPECT_TRUE(result->GetTypeErrorDescription(&string_value));
  EXPECT_EQ(StringPiece("foo"), string_value);
  EXPECT_FALSE(result->GetBooleanValue(&bool_value));

  // DebugString
  result.reset(evaluator.NewInteger(123));
  EXPECT_EQ("123", result->DebugString());
}

// Similar to above test, except we use Evaluate() to compute each value.
// This verifies that the types are really compatible with what the VM uses
// internally, and also tests that Evaluate() works.
TEST(EvlanEvaluatorTest, EvaluateBasicTypes) {
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> result;

  bool bool_value = false;
  int int_value = -1;
  double double_value = -1;
  StringPiece string_value;

  // Boolean
  result.reset(evaluator.Evaluate("true"));
  EXPECT_EQ(EvlanValue::BOOLEAN, result->GetType());
  EXPECT_TRUE(result->GetBooleanValue(&bool_value));
  EXPECT_EQ(true, bool_value);

  // Integer
  result.reset(evaluator.Evaluate("123"));
  EXPECT_EQ(EvlanValue::INTEGER, result->GetType());
  EXPECT_TRUE(result->GetIntegerValue(&int_value));
  EXPECT_EQ(123, int_value);

  // Double
  result.reset(evaluator.Evaluate("1.5"));
  EXPECT_EQ(EvlanValue::DOUBLE, result->GetType());
  EXPECT_TRUE(result->GetDoubleValue(&double_value));
  EXPECT_EQ(1.5, double_value);

  // Atom
  result.reset(evaluator.Evaluate("@foo"));
  EXPECT_EQ(EvlanValue::ATOM, result->GetType());
  EXPECT_TRUE(result->GetAtomValue(&string_value));
  EXPECT_EQ(StringPiece("foo"), string_value);

  // String
  result.reset(evaluator.Evaluate("\"foo\""));
  EXPECT_EQ(EvlanValue::STRING, result->GetType());
  EXPECT_TRUE(result->GetStringValue(&string_value));
  EXPECT_EQ(StringPiece("foo"), string_value);

  // TypeError
  result.reset(evaluator.Evaluate("1(1)"));
  EXPECT_EQ(EvlanValue::TYPE_ERROR, result->GetType());
  EXPECT_TRUE(result->GetTypeErrorDescription(&string_value));
  EXPECT_EQ(StringPiece("Parameter to atom table must be an atom."), string_value);

  // DebugString
  result.reset(evaluator.Evaluate("{12, 34, 56, 78}"));
  EXPECT_EQ(EvlanValue::OTHER, result->GetType());
  EXPECT_EQ("{..., ..., ..., ...}", result->DebugString(0));
  EXPECT_EQ("{12, 34, 56, 78}", result->DebugString(1));
}

TEST(EvlanEvaluatorTest, Call) {
  EvlanEvaluator evaluator;

  scoped_ptr<EvlanValue> func, param, param2, result;
  vector<EvlanValue*> params;

  // Single-arg, passed directly.
  func.reset(evaluator.Evaluate("x => {x}"));
  param.reset(evaluator.NewInteger(123));
  result.reset(func->Call(param.get()));
  EXPECT_EQ("{123}", result->DebugString());

  // Single-arg, but passed in a vector.
  params.push_back(param.get());
  result.reset(func->Call(params));
  EXPECT_EQ("{123}", result->DebugString());

  // Multi-arg function.
  func.reset(evaluator.Evaluate("(x, y) => {x, y}"));
  param.reset(evaluator.NewInteger(123));
  param2.reset(evaluator.NewInteger(456));
  params.clear();
  params.push_back(param.get());
  params.push_back(param2.get());
  result.reset(func->Call(params));
  EXPECT_EQ("{123, 456}", result->DebugString());

  // GetMember
  func.reset(evaluator.Evaluate("x => {x}"));
  result.reset(func->GetMember("foo"));
  EXPECT_EQ("{@foo}", result->DebugString());
}

TEST(EvlanEvaluatorTest, Fork) {
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> param(evaluator.NewInteger(123));

  scoped_ptr<EvlanEvaluator> forked(evaluator.Fork());
  scoped_ptr<EvlanValue> func(forked->Evaluate("x => {x}"));

  scoped_ptr<EvlanValue> result(func->Call(param.get()));

  scoped_ptr<EvlanEvaluator> child(forked->Fork());
  scoped_ptr<EvlanEvaluator> incompatible(evaluator.Fork());

  EXPECT_EQ("{123}", result->DebugString());

  scoped_ptr<EvlanValue> child_param(child->NewInteger(123));
  scoped_ptr<EvlanValue> child_result(func->Call(child_param.get()));
  EXPECT_EQ("{123}", child_result->DebugString());

  scoped_ptr<EvlanValue> incompatible_param(incompatible->NewInteger(123));
  EXPECT_DEATH(func->Call(incompatible_param.get()),
    "Function and parameter are from independent EvlanEvaluators.");

  EXPECT_DEBUG_DEATH(
    scoped_ptr<EvlanValue> dummy(evaluator.NewInteger(123)),
    "An EvlanEvaluator cannot be used to construct or evaluate new values "
    "after Fork\\(\\) has been called\\.");
}

TEST(EvlanEvaluatorTest, ForkedAtoms) {
  EvlanEvaluator evaluator;

  scoped_ptr<EvlanValue> foo(evaluator.NewAtom("foo"));

  scoped_ptr<EvlanEvaluator> child1(evaluator.Fork());
  scoped_ptr<EvlanEvaluator> child2(evaluator.Fork());

  scoped_ptr<EvlanValue> foo1(child1->NewAtom("foo"));
  scoped_ptr<EvlanValue> foo2(child2->NewAtom("foo"));
  scoped_ptr<EvlanValue> bar1(child1->NewAtom("bar"));
  scoped_ptr<EvlanValue> bar2(child2->NewAtom("bar"));

  AtomId foo_id, foo1_id, foo2_id, bar1_id, bar2_id;

  ASSERT_TRUE(foo ->GetAtomValue(&foo_id));
  ASSERT_TRUE(foo1->GetAtomValue(&foo1_id));
  ASSERT_TRUE(foo2->GetAtomValue(&foo2_id));
  ASSERT_TRUE(bar1->GetAtomValue(&bar1_id));
  ASSERT_TRUE(bar2->GetAtomValue(&bar2_id));

  // The foos should all be equal since the "foo" atom was created in the
  // parent evaluator before any children were spawned.
  EXPECT_TRUE(foo_id == foo1_id);
  EXPECT_TRUE(foo_id == foo2_id);
  EXPECT_TRUE(foo1_id == foo2_id);

  // The bars should not be equal since we did not create a "bar" atom in
  // the common parent.
  EXPECT_TRUE(bar1_id != bar2_id);

  // Even more important:  Deleting the first child should not affect the
  // second child's copy of the atom.
  foo1.reset();
  bar1.reset();
  child1.reset();
  EXPECT_EQ("@bar", bar2->DebugString());
}

// Test that builtinSupport.evlan is automatically loaded as support code.
TEST(EvlanEvaluatorTest, EmbeddedSupportCode) {
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> result;
  result.reset(evaluator.Evaluate("123 + 456"));

  int int_value;
  if (result->GetIntegerValue(&int_value)) {
    EXPECT_EQ(123 + 456, int_value);
  } else {
    ADD_FAILURE() << "Expected integer result, got: " << result->DebugString();
  }
}

// Test using custom support code.
TEST(EvlanEvaluatorTest, CustomSupportCode) {
  EvlanEvaluator::Options options;
  options.SetSupportCode("type => value => param => {type, value, param}");

  EvlanEvaluator evaluator(options);
  scoped_ptr<EvlanValue> result;
  result.reset(evaluator.Evaluate("123(456)"));

  EXPECT_EQ("{@integer, 123, 456}", result->DebugString());
}

// Test using no support code.
TEST(EvlanEvaluatorTest, NoSupportCode) {
  EvlanEvaluator::Options options;
  options.UseNoSupportCode();

  EvlanEvaluator evaluator(options);
  scoped_ptr<EvlanValue> result;
  result.reset(evaluator.Evaluate("123(456)"));

  EXPECT_EQ("typeError(\"Called placeholder logic.\")",
            result->DebugString());
}

TEST(EvlanEvaluatorTest, AtomId) {
  EvlanEvaluator::Options options;
  options.UseDebugMemoryManager();
  options.UseNoSupportCode();
  EvlanEvaluator evaluator(options);

  AtomId foo_id, foo2_id, bar_id, bar2_id;

  {
    scoped_ptr<EvlanValue> foo(evaluator.NewAtom("foo"));
    scoped_ptr<EvlanValue> foo2(evaluator.NewAtom("foo"));
    scoped_ptr<EvlanValue> bar(evaluator.NewAtom("bar"));


    ASSERT_TRUE(foo->GetPermanentAtomValue(&foo_id));
    ASSERT_TRUE(foo2->GetAtomValue(&foo2_id));
    ASSERT_TRUE(bar->GetAtomValue(&bar_id));

    EXPECT_TRUE(foo_id == foo2_id);
    EXPECT_TRUE(foo_id != bar_id);

    EXPECT_EQ(foo_id.hash(), foo2_id.hash());
    EXPECT_NE(foo_id.hash(), bar_id.hash());

    EXPECT_EQ(foo_id.hash(), AtomIdHash()(foo_id));

    EXPECT_EQ(StringPiece("foo"), foo_id.GetName());
    EXPECT_EQ(StringPiece("bar"), bar_id.GetName());
  }

  // Force garbage collection by allocating something new.  The "bar" atom
  // should be deleted but "foo" should not since we used
  // GetPermanentAtomValue().
  scoped_ptr<EvlanValue> dummy(evaluator.NewString("abcd"));

  // Allocate an atom to reduce the probability that the "bar" atom will be
  // re-allocated in the same place where it was before.
  scoped_ptr<EvlanValue> dummy2(evaluator.NewAtom("baz"));

  // Now create foo and bar again.
  scoped_ptr<EvlanValue> foo(evaluator.NewAtom("foo"));
  scoped_ptr<EvlanValue> bar(evaluator.NewAtom("bar"));

  // foo_id is still valid since we used GetPermanentAtomValue().
  ASSERT_TRUE(foo->GetAtomValue(&foo2_id));
  EXPECT_TRUE(foo_id == foo2_id);

  // bar_id is no longer valid.
  ASSERT_TRUE(bar->GetAtomValue(&bar2_id));
  EXPECT_FALSE(bar_id == bar2_id);
}

TEST(EvlanEvaluatorTest, Clone) {
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> value(evaluator.NewInteger(123));
  scoped_ptr<EvlanValue> clone(value->Clone());
  EXPECT_NE(value.get(), clone.get());

  int int_value;
  EXPECT_TRUE(clone->GetIntegerValue(&int_value));
  EXPECT_EQ(123, int_value);
}

TEST(EvlanEvaluatorTest, SizeOneTuple) {
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> value(evaluator.NewInteger(123));

  EXPECT_EQ(1, value->GetTupleSize());

  {
    EvlanValue* members[2];
    EXPECT_FALSE(value->GetTupleContents(2, members));
    EXPECT_TRUE(value->GetTupleContents(1, members));
    EXPECT_EQ("123", members[0]->DebugString());
    ASSERT_NE(value.get(), members[0]);
    delete members[0];
  }

  {
    scoped_ptr<EvlanValue> members[2];
    EXPECT_FALSE(value->GetTupleContents(2, members));
    EXPECT_TRUE(value->GetTupleContents(1, members));
    EXPECT_EQ("123", members[0]->DebugString());
    EXPECT_NE(value.get(), members[0].get());
  }
}

TEST(EvlanEvaluatorTest, MultiElementTuple) {
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> value(
    evaluator.Evaluate("(x => x)(123, 456)"));

  EXPECT_EQ(2, value->GetTupleSize());

  {
    EvlanValue* members[2];
    EXPECT_FALSE(value->GetTupleContents(3, members));
    EXPECT_TRUE(value->GetTupleContents(2, members));
    EXPECT_EQ("123", members[0]->DebugString());
    EXPECT_EQ("456", members[1]->DebugString());
    delete members[0];
    delete members[1];
  }

  {
    scoped_ptr<EvlanValue> members[2];
    EXPECT_FALSE(value->GetTupleContents(3, members));
    EXPECT_TRUE(value->GetTupleContents(2, members));
    EXPECT_EQ("123", members[0]->DebugString());
    EXPECT_EQ("456", members[1]->DebugString());
  }
}

// Note that GetTupleContents(1, ...) should *always* succeed, even if the
// value is actually a tuple of another size, because even a tuple of some
// other size is also a size-1 tuple containing another tuple.
TEST(EvlanEvaluatorTest, GotMultiElementTupleButRequestedSizeOne) {
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> value(
    evaluator.Evaluate("(x => x)(123, 456)"));

  EXPECT_EQ(2, value->GetTupleSize());

  {
    EvlanValue* members[1];
    EXPECT_TRUE(value->GetTupleContents(1, members));
    EXPECT_EQ("(123, 456)", members[0]->DebugString());
    ASSERT_NE(value.get(), members[0]);
    delete members[0];
  }

  {
    scoped_ptr<EvlanValue> members[1];
    EXPECT_TRUE(value->GetTupleContents(1, members));
    EXPECT_EQ("(123, 456)", members[0]->DebugString());
    EXPECT_NE(value.get(), members[0].get());
  }
}

TEST(EvlanEvaluatorTest, MakeTuple) {
  vector<EvlanValue*> elements;

  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> tuple(evaluator.NewTuple(elements));
  EXPECT_EQ(0, tuple->GetTupleSize());

  scoped_ptr<EvlanValue> element1(evaluator.NewInteger(123));
  elements.push_back(element1.get());

  int int_value;
  tuple.reset(evaluator.NewTuple(elements));
  EXPECT_EQ(1, tuple->GetTupleSize());
  EXPECT_TRUE(tuple->GetIntegerValue(&int_value));
  EXPECT_EQ(123, int_value);

  scoped_ptr<EvlanValue> element2(evaluator.NewInteger(456));
  elements.push_back(element2.get());

  tuple.reset(evaluator.NewTuple(elements));
  scoped_ptr<EvlanValue> contents[2];
  EXPECT_TRUE(tuple->GetTupleContents(2, contents));
  EXPECT_TRUE(contents[0]->GetIntegerValue(&int_value));
  EXPECT_EQ(123, int_value);
  EXPECT_TRUE(contents[1]->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);
}

TEST(EvlanEvaluatorTest, Array) {
  EvlanEvaluator evaluator;

  // Check that we can read an array created by Evlan code.
  scoped_ptr<EvlanValue> value(evaluator.Evaluate("{123, 456}"));

  vector<EvlanValue*> elements;
  EXPECT_TRUE(value->GetArrayElements(&elements));
  ASSERT_EQ(2, elements.size());

  int int_value;
  EXPECT_TRUE(elements[0]->GetIntegerValue(&int_value));
  EXPECT_EQ(123, int_value);
  EXPECT_TRUE(elements[1]->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);
  STLDeleteElements(&elements);
  elements.clear();

  // Check that GetArrayElements() doesn't work on a non-array.
  value.reset(evaluator.NewInteger(123));
  EXPECT_FALSE(value->GetArrayElements(&elements));

  // Check that we can pass an array to Evlan code.
  elements.push_back(evaluator.NewAtom("foo"));
  elements.push_back(evaluator.NewAtom("bar"));
  value.reset(evaluator.NewArray(elements));
  STLDeleteElements(&elements);
  EXPECT_EQ("{@foo, @bar}", value->DebugString());
}

TEST(EvlanEvaluatorTest, CustomValue) {
  class MockCustomValue : public CustomValue {
   public:
    MockCustomValue(EvlanEvaluator* evaluator)
      : evaluator_(evaluator) {}
    virtual ~MockCustomValue() {}

    virtual EvlanValue* Call(EvlanEvaluator* evaluator,
                             EvlanValue* self,
                             EvlanValue* parameter) {
      EXPECT_EQ(evaluator_, evaluator);

      // Check that "self" refers to this object.
      CustomValue* unwrapped_self = NULL;
      EXPECT_TRUE(self->GetCustomValue(&unwrapped_self));
      // Note:  implicit_cast doesn't work here because classes declared inside
      //   function bodies cannot be used as template parameters.  Go figure.
      CustomValue* upcasted_this = this;
      EXPECT_EQ(upcasted_this, unwrapped_self);

      // Check that "parameter" is correct.
      int int_value;
      EXPECT_TRUE(parameter->GetIntegerValue(&int_value));
      EXPECT_EQ(123, int_value);
      return evaluator->NewInteger(456);
    }

    virtual string DebugString(int depth) {
      // Expect depth is exactly the value we pass to DebugString() below.
      EXPECT_EQ(3, depth);
      return "MyDebugString";
    }

    EvlanEvaluator* evaluator_;
  };

  EvlanEvaluator evaluator;
  CustomValue* original_value = new MockCustomValue(&evaluator);
  scoped_ptr<EvlanValue> value(evaluator.NewCustomValue(original_value));
  ASSERT_EQ(EvlanValue::CUSTOM_VALUE, value->GetType());

  CustomValue* custom_value;
  EXPECT_TRUE(value->GetCustomValue(&custom_value));
  EXPECT_EQ(original_value, custom_value);

  EXPECT_EQ("MyDebugString", value->DebugString(3));

  scoped_ptr<EvlanValue> param(evaluator.NewInteger(123));
  scoped_ptr<EvlanValue> result(value->Call(param.get()));
  int int_value;
  EXPECT_TRUE(result->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);

  // -----------------------------------------------------------------

  // If we invoke the value in the context of a forked evaluator, then it's
  // the sub-evaluator that should be passed to CustomValue::Call(), not the
  // original one in which it was created.

  // Create the value using the original evaluator, but expect the forked
  // evaluator to be passed to Call().
  MockCustomValue* mock_custom_value = new MockCustomValue(NULL);
  value.reset(evaluator.NewCustomValue(mock_custom_value));
  scoped_ptr<EvlanEvaluator> forked(evaluator.Fork());
  mock_custom_value->evaluator_ = forked.get();

  // Clone the value into the forked evaluator, then call it.
  scoped_ptr<EvlanValue> forked_value(forked->CloneValue(value.get()));
  scoped_ptr<EvlanValue> forked_result(forked_value->Call(param.get()));

  // Verify the result to ensure that the CustomValue was actually called.
  EXPECT_TRUE(forked_result->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);
}

static void StorePointerCallback(scoped_ptr<EvlanValue>* target,
                                 EvlanValue* result) {
  EXPECT_TRUE(*target == NULL);
  target->reset(result);
}

// Test a non-blocking custom value which calls its callback synchronously.
TEST(EvlanEvaluatorTest, SynchronousNonBlockingCustomValue) {
  class MockNonBlockingCustomValue : public NonBlockingCustomValue {
   public:
    MockNonBlockingCustomValue(EvlanEvaluator* evaluator)
      : evaluator_(evaluator) {}
    virtual ~MockNonBlockingCustomValue() {}

    virtual void CallNonBlocking(EvlanEvaluator* evaluator,
                                 EvlanValue* self,
                                 EvlanValue* parameter,
                                 function<void(EvlanValue*)> done) {
      EXPECT_EQ(evaluator_, evaluator);

      // Check that "self" refers to this object.
      NonBlockingCustomValue* unwrapped_self = NULL;
      EXPECT_TRUE(self->GetNonBlockingCustomValue(&unwrapped_self));
      // Note:  implicit_cast doesn't work here because classes declared inside
      //   function bodies cannot be used as template parameters.  Go figure.
      NonBlockingCustomValue* upcasted_this = this;
      EXPECT_EQ(upcasted_this, unwrapped_self);

      // Check that "parameter" is correct.
      int int_value;
      EXPECT_TRUE(parameter->GetIntegerValue(&int_value));
      EXPECT_EQ(123, int_value);
      done(evaluator->NewInteger(456));
    }

    virtual string DebugString(int depth) {
      // Expect depth is exactly the value we pass to DebugString() below.
      EXPECT_EQ(3, depth);
      return "MyNonBlockingDebugString";
    }

    EvlanEvaluator* evaluator_;
  };

  EvlanEvaluator evaluator;
  NonBlockingCustomValue* original_value =
      new MockNonBlockingCustomValue(&evaluator);
  scoped_ptr<EvlanValue> value(
      evaluator.NewNonBlockingCustomValue(original_value));
  ASSERT_EQ(EvlanValue::NON_BLOCKING_CUSTOM_VALUE, value->GetType());

  NonBlockingCustomValue* custom_value;
  EXPECT_TRUE(value->GetNonBlockingCustomValue(&custom_value));
  EXPECT_EQ(original_value, custom_value);

  EXPECT_EQ("MyNonBlockingDebugString", value->DebugString(3));

  // First try calling it using blocking style -- the evaluator should take
  // care of waiting for completion.
  scoped_ptr<EvlanValue> param(evaluator.NewInteger(123));
  scoped_ptr<EvlanValue> result(value->Call(param.get()));
  int int_value = 0;
  EXPECT_TRUE(result->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);

  // Try again, non-blocking.  Since we call the callback immediately it should
  // complete immediately.
  int_value = 0;
  result.reset();
  value->CallNonBlocking(param.get(),
                         std::bind(&StorePointerCallback, &result, _1));
  ASSERT_TRUE(result != NULL);
  EXPECT_TRUE(result->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);

  // -----------------------------------------------------------------

  // If we invoke the value in the context of a forked evaluator, then it's
  // the sub-evaluator that should be passed to CustomValue::Call(), not the
  // original one in which it was created.

  // Create the value using the original evaluator, but expect the forked
  // evaluator to be passed to Call().
  MockNonBlockingCustomValue* mock_custom_value =
      new MockNonBlockingCustomValue(NULL);
  value.reset(evaluator.NewNonBlockingCustomValue(mock_custom_value));
  scoped_ptr<EvlanEvaluator> forked(evaluator.Fork());
  mock_custom_value->evaluator_ = forked.get();

  // Clone the value into the forked evaluator, then call it.
  scoped_ptr<EvlanValue> forked_value(forked->CloneValue(value.get()));
  scoped_ptr<EvlanValue> forked_result(forked_value->Call(param.get()));

  // Verify the result to ensure that the CustomValue was actually called.
  EXPECT_TRUE(forked_result->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);
}

#if 0
// Test a non-blocking custom value which calls its callback asynchronously.
// TODO(kenton):  Implement threading to support this test.  Also, implement
//   conditional waits and use them in Evaluator so that it passes the test.
class ThreadPool {
 public:
  void AddAfter(int seconds, Closure* callback);
  void AddAfter(int seconds, function<void()> callback);
};

TEST(EvlanEvaluatorTest, AsynchronousNonBlockingCustomValue) {
  ThreadPool thread_pool;

  class MockNonBlockingCustomValue : public NonBlockingCustomValue {
   public:
    MockNonBlockingCustomValue(function<void(EvlanValue*)>* save_done_to,
                               ThreadPool* thread_pool)
      : save_done_to_(save_done_to), thread_pool_(thread_pool) {}
    virtual ~MockNonBlockingCustomValue() {}

    virtual void CallNonBlocking(EvlanEvaluator* evaluator,
                                 EvlanValue* self,
                                 EvlanValue* parameter,
                                 function<void(EvlanValue*)> done) {
      if (save_done_to_ == NULL) {
        // Invoke in another thread after a short delay.
        thread_pool_->AddAfter(10, std::bind(done, evaluator->NewInteger(456)));
      } else {
        // Save the callback.
        *save_done_to_ = done;
      }
    }

    function<void(EvlanValue*)>* save_done_to_;
    ThreadPool* thread_pool_;
  };

  EvlanEvaluator evaluator;

  // Create a value which, when called, will complete in a separate thread
  // after 10ms.
  scoped_ptr<EvlanValue> value(
      evaluator.NewNonBlockingCustomValue(
        new MockNonBlockingCustomValue(NULL, &thread_pool)));

  // Call it in blocking mode.  The evaluator should wait for completion before
  // returning.
  scoped_ptr<EvlanValue> param(evaluator.NewInteger(123));
  scoped_ptr<EvlanValue> result(value->Call(param.get()));
  int int_value = 0;
  EXPECT_TRUE(result->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);

  // Try again, non-blocking.  This time we tell our mock value to save the
  // callback off to the side rather than call it after 10ms.  This way we can
  // explicitly call it after value->CallNonBlocking() returns.
  function<void(EvlanValue*)> done = NULL;
  value.reset(evaluator.NewNonBlockingCustomValue(
                new MockNonBlockingCustomValue(&done, NULL)));
  int_value = 0;
  result.reset();
  value->CallNonBlocking(param.get(),
                         std::bind(&StorePointerCallback, &result, _1));
  EXPECT_TRUE(result == NULL);  // At this time the call has NOT completed.
  ASSERT_TRUE(done != NULL);

  // Now call the callback.  This should cause the call to complete.
  done(evaluator.NewInteger(456));
  ASSERT_TRUE(result != NULL);
  EXPECT_TRUE(result->GetIntegerValue(&int_value));
  EXPECT_EQ(456, int_value);
}
#endif

TEST(EvlanEvaluatorTest, ImportResolver) {
  EvlanEvaluator::Options options;
  options.UseDebugMemoryManager();
  options.UseNoSupportCode();
  EvlanEvaluator evaluator(options);
  MappedImportResolver resolver;
  scoped_ptr<EvlanValue> value;

  value.reset(evaluator.NewAtom("foo"));
  resolver.Map("foo", value.get());
  value.reset(evaluator.NewAtom("bar"));
  resolver.Map("bar", value.get());

  value.reset(evaluator.Evaluate("import \"foo\"", &resolver));
  EXPECT_EQ("@foo", value->DebugString());

  value.reset(evaluator.Evaluate("import \"bar\"", &resolver));
  EXPECT_EQ("@bar", value->DebugString());

  value.reset(evaluator.Evaluate("import \"baz\"", &resolver));
  StringPiece description;
  EXPECT_TRUE(value->GetTypeErrorDescription(&description));
  EXPECT_EQ(StringPiece("Unknown module: baz"), description);
}

TEST(EvlanEvaluatorTest, ProtoFile) {
  EvlanEvaluator evaluator;

  scoped_ptr<EvlanValue> proto(
    evaluator.NewProtoFile(google::protobuf::DescriptorProto::descriptor()->file(),
                           google::protobuf::MessageFactory::generated_factory(),
                           NULL));

  // We can't rely on proto->DebugString() returning anything in particular,
  // so we need to actually use the file somehow.
  scoped_ptr<EvlanValue> code(evaluator.Evaluate(
    "proto => proto.DescriptorProto.defaultInstance.nestedType.size"));
  scoped_ptr<EvlanValue> result(code->Call(proto.get()));
  EXPECT_EQ("0", result->DebugString());
}

}  // namespace
}  // namespace evlan
