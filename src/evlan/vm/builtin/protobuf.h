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
// This module implements glue which allows Evlan code to manipulate C++
// protocol message objects.  This makes it easy to pass protocol messages
// between C++ and Evlan code without having to serialize the data and then
// parse it again in the other language.
//
// Even though we're manipulating C++ objects, however, Evlan code is still
// prohibited from creating side-effects.  So, writing messages from Evlan code
// uses a builder pattern similar to Proto2-Java.
//
// Details:
//
// Reading required or optional fields works exactly like you'd expect:  you
// simply access them like members.
//   message.foo  # Read the message's field "foo".
//
// Note that field names with underscores are converted to camel-case with a
// lower-case first letter to match Evlan style.  For example, if your message
// has fields named "foo_bar" and "BazQux", you could access them as:
//   message.fooBar     # Read field "foo_bar".
//   message.bazQux     # Read field "BazQux".
//
// Repeated fields work like arrays:
//   message.bar.size   # Get the size of the field "bar".
//   message.bar[15]    # Get element 15 of "bar".
//
// Enums are represented using atoms:
//   message.baz == @ENUM_VALUE
//
// To build a new message, first you need to obtain the message type.
// A message type can be obtained as so:
//   MyFile = import "path/to/my/proto/file.proto"
//   MyMessage = MyFile.MyMessage
//
// Once you have the type, you can obtain a builder for it:
//   builder = MyMessage.builder
//
// The builder has methods matching all of the message type's fields.  Each
// method takes the value of that field as input and returns a new builder.
// Thus, to set the fields of a message, you simply chain calls together like
// so:
//   message = builder.foo(123)
//                    .bar("blah")
//                    .bar("blah2")
//                    .baz(@ANOTHER_VALUE)
//                    .build()
// The final "build()" call returns the completed message.  Note how repeated
// fields ("bar" in this case) are filled in by calling the method multiple
// times, once for each value.
//
// Note:  The examples above are not completely implemented by this module;
//   they also require that the necessary Evlan support code and import resolver
//   have been loaded.
//
//   See support.h for details about support code and public/import_resolver.h
//   for details on import resolution.

#ifndef EVLAN_VM_PROTOBUF_H__
#define EVLAN_VM_PROTOBUF_H__

#include "evlan/common.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/vm/runtime.h"
#include "evlan/stringpiece.h"

class Mutex;

namespace google {
  namespace protobuf {
    class Message;
    class MessageFactory;
    class Descriptor;
    class FileDescriptor;
    class FieldDescriptor;
    class DescriptorPool;
    class Reflection;
  }
}

namespace evlan {
namespace vm {
namespace builtin {

// Utilities for dealing with protocol buffers.  (See file documentation,
// above.)
class ProtobufUtils {
 public:
  // Is this value a protocol message?
  inline static bool IsMessage(const Value& value);

  // Get the underlying Message object for a protocol message Value.
  inline static const google::protobuf::Message* GetMessage(const Value& value);

  // Is this value a protocol message file?
  inline static bool IsProtoFile(const Value& value);

  // Gets the underlying FileDescriptor object for a protocol message file
  // Value.
  inline static const google::protobuf::FileDescriptor* GetFileDescriptor(
      const Value& value);

  // Returns an Evlan value wrapping the given Message object.  The Message
  // remains property of the caller and must not be destroyed until the VM
  // shuts down.  Furthermore, the message must not be modified while it is
  // reachable from Evlan code, since any modification would violate the ban
  // on side-effects.
  static Value WrapMessage(memory::MemoryManager* memory_manager,
                           const google::protobuf::Message* message);

  // Like WrapMessage() but takes ownership.
  static Value AdoptMessage(memory::MemoryManager* memory_manager,
                            const google::protobuf::Message* message);

