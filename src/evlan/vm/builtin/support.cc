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

#include "evlan/vm/builtin/support.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/double.h"
#include "evlan/vm/builtin/boolean.h"
#include "evlan/vm/builtin/atom.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/protobuf.h"
#include "evlan/vm/builtin/type_error.h"
#include "evlan/vm/builtin/array.h"
#include "evlan/vm/builtin/native.h"

namespace evlan {
namespace vm {
namespace builtin {

namespace {

const string kBuiltinTypeNames[SupportCodeManager::NUM_TYPES] = {
  "integer", "double", "boolean", "atom", "string", "array",
  "protobuf", "protobufRepeated", "protobufBuilder", "protobufType"
};

const Logic* kBuiltinLogics[SupportCodeManager::NUM_TYPES] = {
  GetIntegerLogic(),
  GetDoubleLogic(),
  BooleanUtils::GetBooleanLogic(),
  GetAtomLogic(),
  ByteUtils::GetByteArrayLogic(),
  ArrayUtils::GetArrayLogic(),
  ProtobufUtils::GetMessageLogic(),
  ProtobufUtils::GetRepeatedFieldLogic(),
  ProtobufUtils::GetBuilderLogic(),
  ProtobufUtils::GetMessageTypeLogic()
};

// ===================================================================
// Callback Logic which applies its parameter to some value known ahead
// of time, with the result going to another callback.

struct ApplyToValueData {
  Value parameter;
  Value callback;

  int Accept(memory::MemoryVisitor* visitor) const {
    parameter.Accept(visitor);
    callback.Accept(visitor);
    return sizeof(*this);
  }
};

class ApplyToValueCallback : public Logic {
 public:
  ApplyToValueCallback() {}
  ~ApplyToValueCallback() {}

  static const ApplyToValueCallback kSingleton;

  // implements Logic ------------------------------------------------
  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<ApplyToValueData>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("x => x(");
    if (depth > 0) {
      data.As<ApplyToValueData>()->parameter.DebugPrint(depth - 1, printer);
    } else {
      printer->Print("...");
    }
    printer->Print(")");
  }

  void Run(Context* context) const {
    const ApplyToValueData* apply_to_value_data =
      context->GetData().As<ApplyToValueData>();
    context->SetNext(context->GetParameter(),
                     apply_to_value_data->parameter,
                     apply_to_value_data->callback);
  }
};

const ApplyToValueCallback ApplyToValueCallback::kSingleton;

// ===================================================================

class SupportCode : public Logic, public memory::TrackedObject {
 public:
  // |impl| is a function which takes the builtin value and returns an Evlan
  // object representing that value.
  SupportCode(memory::MemoryManager* memory_manager,
              const Logic* builtin_logic, const Value& impl)
    : builtin_logic_(builtin_logic), impl_(impl) {
    tracker_ = memory_manager->AddTrackedObject(this);
  }
  ~SupportCode() {}

  memory::ObjectTracker* GetTracker() const {
    return tracker_;
  }

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    builtin_logic_->Accept(data, visitor);
    visitor->VisitTrackedObject(tracker_);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    builtin_logic_->DebugPrint(data, depth, printer);
  }

  void Run(Context* context) const {
    ApplyToValueData* step2_data = context->Allocate<ApplyToValueData>();

    step2_data->parameter = context->GetParameter();
    step2_data->callback = context->GetCallback();

    context->SetNext(
      impl_,
      Value(builtin_logic_, context->GetData()),
      Value(&ApplyToValueCallback::kSingleton, Data(step2_data)));
  }

  // implements TrackedObject ----------------------------------------

  void NotReachable() {
    delete this;
  }

  void Accept(memory::MemoryVisitor* visitor) const {
    impl_.Accept(visitor);
  }

 private:
  memory::ObjectTracker* tracker_;
  const Logic* builtin_logic_;
  Value impl_;
};

// ===================================================================

// Logic used as a placeholder until the SupportCodeManager is implemented.
class PlaceholderLogic : public Logic {
 public:
  PlaceholderLogic() {}
  ~PlaceholderLogic() {}

  static const PlaceholderLogic kSingleton;

  // implements Logic ------------------------------------------------
  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    // nothing
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("supportCodePlaceholder");
  }

  void Run(Context* context) const {
    context->Return(TypeErrorUtils::MakeError(context,
      "Called placeholder logic."));
  }
};

const PlaceholderLogic PlaceholderLogic::kSingleton;

// ===================================================================

class InitSupportCodeCallback : public Logic {
 public:
  InitSupportCodeCallback(const SupportCode** target,
                          const Logic* builtin_logic)
    : target_(target), builtin_logic_(builtin_logic) {}
  ~InitSupportCodeCallback() {}

  // implements Logic ------------------------------------------------
  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    // nothing
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("initSupportCodeCallback");
  }

  void Run(Context* context) const {
    GOOGLE_CHECK(*target_ == NULL) << "Callback called twice?";
    *target_ = new SupportCode(context->GetMemoryManager(),
                               builtin_logic_, context->GetParameter());
    // Keep alive.
    (*target_)->GetTracker()->AddExternalReference();
  }

 private:
  const SupportCode** target_;
  const Logic* builtin_logic_;
};

}  // namespace

// ===================================================================

SupportCodeManager::SupportCodeManager()
  : initialized_(false) {
  for (int i = 0; i < arraysize(support_code_logics_); i++) {
    support_code_logics_[i] = &PlaceholderLogic::kSingleton;
  }
}

SupportCodeManager::~SupportCodeManager() {
}

void SupportCodeManager::Init(Context* context, const Value& support_module) {
  GOOGLE_CHECK(!initialized_) << "SupportCodeManager already initialized.";

  // Make sure support module is not GC'd until we're done with it.
  KeepAliveValue* support_module_keepalive =
    new KeepAliveValue(context->GetMemoryManager(), support_module);

  // Init each logic.  We call support_module with each of the atoms @integer,
  // @string, etc., then create a SupportCode around each result.
  for (int i = 0; i < arraysize(support_code_logics_); i++) {
    Value atom = context->GetAtomRegistry()->GetAtom(
      context->GetMemoryManager(), kBuiltinTypeNames[i]);
    const SupportCode* result = NULL;
    InitSupportCodeCallback callback(&result, kBuiltinLogics[i]);
    context->SetNext(support_module_keepalive->Get(NULL),
                     atom, Value(&callback, Data()));
    context->Run();
    GOOGLE_CHECK(result != NULL)
      << "Language support module contains blocking code?";
    support_code_logics_[i] = result;
  }

  // We can release the support module now.
  support_module_keepalive->Release();
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
