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
#include "evlan/vm/builtin/native.h"
#include "evlan/vm/builtin/double.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/atom.h"
#include "evlan/vm/builtin/support.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/io/printer.h>
#include "evlan/common.h"
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace builtin {

ProtobufUtils::TrackedMessage::TrackedMessage(
    const google::protobuf::Message* message,
    memory::MemoryManager* memory_manager,
    memory::ObjectTracker* descriptor_tracker)
  : message_(message),
    owns_message_(true),
    descriptor_tracker_(descriptor_tracker),
    parent_(NULL) {
  tracker_ = memory_manager->AddTrackedObject(this);
}

ProtobufUtils::TrackedMessage::TrackedMessage(
    const google::protobuf::Message* message,
    const TrackedMessage* parent,
    memory::MemoryManager* memory_manager)
  : message_(message),
    owns_message_(false),
    descriptor_tracker_(NULL),
    parent_(parent) {
  // We really only care about the top-level parent staying around.
  if (parent_->parent_ != NULL) {
    parent_ = parent_->parent_;
  }
  tracker_ = memory_manager->AddTrackedObject(this);
}

ProtobufUtils::TrackedMessage::TrackedMessage(const google::protobuf::Message* message)
  : message_(message),
    owns_message_(false),
    tracker_(NULL),
    descriptor_tracker_(NULL),
    parent_(NULL) {
}

ProtobufUtils::TrackedMessage::~TrackedMessage() {
  if (owns_message_) delete message_;
}

void ProtobufUtils::TrackedMessage::NotReachable() {
  delete this;
}

void ProtobufUtils::TrackedMessage::Accept(
    memory::MemoryVisitor* visitor) const {
  if (descriptor_tracker_ != NULL) {
    visitor->VisitTrackedObject(descriptor_tracker_);
  }
  if (parent_ != NULL) {
    parent_->VisitTracker(visitor);
  }
}

Value ProtobufUtils::WrapMessage(memory::MemoryManager* memory_manager,
                                 const google::protobuf::Message* message) {
  TrackedMessage* tracked_message = new TrackedMessage(message, memory_manager);
  tracked_message->SetOwnsMessage(false);
  return Value(&kLogic, Data(tracked_message));
}

Value ProtobufUtils::AdoptMessage(memory::MemoryManager* memory_manager,
                                  const google::protobuf::Message* message) {
  TrackedMessage* tracked_message = new TrackedMessage(message, memory_manager);
  return Value(&kLogic, Data(tracked_message));
}

// ===================================================================
// Messages

const ProtobufUtils::MessageLogic ProtobufUtils::kLogic;

ProtobufUtils::MessageLogic::MessageLogic() {}
ProtobufUtils::MessageLogic::~MessageLogic() {}

void ProtobufUtils::MessageLogic::Run(Context* context) const {
  const google::protobuf::Message* message =
    context->GetData().As<TrackedMessage>()->GetMessage();

  const google::protobuf::FieldDescriptor* field =
    FindField(message->GetDescriptor(), context->GetParameter());

  if (field == NULL) {
    context->GetSupportCodeManager()
           ->GetSupportCode(SupportCodeManager::PROTOBUF)
           ->Run(context);
    return;
  }

  ReturnField(context, context->GetData().As<TrackedMessage>(),
              *message, field);
}

void ProtobufUtils::MessageLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  data->As<TrackedMessage>()->VisitTracker(visitor);
}

void ProtobufUtils::MessageLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  const google::protobuf::Message* message = data.As<TrackedMessage>()->GetMessage();
  printer->Print("protobuf(\"$type$\", {",
                 "type", message->GetDescriptor()->full_name());
  string proto_debug_string = message->DebugString();

  if (!proto_debug_string.empty()) {
    printer->Print("\n");
    printer->Indent();

    // google::protobuf::io::Printer currently does not inject indents within a string
    // parameter, so we must break it up into lines and print them individually
    // to make sure they are correctly indented.
    // TODO(kenton):  Modify the google::protobuf::TextFormat interface so that a Printer
    //   can be passed directly to it.
    vector<string> lines;
    SplitStringUsing(proto_debug_string, "\n", &lines);
    for (int i = 0; i < lines.size(); i++) {
      printer->Print("$line$\n", "line", lines[i]);
    }

    printer->Outdent();
  }

  printer->Print("})");
}

// -------------------------------------------------------------------
// Repeated fields

const ProtobufUtils::RepeatedFieldLogic ProtobufUtils::kRepeatedLogic;

int ProtobufUtils::RepeatedFieldData::Accept(
    memory::MemoryVisitor* visitor) const {
  message->VisitTracker(visitor);
  return sizeof(*this);
}

ProtobufUtils::RepeatedFieldLogic::RepeatedFieldLogic() {}
ProtobufUtils::RepeatedFieldLogic::~RepeatedFieldLogic() {}

void ProtobufUtils::RepeatedFieldLogic::Run(Context* context) const {
  context->GetSupportCodeManager()
         ->GetSupportCode(SupportCodeManager::PROTOBUF_REPEATED)
         ->Run(context);
}

