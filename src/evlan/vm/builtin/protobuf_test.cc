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

#include "evlan/vm/builtin/protobuf.h"

#include <string>
#include "evlan/vm/builtin/protobuf_test.pb.h"
#include "evlan/testing/test_util.h"
#include <google/protobuf/descriptor.h>
#include "evlan/strutil.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace builtin {
namespace {

class ProtobufTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    message_.set_optional_int32 (101);
    message_.set_optional_int64 (102);
    message_.set_optional_uint32(103);
    message_.set_optional_uint64(104);
    message_.set_optional_float (105);
    message_.set_optional_double(106);
    message_.set_optional_bool  (true);
    message_.set_optional_string("108");
    message_.set_optional_enum  (TestAllTypes::BAR);
    message_.mutable_optional_message()->set_a(110);

    message_.add_repeated_int32  (201);
    message_.add_repeated_int64  (202);
    message_.add_repeated_uint32 (203);
    message_.add_repeated_uint64 (204);
    message_.add_repeated_float  (205);
    message_.add_repeated_double (206);
    message_.add_repeated_string ("208");
    message_.add_repeated_bool   (false);
    message_.add_repeated_enum   (TestAllTypes::BAZ);
    message_.add_repeated_message()->set_a(210);

    message_.add_repeated_int32  (301);
    message_.add_repeated_int64  (302);
    message_.add_repeated_uint32 (303);
    message_.add_repeated_uint64 (304);
    message_.add_repeated_float  (305);
    message_.add_repeated_double (306);
    message_.add_repeated_bool   (true);
    message_.add_repeated_string ("308");
    message_.add_repeated_enum   (TestAllTypes::FOO);
    message_.add_repeated_message()->set_a(310);

    message_.add_repeated_float  (305.5);
    message_.add_repeated_double (306.5);

    AddImport("protobuf", Value(ProtobufUtils::GetBuiltinModule(), Data()));
    AddImport("message",
      ProtobufUtils::WrapMessage(GetMemoryManager(), &message_));

    const google::protobuf::FileDescriptor* test_file = message_.GetDescriptor()->file();
    Value filevalue = ProtobufUtils::MakeFile(
        GetMemoryManager(),
        GetMemoryRoot(),
        test_file,
        google::protobuf::MessageFactory::generated_factory(),
        NULL,
        NULL);

    AddImport("protofile", filevalue);
  }

  TestAllTypes message_;

  // The Evlan code that we evaluate in these tests involves a lot of
  // boilerplate.  These helper functions help separate the boilerplate from
  // the meat that we care about.  Some of this code could perhaps be made
  // more readable by using where blocks, but using DebugMemoryManager causes
  // execution to be O(n^2) with the complexity of the code being executed, so
  // we want to keep things as simple as we can to keep the tests fast.
  string ReadField(const string& name) {
    return "import \"message\"." + name;
  }

  string GetTestAllTypes() {
    return "import \"protofile\".TestAllTypes";
  }

  string ReadRepeatedFieldSize(const string& name) {
    return strings::Substitute(
      "import \"protobuf\".repeatedSize(import \"message\".$0)",
      name);
  }

  string ReadRepeatedFieldElement(const string& name, int index) {
    return strings::Substitute(
      "import \"protobuf\".repeatedElement(import \"message\".$0, $1)",
      name, index);
  }

  string Build(const string& assignment_chain) {
    return strings::Substitute(
      "(import \"protobuf\".build("
        "import \"protobuf\".builder("
          "import \"protofile\".TestAllTypes"
        ")$0"
      "))",
      assignment_chain);
  }

  string NestedBuilder(const string& assignment_chain) {
    return strings::Substitute(
      "(import \"protobuf\".builder("
        "import \"protofile\".TestAllTypes.NestedMessage"
      "))$0",
      assignment_chain);
  }
};

TEST_F(ProtobufTest, HelperFunctions) {
  Value value = ProtobufUtils::WrapMessage(GetMemoryManager(), &message_);
  EXPECT_TRUE(ProtobufUtils::IsMessage(value));
  EXPECT_EQ(&message_, ProtobufUtils::GetMessage(value));

  TestAllTypes* owned_message = new TestAllTypes;
  value = ProtobufUtils::AdoptMessage(GetMemoryManager(), owned_message);
  EXPECT_TRUE(ProtobufUtils::IsMessage(value));
  EXPECT_EQ(owned_message, ProtobufUtils::GetMessage(value));
  // We rely on heapcheck to make sure the message is deleted.

  const google::protobuf::FileDescriptor* test_file = message_.GetDescriptor()->file();
  value = ProtobufUtils::MakeFile(GetMemoryManager(), GetMemoryRoot(),
                                  test_file,
                                  google::protobuf::MessageFactory::generated_factory(),
                                  NULL, NULL);

  EXPECT_TRUE(ProtobufUtils::IsProtoFile(value));
  EXPECT_EQ(test_file, ProtobufUtils::GetFileDescriptor(value));
}