  // Returns an Evlan value representing a given .proto file. The |factory|
  // is the MessageFactory which can be used to construct instances of the
  // messages found in the file. |factory_lock| is a lock that will be used
  // when invoking the factory to create instances and may be NULL if no
  // such lock is needed. The |tracker| is used to track any pointers which
  // are garbage collected or NULL if they are permanent.
  static Value MakeFile(memory::MemoryManager* memory_manager,
                        const memory::MemoryRoot* root,
                        const google::protobuf::FileDescriptor* file,
                        google::protobuf::MessageFactory* factory,
                        Mutex* factory_lock,
                        memory::ObjectTracker* tracker);

  // Get Logics for various types of objects related to protocol buffers.
  // (These are mainly only here so that support.cc can call them.)
  inline static const Logic* GetMessageLogic();
  inline static const Logic* GetRepeatedFieldLogic();
  inline static const Logic* GetBuilderLogic();
  inline static const Logic* GetMessageTypeLogic();

  // Returns a Logic for an object containing various functions that operate on
  // protocol buffers.  This Logic completely ignores its associated Data.
  static const Logic* GetBuiltinModule();

 private:
  // Wraps a google::protobuf::Message in a TrackedObject so that it can be
  // garbage-collected.
  class TrackedMessage : public memory::TrackedObject {
   public:
    // Construct a TrackedMessage around a message.  The TrackedMessage is
    // tracked by the given MemoryManager.  |descriptor_tracker|, if given,
    // is an ObjectTracker which encapsulates the message's Descriptor.  The
    // Descriptor must outlive the Message itself, so if the Descriptor is not
    // permanent then a tracker must be provided for it.  The TrackedMessage
    // takes ownership of the underlying Message object, though this can be
    // undone by calling SetOwnsMessage(false).  The TrackedMessage will delete
    // itself when NotReachable() is called.
    TrackedMessage(const google::protobuf::Message* message,
                   memory::MemoryManager* memory_manager,
                   memory::ObjectTracker* descriptor_tracker = NULL);

    // Creates a TrackedMessage for a message embedded inside some other
    // tracked message.  The parent owns the message, but this tracker will
    // hold a reference to the parent so that it doesn't go away.
    TrackedMessage(const google::protobuf::Message* message,
                   const TrackedMessage* parent,
                   memory::MemoryManager* memory_manager);

    // Construct a TrackedMessage around a message.  The TrackedMessage
    // is, actually, *not* tracked by any MemoryManager -- it is up to the
    // caller to delete the TrackedMessage once it is known that it is no
    // longer needed.  |message| is not owned by the TrackedMessage; it must
    // remain valid until the TrackedMessage is destroyed.
    TrackedMessage(const google::protobuf::Message* message);

    ~TrackedMessage();

    inline const google::protobuf::Message* GetMessage() const { return message_; }

    inline memory::ObjectTracker* GetDescriptorTracker() const {
      return descriptor_tracker_;
    }

    // Call SetOwnsMessage(true) to tell the TrackedMessage to delete the
    // underlying Message object when it is destroyed.
    inline void SetOwnsMessage(bool owned) { owns_message_ = owned; }

    // If this object is tracked, visit its ObjectTracker.
    inline void VisitTracker(memory::MemoryVisitor* visitor) const {
      if (tracker_ != NULL) visitor->VisitTrackedObject(tracker_);
    }

    // implements TrackedObject --------------------------------------

    void NotReachable();
    void Accept(memory::MemoryVisitor* visitor) const;

   private:
    const google::protobuf::Message* message_;
    bool owns_message_;
    memory::ObjectTracker* tracker_;
    memory::ObjectTracker* descriptor_tracker_;
    const TrackedMessage* parent_;
  };

  // Logic for protocol message objects.  The corresponding Data is a pointer
  // to a TrackedMessage.
  class MessageLogic : public Logic {
   public:
    MessageLogic();
    ~MessageLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  // Data corresponding to RepeatedFieldLogic.
  struct RepeatedFieldData {
    const TrackedMessage* message;
    const google::protobuf::FieldDescriptor* field;

    int Accept(memory::MemoryVisitor* visitor) const;
  };