void ProtobufUtils::RepeatedFieldLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  data->AcceptAs<RepeatedFieldData>(visitor);
}

void ProtobufUtils::RepeatedFieldLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  printer->Print("repeatedField(@$name$)",
                 "name", data.As<RepeatedFieldData>()->field->name());
}

// ===================================================================
// Builders

const ProtobufUtils::BuilderLogic ProtobufUtils::kBuilderLogic;

int ProtobufUtils::AssignmentList::Accept(
    memory::MemoryVisitor* visitor) const {
  // The FieldDescriptor is a non-tracked pointer.
  value.Accept(visitor);
  if (rest != NULL) {
    visitor->VisitPointer(&rest);
  }
  return sizeof(*this);
}

int ProtobufUtils::BuilderData::Accept(memory::MemoryVisitor* visitor) const {
  default_instance->VisitTracker(visitor);
  if (assignment_list != NULL) {
    visitor->VisitPointer(&assignment_list);
  }
  return sizeof(*this);
}

ProtobufUtils::BuilderLogic::BuilderLogic() {}
ProtobufUtils::BuilderLogic::~BuilderLogic() {}

void ProtobufUtils::BuilderLogic::Run(Context* context) const {
  const BuilderData* builder_data = context->GetData().As<BuilderData>();
  const google::protobuf::Message* message = builder_data->default_instance->GetMessage();

  const google::protobuf::FieldDescriptor* field =
    FindField(message->GetDescriptor(), context->GetParameter());

  if (field == NULL) {
    context->GetSupportCodeManager()
           ->GetSupportCode(SupportCodeManager::PROTOBUF_BUILDER)
           ->Run(context);
  } else {
    FieldBuilderData* field_builder_data =
      context->Allocate<FieldBuilderData>();
    field_builder_data->builder_data = context->GetData().As<BuilderData>();
    field_builder_data->field = field;
    context->Return(Value(&kFieldBuilderLogic, Data(field_builder_data)));
  }
}

void ProtobufUtils::BuilderLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  data->AcceptAs<BuilderData>(visitor);
}

void ProtobufUtils::BuilderLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  printer->Print("protobufBuilder(\"$name$\")",
                 "name", data.As<BuilderData>()->default_instance
                            ->GetMessage()->GetDescriptor()->full_name());
}

// -------------------------------------------------------------------
// Field builders

const ProtobufUtils::FieldBuilderLogic ProtobufUtils::kFieldBuilderLogic;

int ProtobufUtils::FieldBuilderData::Accept(
    memory::MemoryVisitor* visitor) const {
  visitor->VisitPointer(&builder_data);
  // The FieldDescriptor is a non-tracked pointer.
  return sizeof(*this);
}

ProtobufUtils::FieldBuilderLogic::FieldBuilderLogic() {}
ProtobufUtils::FieldBuilderLogic::~FieldBuilderLogic() {}

void ProtobufUtils::FieldBuilderLogic::Run(Context* context) const {
  const FieldBuilderData* field_builder_data =
    context->GetData().As<FieldBuilderData>();

  SetField(context,
           field_builder_data->builder_data,
           field_builder_data->field, NULL,
           context->GetParameter(),
           field_builder_data);
}

void ProtobufUtils::FieldBuilderLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  data->AcceptAs<FieldBuilderData>(visitor);
}

void ProtobufUtils::FieldBuilderLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  const FieldBuilderData* field_builder_data = data.As<FieldBuilderData>();
  printer->Print("protobufFieldBuilder(\"$name$\", @$field$)",
                 "name", field_builder_data->field
                           ->containing_type()->full_name(),
                 "field", field_builder_data->field->name());
}

// ===================================================================
// ProtoFile

const ProtobufUtils::ProtoFileLogic ProtobufUtils::kProtoFileLogic;

int ProtobufUtils::ProtoFileData::Accept(
    memory::MemoryVisitor* visitor) const {
  if (tracker != NULL) {
    visitor->VisitTrackedObject(tracker);
  }
  return sizeof(*this);
}

ProtobufUtils::ProtoFileLogic::ProtoFileLogic() {}
ProtobufUtils::ProtoFileLogic::~ProtoFileLogic() {}

