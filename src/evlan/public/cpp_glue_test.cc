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

#include "evlan/public/cpp_glue.h"
#include "evlan/public/evaluator.h"
#include "evlan/proto/module.pb.h"
#include "evlan/stringpiece.h"
#include <gtest/gtest.h>

namespace evlan {

class MockType;

// Test that Koenig lookup tricks are working correctly by exporting a type
// in a different namespace.
namespace test {

class IntWrapper {
 public:
  IntWrapper(int value): value_(value) {}

  int Get() const { return value_; }

  IntWrapper* Add(const IntWrapper& other) const {
    return new IntWrapper(value_ + other.value_);
  }

  // Make sure mutually-dependent types work.
  MockType* NewMockType() const;

 private:
  int value_;
};

EVLAN_EXPORT_TYPE(IntWrapper) {
  EVLAN_EXPORT_METHOD(Get);
  EVLAN_EXPORT_METHOD(Add);
  EVLAN_EXPORT_METHOD(NewMockType);
}

}  // namespace test

const string kQux = "qux";
const StringPiece kCorge = "corge";
const test::IntWrapper k456Wrapper(456);

class MockType {
 public:
  bool               ReturnTrue () const { return true  ; }
  bool               ReturnFalse() const { return false ; }
  int                Return123  () const { return 123   ; }
  double             Return1p5  () const { return 1.5   ; }
  const char*        ReturnFoo  () const { return "foo" ; }
  string             ReturnBar  () const { return "bar" ; }
  StringPiece        ReturnBaz  () const { return "baz" ; }
  const string&      ReturnQux  () const { return kQux  ; }
  const StringPiece& ReturnCorge() const { return kCorge; }

  bool IsTrue (bool               value) const { return value == true   ; }
  bool IsFalse(bool               value) const { return value == false  ; }
  bool Is123  (int                value) const { return value == 123    ; }
  bool Is1p5  (double             value) const { return value == 1.5    ; }
  bool IsFoo  (const char*        value) const { return string(value)=="foo"; }
  bool IsBar  (string             value) const { return value == "bar"  ; }
  bool IsBaz  (StringPiece        value) const { return value == "baz"  ; }
  bool IsQux  (const string&      value) const { return value == "qux"  ; }
  bool IsCorge(const StringPiece& value) const { return value == "corge"; }

  test::IntWrapper* NewIntWrapper(int value) const {
    return new test::IntWrapper(value);
  }
  const test::IntWrapper* NewConstIntWrapper(int value) const {
    return new test::IntWrapper(value);
  }
  const test::IntWrapper& Get456Wrapper() const {
    return k456Wrapper;
  }

  bool IsIntWrapper(const test::IntWrapper& wrapper, int value) const {
    return wrapper.Get() == value;
  }
  bool Is456Wrapper(const test::IntWrapper* wrapper) const {
    return wrapper == &k456Wrapper;
  }

  EvlanValue* ReturnAtom(EvlanEvaluator* evaluator, string name) const {
    return evaluator->NewAtom(name);
  }
  bool IsAtom(EvlanValue* value, string expected_name) const {
    StringPiece name;
    if (!value->GetAtomValue(&name)) return false;
    return name == expected_name;
  }
  bool IsFooAtom(AtomId atom) const {
    return atom.GetName() == "foo";
  }
  bool AreAtomsEqual(AtomId a, const AtomId& b) const {
    return a == b;
  }

  // Protocol buffers -- I'm using Module here so that I don't have to write
  // a .proto file with a test type.
  Module* NewModule(int a, int b) const {
    Module* result = new Module;
    result->add_code_tree(a);
    result->add_code_tree(b);
    return result;
  }

  bool IsModule(const Module* module, int a, int b) const {
    return module->code_tree_size() == 2 &&
           module->code_tree(0) == a &&
           module->code_tree(1) == b;
  }

  const Module& DefaultModule() const {
    return Module::default_instance();
  }

  bool IsDefaultModule(const Module& module) const {
    return &module == &Module::default_instance();
  }

  // Fields
  string grault_field() const { return "grault"; }

  // Arrays
  vector<string>* ReturnFooBarBaz() const {
    vector<string>* result = new vector<string>;
    result->push_back("foo");
    result->push_back("bar");
    result->push_back("baz");
    return result;
  }

  const vector<int>& Return123456789() const {
    // Use a static so that it's permanent and we can return a reference.
    static vector<int> result;
    if (result.empty()) {
      // First time this has been called.
      result.push_back(123);
      result.push_back(456);
      result.push_back(789);
    }
    return result;
  }

  bool IsFooBarBaz(const vector<StringPiece>* elements) const {
    return elements->size() == 3 &&
           elements->at(0) == "foo" &&
           elements->at(1) == "bar" &&
           elements->at(2) == "baz";
  }

  bool Is123456789(const vector<int>& elements) const {
    return elements.size() == 3 &&
           elements[0] == 123 &&
           elements[1] == 456 &&
           elements[2] == 789;
  }
};

EVLAN_EXPORT_TYPE(MockType) {
  EVLAN_EXPORT_METHOD(ReturnTrue );
  EVLAN_EXPORT_METHOD(ReturnFalse);
  EVLAN_EXPORT_METHOD(Return123  );
  EVLAN_EXPORT_METHOD(Return1p5  );
  EVLAN_EXPORT_METHOD(ReturnFoo  );
  EVLAN_EXPORT_METHOD(ReturnBar  );
  EVLAN_EXPORT_METHOD(ReturnBaz  );
  EVLAN_EXPORT_METHOD(ReturnQux  );
  EVLAN_EXPORT_METHOD(ReturnCorge);

  EVLAN_EXPORT_METHOD(IsTrue );
  EVLAN_EXPORT_METHOD(IsFalse);
  EVLAN_EXPORT_METHOD(Is123  );
  EVLAN_EXPORT_METHOD(Is1p5  );
  EVLAN_EXPORT_METHOD(IsFoo  );
  EVLAN_EXPORT_METHOD(IsBar  );
  EVLAN_EXPORT_METHOD(IsBaz  );
  EVLAN_EXPORT_METHOD(IsQux  );
  EVLAN_EXPORT_METHOD(IsCorge);

  EVLAN_EXPORT_METHOD(NewIntWrapper);
  EVLAN_EXPORT_METHOD(NewConstIntWrapper);
  EVLAN_EXPORT_METHOD(Get456Wrapper);
  EVLAN_EXPORT_METHOD(IsIntWrapper);
  EVLAN_EXPORT_METHOD(Is456Wrapper);

  EVLAN_EXPORT_METHOD(ReturnAtom);
  EVLAN_EXPORT_METHOD(IsAtom);
  EVLAN_EXPORT_METHOD(IsFooAtom);
  EVLAN_EXPORT_METHOD(AreAtomsEqual);

  EVLAN_EXPORT_METHOD(NewModule);
  EVLAN_EXPORT_METHOD(IsModule);
  EVLAN_EXPORT_METHOD(DefaultModule);
  EVLAN_EXPORT_METHOD(IsDefaultModule);

  EVLAN_EXPORT_FIELD(grault_field);

  EVLAN_EXPORT_METHOD(ReturnFooBarBaz);
  EVLAN_EXPORT_METHOD(Return123456789);
  EVLAN_EXPORT_METHOD(IsFooBarBaz);
  EVLAN_EXPORT_METHOD(Is123456789);
}

namespace test {

MockType* IntWrapper::NewMockType() const {
  return new MockType;
}

}  // namespace test

// ===================================================================

class CppGlueTest : public testing::Test {
 protected:
  CppGlueTest() {
    original_mock_value_ = new MockType;
    mock_value_.reset(evaluator_.NewCppObject(original_mock_value_));
  }

  void ExpectTrue(const string& code) {
    scoped_ptr<EvlanEvaluator> sub_evaluator(evaluator_.Fork());

    string real_code = "mockValue => " + code;
    scoped_ptr<EvlanValue> compiled(sub_evaluator->Evaluate(real_code));
    scoped_ptr<EvlanValue> result(compiled->Call(mock_value_.get()));

    EXPECT_EQ("true", result->DebugString())
      << "Code did not return true: " + code;
  }

  void ExpectError(const string& code, StringPiece expected_description) {
    scoped_ptr<EvlanEvaluator> sub_evaluator(evaluator_.Fork());

    string real_code = "mockValue => " + code;
    scoped_ptr<EvlanValue> compiled(sub_evaluator->Evaluate(real_code));
    scoped_ptr<EvlanValue> result(compiled->Call(mock_value_.get()));

    StringPiece description;
    if (result->GetTypeErrorDescription(&description)) {
      EXPECT_EQ(expected_description, description)
        << "Code did not return expected error: " + code;
    } else {
      ADD_FAILURE()
        << "Code did not return error: " + code
        << "\nActual result: " << result->DebugString()
        << "\nExpected error: " << expected_description;
    }
  }

  EvlanEvaluator evaluator_;
  const MockType* original_mock_value_;
  scoped_ptr<EvlanValue> mock_value_;
};

TEST_F(CppGlueTest, Query) {
  const MockType* pointer;
  EXPECT_TRUE(mock_value_->GetCppObjectValue(&pointer));
  EXPECT_EQ(original_mock_value_, pointer);

  const test::IntWrapper* bad_pointer;
  EXPECT_FALSE(mock_value_->GetCppObjectValue(&bad_pointer));
}

TEST_F(CppGlueTest, BuiltinTypes) {
  ExpectTrue("mockValue.returnTrue()");
  ExpectTrue("not mockValue.returnFalse()");
  ExpectTrue("mockValue.return123  () == 123");
  ExpectTrue("mockValue.return1p5  () == 1.5");
  ExpectTrue("mockValue.returnFoo  () == \"foo\"");
  ExpectTrue("mockValue.returnBar  () == \"bar\"");
  ExpectTrue("mockValue.returnBaz  () == \"baz\"");
  ExpectTrue("mockValue.returnQux  () == \"qux\"");
  ExpectTrue("mockValue.returnCorge() == \"corge\"");

  ExpectTrue("mockValue.isTrue (true     )");
  ExpectTrue("mockValue.isFalse(false    )");
  ExpectTrue("mockValue.is123  (123      )");
  ExpectTrue("mockValue.is1p5  (1.5      )");
  ExpectTrue("mockValue.isFoo  (\"foo\"  )");
  ExpectTrue("mockValue.isBar  (\"bar\"  )");
  ExpectTrue("mockValue.isBaz  (\"baz\"  )");
  ExpectTrue("mockValue.isQux  (\"qux\"  )");
  ExpectTrue("mockValue.isCorge(\"corge\")");
}

TEST_F(CppGlueTest, Arrays) {
  ExpectTrue("mockValue.returnFooBarBaz().size == 3");
  ExpectTrue("mockValue.returnFooBarBaz()[0] == \"foo\"");
  ExpectTrue("mockValue.returnFooBarBaz()[1] == \"bar\"");
  ExpectTrue("mockValue.returnFooBarBaz()[2] == \"baz\"");

  ExpectTrue("mockValue.return123456789().size == 3");
  ExpectTrue("mockValue.return123456789()[0] == 123");
  ExpectTrue("mockValue.return123456789()[1] == 456");
  ExpectTrue("mockValue.return123456789()[2] == 789");

  ExpectTrue("mockValue.isFooBarBaz({\"foo\", \"bar\", \"baz\"})");
  ExpectTrue("mockValue.is123456789({123, 456, 789})");
}

TEST_F(CppGlueTest, Objects) {
  ExpectTrue("mockValue.newIntWrapper(789).get() == 789");
  ExpectTrue("mockValue.newConstIntWrapper(789).get() == 789");
  ExpectTrue("mockValue.get456Wrapper().get() == 456");
  ExpectTrue("mockValue.isIntWrapper(mockValue.newIntWrapper(789), 789)");
  ExpectTrue("mockValue.isIntWrapper(mockValue.newConstIntWrapper(789), 789)");
  ExpectTrue("mockValue.isIntWrapper(mockValue.get456Wrapper(), 456)");
  ExpectTrue("mockValue.is456Wrapper(mockValue.get456Wrapper())");

  ExpectTrue("mockValue.get456Wrapper().newMockType().returnTrue()");
}

TEST_F(CppGlueTest, Protobufs) {
  ExpectTrue("mockValue.newModule(123, 456).codeTree[0] == 123");
  ExpectTrue("mockValue.isModule(mockValue.newModule(123, 456), 123, 456)");
  ExpectTrue("not mockValue.isModule(mockValue.defaultModule(), 123, 456)");
  ExpectTrue("mockValue.isDefaultModule(mockValue.defaultModule())");
}

TEST_F(CppGlueTest, ArbitraryValues) {
  ExpectTrue("mockValue.returnAtom(\"foo\") == @foo");
  ExpectTrue("mockValue.isAtom(@foo, \"foo\")");
  ExpectTrue("mockValue.isFooAtom(@foo)");
  ExpectTrue("mockValue.areAtomsEqual(@foo, @foo)");
  ExpectTrue("not mockValue.areAtomsEqual(@foo, @bar)");
}

TEST_F(CppGlueTest, Errors) {
  ExpectError("mockValue.areAtomsEqual(@foo)",
    "Method \"evlan::MockType::AreAtomsEqual\" takes 2 arguments, but was "
    "given 1.");

  ExpectError("mockValue.is123(@foo)",
    "Parameter type mismatch for parameter 0 of method "
    "\"evlan::MockType::Is123\".  Evlan value could not be converted to type "
    "\"int\": @foo");
}

TEST_F(CppGlueTest, Field) {
  ExpectTrue("mockValue.graultField == \"grault\"");
}

TEST_F(CppGlueTest, WrapObjectReference) {
  MockType local_mock_value;
  EvlanEvaluator evaluator;
  scoped_ptr<EvlanValue> value(
    evaluator.NewCppObjectReference(local_mock_value));
  scoped_ptr<EvlanValue> function(
    evaluator.Evaluate("mockValue => mockValue.return123() == 123"));
  scoped_ptr<EvlanValue> result(function->Call(value.get()));
  EXPECT_EQ("true", result->DebugString());
}

// Verify that if we register a type only in a child evaluator, it doesn't
// cause problems.  Namely, it should not create new atoms using the parent
// evaluator.
TEST(CppGlueForkTest, NoInterference) {
  EvlanEvaluator parent;
  scoped_ptr<EvlanEvaluator> child1(parent.Fork());
  scoped_ptr<EvlanEvaluator> child2(parent.Fork());

  scoped_ptr<EvlanValue> child1_value(child1->NewCppObject(new MockType));
  scoped_ptr<EvlanValue> child2_value(child2->NewCppObject(new MockType));

  // Now delete child1 and expect that child2 is still usable.
  child1_value.reset();
  child1.reset();

  scoped_ptr<EvlanValue> code(
    child2->Evaluate("mockValue => mockValue.return123()"));
  scoped_ptr<EvlanValue> result(code->Call(child2_value.get()));
  EXPECT_EQ("123", result->DebugString());

  // If that didn't crash, we're all good.
}

}  // namespace evlan