  // Logic for a repeated protocol message field.  Similar to an array.
  class RepeatedFieldLogic : public Logic {
   public:
    RepeatedFieldLogic();
    ~RepeatedFieldLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  // -----------------------------------------------------------------
  // Builder stuff.

  // A linked list of fields and corresponding values to which they are to be
  // set.  Since side-effects aren't allowed, a protobuf builder cannot simply
  // keep a Message object internally and set its fields as it goes along.
  // Instead, it must keep a list of all the field values assigned and then,
  // when the message is finally built, go and set them all.  In other words,
  // when you have code like:
  //   myBuilder.foo(123).bar("abc").baz(@blah)
  // The returned builder contains an AssignmentList containing the assignments
  // foo = 123, bar = "abc", and baz = @blah.  Actually, the first element in
  // the list will be the last one that appeared in the code, since
  // side-effect-free semantics mean that we can only add to the front of the
  // list, not the back.
  struct AssignmentList {
    const google::protobuf::FieldDescriptor* field;
    Value value;
    const AssignmentList* rest;  // NULL if there are no more elements.

    int Accept(memory::MemoryVisitor* visitor) const;
  };

  // Data corresponding to BuilderLogic.
  struct BuilderData {
    // The default instance of the type.
    const TrackedMessage* default_instance;

    // Assignments which should be applied to the message.
    const AssignmentList* assignment_list;

    int Accept(memory::MemoryVisitor* visitor) const;
  };

  // Logic for a protobuf builder.
  class BuilderLogic : public Logic {
   public:
    BuilderLogic();
    ~BuilderLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  // Data corresponding to FieldBuilderLogic.
  struct FieldBuilderData {
    const BuilderData* builder_data;
    const google::protobuf::FieldDescriptor* field;

    int Accept(memory::MemoryVisitor* visitor) const;
  };

  // Logic for a field builder.  This is what is returned when you say e.g.
  // "builder.foo", where foo is a field of the message.  Applying a field
  // builder to a value assigns that value to the field and returns a new
  // builder (for the message).
  class FieldBuilderLogic : public Logic {
   public:
    FieldBuilderLogic();
    ~FieldBuilderLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  // -----------------------------------------------------------------
  // ProtoFile.

  // Data corresponding to a ProtoFileLogic.
  struct ProtoFileData {
    const google::protobuf::FileDescriptor* file;
    google::protobuf::MessageFactory* factory;

    // Protects calls to factory->GetDefaultInstance().  May be NULL if the
    // factory implementation is known to be reentrant.
    Mutex* factory_mutex;

    // ObjectTracker for the MessageFactory and FileDescriptor.  May be NULL if
    // both objects are permanent.
    memory::ObjectTracker* tracker;

    int Accept(memory::MemoryVisitor* visitor) const;
  };

  // Logic for a proto file, which is an object whose members are the top-level
  // message types and extension identifiers defined in the file.
  class ProtoFileLogic : public Logic {
   public:
    ProtoFileLogic();
    ~ProtoFileLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  // Data corresponding to MessageTypeLogic.
  struct MessageTypeData {
    // The proto file that this came from.
    const ProtoFileData* proto_file;

    // Descriptor for the type.
    const google::protobuf::Descriptor* descriptor;

    int Accept(memory::MemoryVisitor* visitor) const;
  };

  // Logic for a message type.  Nested types and extensions are members of
  // this value.
  class MessageTypeLogic : public Logic {
   public:
    MessageTypeLogic();
    ~MessageTypeLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  // Data corresponding to ExtensionIdentifierLogic.
  struct ExtensionIdentifierData {
    // The proto file that this came from.
    const ProtoFileData* proto_file;

    // Descriptor for the extension.
    const google::protobuf::FieldDescriptor* descriptor;

    int Accept(memory::MemoryVisitor* visitor) const;
  };

  // Logic for an extension identifier, which can be passed to the
  // extension() and hasExtension() methods of protocol message objects.
  class ExtensionIdentifierLogic : public Logic {
   public:
    ExtensionIdentifierLogic();
    ~ExtensionIdentifierLogic();

    // implements Logic ----------------------------------------------
    void Run(Context* context) const;
    void Accept(const Data* data,
                memory::MemoryVisitor* visitor) const;
    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const;
  };

  // -----------------------------------------------------------------

  // Helper functions.  (Documented in the .cc file.)
  static const google::protobuf::FieldDescriptor* FindField(
    const google::protobuf::Descriptor* descriptor,
    const Value& id);
  static void ReturnField(Context* context,
                          const TrackedMessage* tracked_message,
                          const google::protobuf::Message& message,
                          const google::protobuf::FieldDescriptor* field);
  static void ReturnFieldValue(Context* context,
                               const TrackedMessage* tracked_message,
                               const google::protobuf::Message& message,
                               const google::protobuf::Reflection* reflection,
                               const google::protobuf::FieldDescriptor* field,
                               int index = -1);

  static void SetField(Context* context,
                       const BuilderData* builder_data,
                       const google::protobuf::FieldDescriptor* field,
                       memory::ObjectTracker* field_tracker,
                       Value value,
                       const FieldBuilderData* field_builder_data);

  static bool ApplyAssignmentList(Context* context,
                                  const AssignmentList* list,
                                  google::protobuf::Message* message);
  static void WithdrawBuilderData(const BuilderData* builder,
                                  memory::MemoryManager* memory_manager);

  static TrackedMessage* GetDefaultInstanceAsTrackedMessage(
    Context* context, Value type);

  static void HasHelper(Context* context,
                        const google::protobuf::Message* message,
                        const google::protobuf::FieldDescriptor* field);

  // Builtin functions.  (Documented in the .cc file.)
  static void GetSize(Context* context, Value repeated_field);
  static void GetElement(Context* context, Value repeated_field, int index);
  static void Build(Context* context, Value builder);
  static void DefaultInstance(Context* context, Value type);
  static void Builder(Context* context, Value type);
  static void HasField(Context* context,
                       const google::protobuf::Message* message,
                       Value field_name);
  static void HasExtension(Context* context,
                           const google::protobuf::Message* message,
                           Value extension_identifier);
  static void GetExtension(Context* context,
                           Value message_value,
                           Value extension_identifier);
  static void SetExtension(Context* context,
                           Value builder_value,
                           Value extension_identifier,
                           Value value);

  static const Logic* NewBuiltinModule();

  static const MessageLogic kLogic;
  static const RepeatedFieldLogic kRepeatedLogic;
  static const BuilderLogic kBuilderLogic;
  static const FieldBuilderLogic kFieldBuilderLogic;
  static const ProtoFileLogic kProtoFileLogic;
  static const MessageTypeLogic kMessageTypeLogic;
  static const ExtensionIdentifierLogic kExtensionIdentifierLogic;
};

// ===================================================================

inline bool ProtobufUtils::IsMessage(const Value& value) {
  return value.GetLogic() == &kLogic;
}

inline bool ProtobufUtils::IsProtoFile(const Value& value) {
  return value.GetLogic() == &kProtoFileLogic;
}

inline const google::protobuf::Message* ProtobufUtils::GetMessage(const Value& value) {
  GOOGLE_DCHECK(IsMessage(value));
  return value.GetData().As<TrackedMessage>()->GetMessage();
}

inline const google::protobuf::FileDescriptor* ProtobufUtils::GetFileDescriptor(
    const Value& value) {
  GOOGLE_DCHECK(IsProtoFile(value));
  return value.GetData().As<ProtoFileData>()->file;
}

inline const Logic* ProtobufUtils::GetMessageLogic() {
  return &kLogic;
}

inline const Logic* ProtobufUtils::GetRepeatedFieldLogic() {
  return &kRepeatedLogic;
}

inline const Logic* ProtobufUtils::GetBuilderLogic() {
  return &kBuilderLogic;
}

inline const Logic* ProtobufUtils::GetMessageTypeLogic() {
  return &kMessageTypeLogic;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_PROTOBUF_H__