void ProtobufUtils::ProtoFileLogic::Run(Context* context) const {
  if (!IsAtom(context->GetParameter())) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to protobuf file.  Expected atom, got: " +
      context->GetParameter().DebugString()));
    return;
  }

  string name = GetAtomName(context->GetParameter().GetData()).as_string();
  const google::protobuf::FileDescriptor* file =
    context->GetData().As<ProtoFileData>()->file;

  // Look for a message type by this name.
  const google::protobuf::Descriptor* descriptor = file->FindMessageTypeByName(name);

  if (descriptor != NULL) {
    MessageTypeData* message_type_data = context->Allocate<MessageTypeData>();
    message_type_data->proto_file = context->GetData().As<ProtoFileData>();
    message_type_data->descriptor = descriptor;
    context->Return(Value(&kMessageTypeLogic, Data(message_type_data)));
    return;
  }

  // No message type found.  Try looking for an extension instead.
  const google::protobuf::FieldDescriptor* extension =
    file->FindExtensionByCamelcaseName(name);

  if (extension != NULL) {
    ExtensionIdentifierData* extension_data =
      context->Allocate<ExtensionIdentifierData>();
    extension_data->proto_file = context->GetData().As<ProtoFileData>();
    extension_data->descriptor = extension;
    context->Return(Value(&kExtensionIdentifierLogic, Data(extension_data)));
    return;
  }

  // Not a message type or an extension type.
  context->Return(TypeErrorUtils::MakeError(context,
    "Proto file has no such member: " + name));
}

void ProtobufUtils::ProtoFileLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  data->AcceptAs<ProtoFileData>(visitor);
}

void ProtobufUtils::ProtoFileLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  printer->Print("protobufProtoFile(\"$type$\")",
                 "type", data.As<ProtoFileData>()->file->name());
}

Value ProtobufUtils::MakeFile(memory::MemoryManager* memory_manager,
                              const memory::MemoryRoot* root,
                              const google::protobuf::FileDescriptor* file,
                              google::protobuf::MessageFactory* factory,
                              Mutex* factory_lock,
                              memory::ObjectTracker* tracker) {
  ProtobufUtils::ProtoFileData* file_data =
      memory_manager->Allocate<ProtobufUtils::ProtoFileData>(root);

  file_data->file = file;
  file_data->factory = factory;
  file_data->factory_mutex = factory_lock;
  file_data->tracker = tracker;

  return Value(&kProtoFileLogic, Data(file_data));
}

// -------------------------------------------------------------------
// Message types

const ProtobufUtils::MessageTypeLogic ProtobufUtils::kMessageTypeLogic;

int ProtobufUtils::MessageTypeData::Accept(
    memory::MemoryVisitor* visitor) const {
  visitor->VisitPointer(&proto_file);

  // The Descriptor is a non-tracked pointer.
  return sizeof(*this);
}

ProtobufUtils::MessageTypeLogic::MessageTypeLogic() {}
ProtobufUtils::MessageTypeLogic::~MessageTypeLogic() {}

void ProtobufUtils::MessageTypeLogic::Run(Context* context) const {
  if (IsAtom(context->GetParameter())) {
    string name = GetAtomName(context->GetParameter().GetData()).as_string();

    const google::protobuf::Descriptor* descriptor =
      context->GetData().As<MessageTypeData>()->descriptor;

    // Is it a nested type?
    const google::protobuf::Descriptor* nested_type =
      descriptor->FindNestedTypeByName(name);

    if (nested_type != NULL) {
      MessageTypeData* nested_type_data = context->Allocate<MessageTypeData>();
      nested_type_data->proto_file =
        context->GetData().As<MessageTypeData>()->proto_file;
      nested_type_data->descriptor = nested_type;

      context->Return(Value(&kMessageTypeLogic, Data(nested_type_data)));
      return;
    }

    // No nested type found.  Try looking for a nested extension instead.
    const google::protobuf::FieldDescriptor* extension =
      descriptor->FindExtensionByCamelcaseName(name);

    if (extension != NULL) {
      ExtensionIdentifierData* extension_data =
        context->Allocate<ExtensionIdentifierData>();
      extension_data->proto_file =
        context->GetData().As<MessageTypeData>()->proto_file;
      extension_data->descriptor = extension;
      context->Return(Value(&kExtensionIdentifierLogic, Data(extension_data)));
      return;
    }
  }

  // Not a nested type or extension.  Fall back to the support code.
  context->GetSupportCodeManager()
         ->GetSupportCode(SupportCodeManager::PROTOBUF_TYPE)
         ->Run(context);
}

void ProtobufUtils::MessageTypeLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  data->AcceptAs<MessageTypeData>(visitor);
}

void ProtobufUtils::MessageTypeLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  const MessageTypeData* message_type_data = data.As<MessageTypeData>();
  printer->Print("protobufMessageType(\"$name$\")",
                 "name", message_type_data->descriptor->full_name());
}

// -------------------------------------------------------------------
// Extension identifiers

const ProtobufUtils::ExtensionIdentifierLogic
  ProtobufUtils::kExtensionIdentifierLogic;

int ProtobufUtils::ExtensionIdentifierData::Accept(
    memory::MemoryVisitor* visitor) const {
  visitor->VisitPointer(&proto_file);

  // The FieldDescriptor is a non-tracked pointer.
  return sizeof(*this);
}

ProtobufUtils::ExtensionIdentifierLogic::ExtensionIdentifierLogic() {}
ProtobufUtils::ExtensionIdentifierLogic::~ExtensionIdentifierLogic() {}