TEST_F(ProtobufTest, ReadFields) {
  ExpectEvaluatesToInteger(101, ReadField("optionalInt32"));
  ExpectEvaluatesToDouble(106, ReadField("optionalDouble"));
  ExpectEvaluatesToBoolean(true, ReadField("optionalBool"));
  ExpectEvaluatesToString("108", ReadField("optionalString"));
  ExpectEvaluatesToAtom("BAR", ReadField("optionalEnum"));
  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes.NestedMessage\", {\n"
    "  a: 110\n"
    "})",
    ReadField("optionalMessage"));
}

TEST_F(ProtobufTest, ReadRepeatedFieldSize) {
  ExpectEvaluatesToInteger(2, ReadRepeatedFieldSize("repeatedInt32"));
  ExpectEvaluatesToInteger(2, ReadRepeatedFieldSize("repeatedBool"));
  ExpectEvaluatesToInteger(2, ReadRepeatedFieldSize("repeatedString"));
  ExpectEvaluatesToInteger(2, ReadRepeatedFieldSize("repeatedEnum"));
  ExpectEvaluatesToInteger(2, ReadRepeatedFieldSize("repeatedMessage"));

  ExpectEvaluatesToInteger(3, ReadRepeatedFieldSize("repeatedDouble"));
  ExpectEvaluatesToInteger(3, ReadRepeatedFieldSize("repeatedFloat"));
}

TEST_F(ProtobufTest, ReadRepeatedFields) {
  ExpectEvaluatesToInteger(201, ReadRepeatedFieldElement("repeatedInt32", 0));
  ExpectEvaluatesToDouble(206, ReadRepeatedFieldElement("repeatedDouble", 0));
  ExpectEvaluatesToBoolean(false, ReadRepeatedFieldElement("repeatedBool", 0));
  ExpectEvaluatesToString("208", ReadRepeatedFieldElement("repeatedString", 0));
  ExpectEvaluatesToAtom("BAZ", ReadRepeatedFieldElement("repeatedEnum", 0));
  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes.NestedMessage\", {\n"
    "  a: 210\n"
    "})",
    ReadRepeatedFieldElement("repeatedMessage", 0));

  ExpectEvaluatesToInteger(301, ReadRepeatedFieldElement("repeatedInt32", 1));
  ExpectEvaluatesToBoolean(true, ReadRepeatedFieldElement("repeatedBool", 1));
  ExpectEvaluatesToString("308", ReadRepeatedFieldElement("repeatedString", 1));
  ExpectEvaluatesToAtom("FOO", ReadRepeatedFieldElement("repeatedEnum", 1));
  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes.NestedMessage\", {\n"
    "  a: 310\n"
    "})",
    ReadRepeatedFieldElement("repeatedMessage", 1));

  ExpectEvaluatesToDouble(305, ReadRepeatedFieldElement("repeatedFloat", 1));
  ExpectEvaluatesToDouble(306, ReadRepeatedFieldElement("repeatedDouble", 1));

  ExpectEvaluatesToDouble(305.5, ReadRepeatedFieldElement("repeatedFloat", 2));

  ExpectEvaluatesToDouble(306.5,
                          ReadRepeatedFieldElement("repeatedDouble", 2));
}

TEST_F(ProtobufTest, ProtobufType) {
  ExpectEvaluatesToDebugString(
    "protobufProtoFile(\"evlan/vm/builtin/protobuf_test.proto\")",
    "import \"protofile\"");

  ExpectEvaluatesToDebugString(
    "protobufMessageType(\"evlan.vm.builtin.TestAllTypes\")",
    "import \"protofile\".TestAllTypes");

  ExpectEvaluatesToDebugString(
    "protobufMessageType(\"evlan.vm.builtin.TestAllTypes.NestedMessage\")",
    "import \"protofile\".TestAllTypes.NestedMessage");

  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes\", {})",
    "import \"protobuf\".defaultInstance("
    "import \"protofile\".TestAllTypes)");
}

TEST_F(ProtobufTest, BuildFields) {
  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes\", {\n"
    "  optional_int32: 101\n"
    "})",
    Build(".optionalInt32(101)"));

  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes\", {\n"
    "  optional_bool: true\n"
    "  optional_string: \"108\"\n"
    "  optional_enum: BAR\n"
    "})",
    Build(".optionalEnum(@BAR)"
          ".optionalBool(true)"
          ".optionalString(\"108\")"));

  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes\", {\n"
    "  optional_message {\n"
    "    a: 110\n"
    "  }\n"
    "})",
    Build(".optionalMessage(" + NestedBuilder(".a(110)") + ")"));
}