void ProtobufUtils::ExtensionIdentifierLogic::Run(Context* context) const {
  const ExtensionIdentifierData* extension_data =
    context->GetData().As<ExtensionIdentifierData>();
  context->Return(TypeErrorUtils::MakeError(context,
    "Tried to invoke protobuf extension identifier \"" +
    extension_data->descriptor->full_name() + "\" as a function."));
}

void ProtobufUtils::ExtensionIdentifierLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  data->AcceptAs<ExtensionIdentifierData>(visitor);
}

void ProtobufUtils::ExtensionIdentifierLogic::DebugPrint(
    Data data, int depth,
    google::protobuf::io::Printer* printer) const {
  const ExtensionIdentifierData* extension_data =
    data.As<ExtensionIdentifierData>();
  printer->Print("protobufExtensionIdentifier(\"$name$\")",
                 "name", extension_data->descriptor->full_name());
}

// ===================================================================
// Builtin functions and helpers

// Look up a FieldDescriptor within a Descriptor, identified by an atom Value.
const google::protobuf::FieldDescriptor* ProtobufUtils::FindField(
    const google::protobuf::Descriptor* descriptor,
    const Value& id) {
  if(IsAtom(id)) {
    string name = GetAtomName(id.GetData()).as_string();
    return descriptor->FindFieldByCamelcaseName(name);
  } else {
    return NULL;
  }
}

// Helper function for GetExtension() as well as MessageLogic::Run().
// Extract the value of the given field and return it with context->Return().
// If the field is a repeated field, a repeated field wrapper will be returned.
void ProtobufUtils::ReturnField(Context* context,
                                const TrackedMessage* tracked_message,
                                const google::protobuf::Message& message,
                                const google::protobuf::FieldDescriptor* field) {
  if (field->is_repeated()) {
    RepeatedFieldData* repeated_field_data =
      context->Allocate<RepeatedFieldData>();
    repeated_field_data->message = tracked_message;
    repeated_field_data->field = field;
    context->Return(Value(&kRepeatedLogic, Data(repeated_field_data)));
    return;
  }

  ReturnFieldValue(context, tracked_message, message,
                   message.GetReflection(), field);
}

// Helper function for GetElement() and ReturnField().
// Extract the value of the given field and return it with context->Return().
// |index| should only be specified for repeated fields.
void ProtobufUtils::ReturnFieldValue(Context* context,
                                     const TrackedMessage* tracked_message,
                                     const google::protobuf::Message& message,
                                     const google::protobuf::Reflection* reflection,
                                     const google::protobuf::FieldDescriptor* field,
                                     int index) {
  GOOGLE_DCHECK_EQ(field->is_repeated(), index != -1);

  Value result;
  bool is_repeated = field->is_repeated();

  switch (field->cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      result = MakeInteger(is_repeated ?
        reflection->GetRepeatedInt32(message, field, index) :
        reflection->GetInt32        (message, field));
      break;

    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      result = MakeDouble(is_repeated ?
        reflection->GetRepeatedFloat(message, field, index) :
        reflection->GetFloat        (message, field));
      break;

    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      result = MakeDouble(is_repeated ?
        reflection->GetRepeatedDouble(message, field, index) :
        reflection->GetDouble        (message, field));
      break;

    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
      result = BooleanUtils::MakeBoolean(is_repeated ?
        reflection->GetRepeatedBool(message, field, index) :
        reflection->GetBool        (message, field));
      break;

    case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
      // TODO(kenton):  Somehow alias the original string?
      string scratch;
      const string& value = is_repeated ?
        reflection->GetRepeatedStringReference(
          message, field, index, &scratch) :
        reflection->GetStringReference(message, field, &scratch);
      result = ByteUtils::CopyString(context->GetMemoryManager(),
                                     context, value);
      break;
    }

    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
      TrackedMessage* child_tracked_message =
        new TrackedMessage(
          is_repeated ?
            &reflection->GetRepeatedMessage(message, field, index) :
            &reflection->GetMessage        (message, field),
          tracked_message,
          context->GetMemoryManager());
      result = Value(&kLogic, Data(child_tracked_message));
      break;
    }

    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
      const google::protobuf::EnumValueDescriptor* enum_value = is_repeated ?
        reflection->GetRepeatedEnum(message, field, index) :
        reflection->GetEnum        (message, field);
      result = context->GetAtomRegistry()->GetAtom(
        context->GetMemoryManager(), enum_value->name());
      break;
    }

    default:
      // TODO(kenton):  Implement other field types.
      context->Return(TypeErrorUtils::MakeError(context,
        "Field type not implemented for: " + field->full_name()));
      return;
  }

  context->Return(result);
}

// Helper for FieldBuilderLogic::Run() and SetExtension().  Sets a field in
// the given builder and returns (via the Context) the new builder.
void ProtobufUtils::SetField(
    Context* context,
    // BuilderData for the builder in which the field is to be set.
    const BuilderData* builder_data,
    // The field to be set.
    const google::protobuf::FieldDescriptor* field,
    // ObjectTracker which owns the FieldDescriptor.  May be NULL.  This is
    // important when extensions are in use.  For regular fields, this can
    // always be NULL because the FieldDescriptor is already reachable from
    // builder_data.
    memory::ObjectTracker* field_tracker,
    // The value to assign to the field.
    Value value,
    // If SetField() is being called from FieldBuilderLogic::Run(), this is
    // the FieldBuilderData.  Otherwise, NULL.  This is only here so that it
    // can be Withdraw()n from the MemoryManager.
    const FieldBuilderData* field_builder_data) {

  // Create a MemoryRoot containing all the information we need to keep around,
  // so that we can allocate some memory.
  class AssignmentListRoot : public memory::MemoryRoot {
   public:
    const TrackedMessage* default_instance_;
    const AssignmentList* assignment_list_;
    Value new_value_;
    Value callback_;
    memory::ObjectTracker* field_tracker_;

    // implements MemoryRoot -----------------------------------------
    void Accept(memory::MemoryVisitor* visitor) const {
      default_instance_->VisitTracker(visitor);
      if (assignment_list_ != NULL) {
        visitor->VisitPointer(&assignment_list_);
      }
      new_value_.Accept(visitor);
      callback_.Accept(visitor);
      if (field_tracker_ != NULL) {
        visitor->VisitTrackedObject(field_tracker_);
      }
    }
  };
  AssignmentListRoot root;

  root.default_instance_ = builder_data->default_instance;
  root.assignment_list_ = builder_data->assignment_list;
  root.new_value_ = value;
  root.callback_ = context->GetCallback();
  root.field_tracker_ = field_tracker;

  memory::MemoryManager* memory_manager = context->GetMemoryManager();

  // We don't need these anymore.
  if (field_builder_data != NULL) {
    memory_manager->TryWithdraw(field_builder_data);
  }
  memory_manager->TryWithdraw(builder_data);

  // Allocate new stuff!
  AssignmentList* new_assignment_list =
    memory_manager->Allocate<AssignmentList>(&root);
  new_assignment_list->field = field;
  new_assignment_list->value = root.new_value_;
  new_assignment_list->rest = root.assignment_list_;
  root.assignment_list_ = new_assignment_list;

  BuilderData* new_builder_data = memory_manager->Allocate<BuilderData>(&root);
  new_builder_data->default_instance = root.default_instance_;
  new_builder_data->assignment_list = root.assignment_list_;

  context->SetNext(root.callback_,
                   Value(&kBuilderLogic, Data(new_builder_data)));
}

// Get the size of a repeated field.
void ProtobufUtils::GetSize(Context* context, Value repeated_field) {
  if (repeated_field.GetLogic() != &kRepeatedLogic) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to native function repeatedFieldGetElement().  "
      "Expected protobuf repeated field, got: " +
      repeated_field.DebugString()));
    return;
  }

  const RepeatedFieldData* repeated_field_data =
    repeated_field.GetData().As<RepeatedFieldData>();
  const google::protobuf::Message* message = repeated_field_data->message->GetMessage();
  const google::protobuf::Reflection* reflection = message->GetReflection();
  const google::protobuf::FieldDescriptor* field = repeated_field_data->field;

  context->Return(MakeInteger(reflection->FieldSize(*message, field)));
}

// Get an element of a repeated field.
void ProtobufUtils::GetElement(Context* context,
                               Value repeated_field, int index) {
  if (repeated_field.GetLogic() != &kRepeatedLogic) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to native function repeatedFieldGetElement().  "
      "Expected protobuf repeated field, got: " +
      repeated_field.DebugString()));
    return;
  }

  const RepeatedFieldData* repeated_field_data =
    repeated_field.GetData().As<RepeatedFieldData>();
  const google::protobuf::Message* message = repeated_field_data->message->GetMessage();
  const google::protobuf::Reflection* reflection = message->GetReflection();
  const google::protobuf::FieldDescriptor* field = repeated_field_data->field;

  if (index < 0 || index >= reflection->FieldSize(*message, field)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Index out-of-bounds for protocol message repeated field: " +
      field->name()));
    return;
  }

  ReturnFieldValue(context, repeated_field_data->message, *message, reflection,
                   field, index);
}

// Helper function for ProtobufUtils::Build() which goes through an
// AssignmentList and sets the corresponding fields in the given Message.
bool ProtobufUtils::ApplyAssignmentList(Context* context,
                                        const AssignmentList* list,
                                        google::protobuf::Message* message) {
  const google::protobuf::Reflection* reflection = message->GetReflection();

  // The list is in backwards order from the order in which we want to apply
  // it.
  vector<const AssignmentList*> assignments;

  while (list != NULL) {
    assignments.push_back(list);
    list = list->rest;
  }

  for (int i = assignments.size() - 1; i >= 0; i--) {
    const google::protobuf::FieldDescriptor* field = assignments[i]->field;
    const Value& value = assignments[i]->value;

    // TODO(kenton):  Perhaps the type checking should really be done in
    //   FieldBuilderLogic::Run()?

    switch (field->cpp_type()) {
      case google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
        if (!IsInteger(value)) {
          context->Return(TypeErrorUtils::MakeError(context,
            strings::Substitute(
              "Wrong type when setting protobuf field $0.  Expected integer, "
              "got: $1", field->full_name(), value.DebugString())));
          return false;
        }

        int native_value = GetIntegerValue(value);

        if (field->is_repeated()) {
          reflection->AddInt32(message, field, native_value);
        } else {
          reflection->SetInt32(message, field, native_value);
        }
        break;
      }

      case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
        double double_value = 0;

        if (IsDouble(value)) {
          double_value = GetDoubleValue(value);
        } else if (IsInteger(value)) {
          double_value = GetIntegerValue(value);
        } else {
          context->Return(TypeErrorUtils::MakeError(context,
            strings::Substitute(
              "Wrong type when setting protobuf field $0.  Expected double, "
              "got: $1", field->full_name(), value.DebugString())));
        }

        if (field->is_repeated()) {
          if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
            reflection->AddDouble(message, field, double_value);
          } else {
            reflection->AddFloat(message, field, (float) double_value);
          }
        } else {
          if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
            reflection->SetDouble(message, field, double_value);
          } else {
            reflection->SetFloat(message, field, (float) double_value);
          }
        }
        break;
      }

      case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
        if (!BooleanUtils::IsBoolean(value)) {
          context->Return(TypeErrorUtils::MakeError(context,
            strings::Substitute(
              "Wrong type when setting protobuf field $0.  Expected boolean, "
              "got: $1", field->full_name(), value.DebugString())));
          return false;
        }

        bool native_value = BooleanUtils::GetBooleanValue(value);

        if (field->is_repeated()) {
          reflection->AddBool(message, field, native_value);
        } else {
          reflection->SetBool(message, field, native_value);
        }
        break;
      }

      case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
        if (!ByteUtils::IsByteArray(value)) {
          context->Return(TypeErrorUtils::MakeError(context,
            strings::Substitute(
              "Wrong type when setting protobuf field $0.  Expected string, "
              "got: $1", field->full_name(), value.DebugString())));
          return false;
        }

        string native_value = ByteUtils::ToString(value).as_string();

        if (field->is_repeated()) {
          reflection->AddString(message, field, native_value);
        } else {
          reflection->SetString(message, field, native_value);
        }
        break;
      }

      case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
        if (!IsAtom(value)) {
          context->Return(TypeErrorUtils::MakeError(context,
            strings::Substitute(
              "Wrong type when setting protobuf field $0.  Expected atom, "
              "got: $1", field->full_name(), value.DebugString())));
          return false;
        }

        string name = GetAtomName(value.GetData()).as_string();
        const google::protobuf::EnumValueDescriptor* native_value =
          field->enum_type()->FindValueByName(name);

        if (native_value == NULL) {
          context->Return(TypeErrorUtils::MakeError(context,
            strings::Substitute(
              "Protobuf field $0 has no enum value named \"$1\".",
              field->full_name(), name)));
          return false;
        }

        if (field->is_repeated()) {
          reflection->AddEnum(message, field, native_value);
        } else {
          reflection->SetEnum(message, field, native_value);
        }
        break;
      }

      case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
        if (value.GetLogic() != &kBuilderLogic) {
          context->Return(TypeErrorUtils::MakeError(context,
            strings::Substitute(
              "Wrong type when setting protobuf field $0.  Expected protobuf "
              "builder, got: $1", field->full_name(), value.DebugString())));
          return false;
        }

        const BuilderData* sub_builder = value.GetData().As<BuilderData>();
        const google::protobuf::Descriptor* sub_descriptor =
          sub_builder->default_instance->GetMessage()->GetDescriptor();

        if (sub_descriptor != field->message_type()) {
          context->Return(TypeErrorUtils::MakeError(context,
            strings::Substitute(
              "Wrong type when setting protobuf field $0.  Expected protobuf "
              "builder for type \"$1\", got builder for type \"$2\".",
              field->full_name(), field->message_type()->full_name(),
              sub_descriptor->full_name())));
          return false;
        }

        google::protobuf::Message* sub_message;
        if (field->is_repeated()) {
          sub_message = reflection->AddMessage(message, field);
        } else {
          sub_message = reflection->MutableMessage(message, field);
        }

        if (!ApplyAssignmentList(context, sub_builder->assignment_list,
                                 sub_message)) {
          return false;
        }
        break;
      }

      default:
        context->Return(TypeErrorUtils::MakeError(context,
          "Field type not implemented: " + field->name()));
        return false;
    }
  }

  return true;
}

// Helper function for ProtobufUtils::Build() which goes through a BuilderData
// and calls memory_manager->Withdraw() on all of its contents in the order
// which is most likely to succeed.
void ProtobufUtils::WithdrawBuilderData(
    const BuilderData* builder,
    memory::MemoryManager* memory_manager) {
  const AssignmentList* list = builder->assignment_list;
  memory_manager->TryWithdraw(builder);

  vector<const BuilderData*> sub_builders;
  while (list != NULL) {
    if (!memory_manager->TryWithdraw(list)) {
      // No use trying to withdraw anything else.
      return;
    }
    if (list->value.GetLogic() == &kBuilderLogic) {
      sub_builders.push_back(list->value.GetData().As<BuilderData>());
    }
    list = list->rest;
  }

  // At this point, sub_builders contains builders in the order in which they
  // were created, due to the fact that the interpreter computes the parameter
  // of an application before computing the function that is being applied to
  // it.  We want to withdraw them in reverse order, so we iterate through the
  // vector backwards.
  for (int i = sub_builders.size() - 1; i >= 0; i--) {
    WithdrawBuilderData(sub_builders[i], memory_manager);
  }
}

// Inputs a protobuf builder, returns the final message.
void ProtobufUtils::Build(Context* context, Value builder) {
  if (builder.GetLogic() != &kBuilderLogic) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to native function protobufBuild().  "
      "Expected protobuf builder, got: " +
      builder.DebugString()));
    return;
  }

  const BuilderData* builder_data = builder.GetData().As<BuilderData>();
  const TrackedMessage* default_instance = builder_data->default_instance;

  scoped_ptr<google::protobuf::Message> message(default_instance->GetMessage()->New());

  if (!ApplyAssignmentList(context, builder_data->assignment_list,
                           message.get())) {
    // Already returned a type error.
    return;
  }

  WithdrawBuilderData(builder_data, context->GetMemoryManager());

  TrackedMessage* result =
    new TrackedMessage(message.release(), context->GetMemoryManager(),
                       default_instance->GetDescriptorTracker());
  context->Return(Value(&kLogic, Data(result)));
}

// Get a TrackedMessage wrapping the default instance of the given protobuf
// type.  On error, calls context->Return() with a type error and returns
// NULL.
ProtobufUtils::TrackedMessage*
ProtobufUtils::GetDefaultInstanceAsTrackedMessage(
    Context* context, Value type) {
  if (type.GetLogic() != &kMessageTypeLogic) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type to native function protobufDefaultInstance().  Expected "
      "protobuf type, got: " + type.DebugString()));
    return NULL;
  }

  const MessageTypeData* message_type_data =
    type.GetData().As<MessageTypeData>();

  const google::protobuf::Message* prototype;

  {
    MutexLockMaybe lock(message_type_data->proto_file->factory_mutex);
    prototype = message_type_data->proto_file->factory->GetPrototype(
        message_type_data->descriptor);
  }

  if (prototype == NULL) {
    context->Return(TypeErrorUtils::MakeError(context,
      "MessageFactory failed to get prototype for protobuf type: " +
      message_type_data->descriptor->full_name()));
    return NULL;
  }

  TrackedMessage* tracked_message =
      new TrackedMessage(prototype, context->GetMemoryManager(),
                         message_type_data->proto_file->tracker);

  tracked_message->SetOwnsMessage(false);
  return tracked_message;
}

// Given a protocol message type, return the default instance of the type.
void ProtobufUtils::DefaultInstance(Context* context, Value type) {
  TrackedMessage* tracked_message =
    GetDefaultInstanceAsTrackedMessage(context, type);
  if (tracked_message == NULL) return;  // Type error already returned.

  context->Return(Value(&kLogic, Data(tracked_message)));
}

// Given a protocol message type, return a builder for the type.
void ProtobufUtils::Builder(Context* context, Value type) {
  TrackedMessage* tracked_message =
    GetDefaultInstanceAsTrackedMessage(context, type);
  if (tracked_message == NULL) return;  // Type error already returned.

  class TrackedMessageRoot : public memory::MemoryRoot {
   public:
    const TrackedMessage* tracked_message_;
    Value callback_;

    // implements MemoryRoot -----------------------------------------
    void Accept(memory::MemoryVisitor* visitor) const {
      tracked_message_->VisitTracker(visitor);
      callback_.Accept(visitor);
    }
  };
  TrackedMessageRoot root;
  root.tracked_message_ = tracked_message;
  root.callback_ = context->GetCallback();

  BuilderData* builder_data =
    context->GetMemoryManager()->Allocate<BuilderData>(&root);
  builder_data->default_instance = tracked_message;
  builder_data->assignment_list = NULL;
  context->SetNext(root.callback_, Value(&kBuilderLogic, Data(builder_data)));
}

// Is the field with the given (atom) name set in the given message?
void ProtobufUtils::HasField(Context* context,
                             const google::protobuf::Message* message,
                             Value field_name) {
  if (!IsAtom(field_name)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type for second argument to native function protobufHas().  "
      "Expected atom, got: " +
      field_name.DebugString()));
    return;
  }

  StringPiece name = GetAtomName(field_name.GetData());
  const google::protobuf::Descriptor* descriptor = message->GetDescriptor();
  const google::protobuf::FieldDescriptor* field =
    descriptor->FindFieldByCamelcaseName(name.as_string());

  if (field == NULL) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Protobuf type \"" + descriptor->full_name() + "\" has no field \"" +
      name.as_string() + "\"."));
    return;
  }

  HasHelper(context, message, field);
}

// Is the extension with the given (full) name set in the given message?
void ProtobufUtils::HasExtension(Context* context,
                                 const google::protobuf::Message* message,
                                 Value extension_identifier) {
  if (extension_identifier.GetLogic() != &kExtensionIdentifierLogic) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to hasExtension().  Expected extension identifier, "
      "got: " + extension_identifier.DebugString()));
    return;
  }

  const google::protobuf::FieldDescriptor* extension =
    extension_identifier.GetData().As<ExtensionIdentifierData>()->descriptor;

  if (extension->containing_type() != message->GetDescriptor()) {
    context->Return(TypeErrorUtils::MakeError(context,
      strings::Substitute(
        "Extension identifier passed to hasExtension() did not match message "
        "type.  Extension is \"$0\", which extends message type \"$1\", but "
        "the message being read is of type \"$2\".",
        extension->full_name(),
        extension->containing_type()->full_name(),
        message->GetDescriptor()->full_name())));
    return;
  }

  HasHelper(context, message, extension);
}

// Helper for Has() and HasExtension().
void ProtobufUtils::HasHelper(Context* context,
                              const google::protobuf::Message* message,
                              const google::protobuf::FieldDescriptor* field) {
  if (field->is_repeated()) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Called protobufHas() on repeated field \"" + field->full_name() +
      "\"."));
    return;
  }

  context->Return(BooleanUtils::MakeBoolean(
    message->GetReflection()->HasField(*message, field)));
}

void ProtobufUtils::GetExtension(Context* context,
                                 Value message_value,
                                 Value extension_identifier) {
  if (!IsMessage(message_value)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type for first argument to native function "
      "protobufGetExtension().  Expected protocol message, got: " +
      message_value.DebugString()));
    return;
  }

  if (extension_identifier.GetLogic() != &kExtensionIdentifierLogic) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to getExtension().  Expected extension identifier, "
      "got: " + extension_identifier.DebugString()));
    return;
  }

  const google::protobuf::Message* message = GetMessage(message_value);
  const google::protobuf::FieldDescriptor* extension =
    extension_identifier.GetData().As<ExtensionIdentifierData>()->descriptor;

  if (extension->containing_type() != message->GetDescriptor()) {
    context->Return(TypeErrorUtils::MakeError(context,
      strings::Substitute(
        "Extension identifier passed to extension() did not match message "
        "type.  Extension is \"$0\", which extends message type \"$1\", but "
        "the message being read is of type \"$2\".",
        extension->full_name(),
        extension->containing_type()->full_name(),
        message->GetDescriptor()->full_name())));
    return;
  }

  ReturnField(context, message_value.GetData().As<TrackedMessage>(),
              *message, extension);
}

void ProtobufUtils::SetExtension(Context* context,
                                 Value builder_value,
                                 Value extension_identifier,
                                 Value value) {
  if (builder_value.GetLogic() != &kBuilderLogic) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type for first argument to native function "
      "protobufSetExtension().  Expected protocol message builder, got: " +
      builder_value.DebugString()));
    return;
  }

  if (extension_identifier.GetLogic() != &kExtensionIdentifierLogic) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type passed to builder.extension().  Expected extension "
      "identifier, got: " + extension_identifier.DebugString()));
    return;
  }

  const ExtensionIdentifierData* extension_data =
    extension_identifier.GetData().As<ExtensionIdentifierData>();
  const google::protobuf::FieldDescriptor* extension = extension_data->descriptor;
  const BuilderData* builder_data =
    builder_value.GetData().As<BuilderData>();
  const google::protobuf::Descriptor* descriptor =
    builder_data->default_instance->GetMessage()->GetDescriptor();

  if (extension->containing_type() != descriptor) {
    context->Return(TypeErrorUtils::MakeError(context,
      strings::Substitute(
        "Extension identifier passed to builder.extension() did not match "
        "message type.  Extension is \"$0\", which extends message type "
        "\"$1\", but the message being written is of type \"$2\".",
        extension->full_name(),
        extension->containing_type()->full_name(),
        descriptor->full_name())));
    return;
  }

  SetField(context, builder_data,
           extension, extension_data->proto_file->tracker,
           value, NULL);
}

const Logic* ProtobufUtils::NewBuiltinModule() {
  NativeModule* result = new NativeModule("nativeProtobuf");
  result->AddFunction("repeatedSize", &GetSize);
  result->AddFunction("repeatedElement", &GetElement);
  result->AddFunction("build", &Build);
  result->AddFunction("builder", &Builder);
  result->AddFunction("defaultInstance", &DefaultInstance);
  result->AddFunction("has", &HasField);
  result->AddFunction("extension", &GetExtension);
  result->AddFunction("hasExtension", &HasExtension);
  result->AddFunction("setExtension", &SetExtension);
  return result;
}

const Logic* ProtobufUtils::GetBuiltinModule() {
  static const Logic* const kSingleton = NewBuiltinModule();
  return kSingleton;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