TEST_F(ProtobufTest, BuildRepeatedFields) {
  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes\", {\n"
    "  repeated_int32: 201\n"
    "  repeated_int32: 301\n"
    "  repeated_double: 206\n"
    "  repeated_double: 20.5\n"
    "})",
    Build(".repeatedInt32(201)"
          ".repeatedInt32(301)"
          ".repeatedDouble(206)"
          ".repeatedDouble(20.5)"));

  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes\", {\n"
    "  repeated_bool: false\n"
    "  repeated_string: \"208\"\n"
    "  repeated_string: \"308\"\n"
    "  repeated_enum: BAZ\n"
    "  repeated_enum: FOO\n"
    "})",
    Build(".repeatedEnum(@BAZ)"
          ".repeatedString(\"208\")"
          ".repeatedBool(false)"
          ".repeatedString(\"308\")"
          ".repeatedEnum(@FOO)"));

  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestAllTypes\", {\n"
    "  repeated_message {\n"
    "    a: 210\n"
    "  }\n"
    "  repeated_message {\n"
    "    a: 310\n"
    "  }\n"
    "})",
    Build(".repeatedMessage(" + NestedBuilder(".a(210)") + ")"
          ".repeatedMessage(" + NestedBuilder(".a(310)") + ")"));
}

// Test that support code is correctly attached to protocol buffer types.
TEST_F(ProtobufTest, SupportCode) {
  SetSupportCode("type => value => param => {type, value, param}");
  AddImport("emptyMessage",
    ProtobufUtils::WrapMessage(GetMemoryManager(),
                               &TestAllTypes::default_instance()));

  ExpectEvaluatesToDebugString(
    "{@protobuf, protobuf(\"evlan.vm.builtin.TestAllTypes\", {}), @foo}",
    "import \"emptyMessage\".foo");

  ExpectEvaluatesToDebugString(
    "{@protobufRepeated, repeatedField(@repeated_int32), @foo}",
    "import \"emptyMessage\".repeatedInt32.foo");

  ExpectEvaluatesToDebugString(
    "{@protobufBuilder, protobufBuilder(\"evlan.vm.builtin.TestAllTypes\"), "
      "@foo}",
    "import \"protobuf\".builder("
      "import \"protofile\".TestAllTypes).foo");

  ExpectEvaluatesToDebugString(
    "{@protobufType, protobufMessageType(\"evlan.vm.builtin.TestAllTypes\"), "
      "@foo}",
    "import \"protofile\".TestAllTypes.foo");
}

TEST_F(ProtobufTest, Has) {
  TestAllTypes message;
  message.set_optional_int32(123);
  AddImport("partialMessage",
    ProtobufUtils::WrapMessage(GetMemoryManager(), &message));

  ExpectEvaluatesToBoolean(true,
    "import \"protobuf\".has(import \"partialMessage\", @optionalInt32)");
  ExpectEvaluatesToBoolean(false,
    "import \"protobuf\".has(import \"partialMessage\", @optionalFloat)");
}

TEST_F(ProtobufTest, Extensions) {
  TestExtensions message;
  message.MutableExtension(optional_message_extension)->set_optional_int32(123);
  AddImport("extendedMessage",
    ProtobufUtils::WrapMessage(GetMemoryManager(), &message));

  // Test that we can fetch extension identifiers from their enclosing scopes.
  ExpectEvaluatesToDebugString(
    "protobufExtensionIdentifier("
      "\"evlan.vm.builtin.optional_message_extension\")",
    "import \"protofile\".optionalMessageExtension");

  ExpectEvaluatesToDebugString(
    "protobufExtensionIdentifier("
      "\"evlan.vm.builtin.TestAllTypes.nested_extension\")",
    "import \"protofile\".TestAllTypes.nestedExtension");

  // Test hasExtension().
  ExpectEvaluatesToBoolean(false,
    "import \"protobuf\".hasExtension(import \"extendedMessage\", "
      "import \"protofile\".optionalInt32Extension)");

  ExpectEvaluatesToBoolean(true,
    "import \"protobuf\".hasExtension(import \"extendedMessage\", "
      "import \"protofile\".optionalMessageExtension)");

  // Test reading an extension.
  ExpectEvaluatesToInteger(123,
    "import \"protobuf\".extension(import \"extendedMessage\", "
      "import \"protofile\".optionalMessageExtension).optionalInt32");

  // Test setting an extension.
  ExpectEvaluatesToDebugString(
    "protobuf(\"evlan.vm.builtin.TestExtensions\", {\n"
    "  [evlan.vm.builtin.optional_int32_extension]: 123\n"
    "})",
    "(import \"protobuf\".build("
      "import \"protobuf\".setExtension("
        "import \"protobuf\".builder("
          "import \"protofile\".TestExtensions"
        "),"
        "import \"protofile\".optionalInt32Extension,"
        "123"
      ")"
    "))");
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan
