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

#include "evlan/vm/distribute/remote_value.h"
#include "evlan/vm/distribute/remote_worker.h"
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/vm/proto/marshal.pb.h"
#include "evlan/vm/hasher.h"
#include <google/protobuf/io/printer.h>
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace distribute {

namespace {

struct RemoteValueData {
  RemoteWorker* worker;
  memory::ObjectTracker* worker_tracker;
  ValueId value_id;

  int Accept(memory::MemoryVisitor* visitor) const {
    if (worker_tracker != NULL) {
      visitor->VisitTrackedObject(worker_tracker);
    }
    return sizeof(*this);
  }
};

class RemoteValueLogic : public Logic {
 public:
  static const RemoteValueLogic kSingleton;

  RemoteValueLogic() {}

  // implements Logic ------------------------------------------------
  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<RemoteValueData>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    const RemoteValueData* remote_value_data = data.As<RemoteValueData>();
    printer->Print("remoteValue(\"$id$\")", "id",
        strings::CEscape(remote_value_data->value_id.GetBytes().as_string()));
  }

  void Run(Context* context) const {
    const RemoteValueData* remote_value_data =
        context->GetData().As<RemoteValueData>();

    remote_value_data->worker->Call(
        context,
        remote_value_data->value_id,
        context->GetParameter(),
        context->GetCallback());
  }

  void GetId(Data data, ValueId* output) const {
    const RemoteValueData* remote_value_data = data.As<RemoteValueData>();

    Hasher hasher;

    proto::ServerAddress address;
    remote_value_data->worker->GetAddress(&address);
    hasher.AddString(address.SerializeAsString());
    hasher.AddString(remote_value_data->value_id.GetBytes());

    output->SetBytesFromHasher('~', &hasher);
  }

  int Marshal(Data data, distribute::Marshaller* marshaller,
              const ValueId* known_id = NULL) const {
    const RemoteValueData* remote_value_data = data.As<RemoteValueData>();

    marshaller->TryAvoidMarshalling(ValueId(), Value(this, data),
                                    Marshaller::ELIMINATE_DUPS);

    int result = marshaller->AddRemoteValue(
        ValueId(),       // Don't bother remembering if we already sent this.
        remote_value_data->value_id,
        remote_value_data->worker);
    marshaller->RecordForDupElimination(Value(this, data), result);
    return result;
  }
};

const RemoteValueLogic RemoteValueLogic::kSingleton;

class MakeRemoteValueMemoryRoot : public memory::MemoryRoot {
 public:
  memory::ObjectTracker* worker_tracker_;
  memory::MemoryRoot* parent_;

  // implements MemoryRoot -------------------------------------------
  virtual void Accept(memory::MemoryVisitor* visitor) const {
    visitor->VisitTrackedObject(worker_tracker_);
    parent_->Accept(visitor);
  }
};

}  // namespace

Value MakeRemoteValue(memory::MemoryManager* memory_manager,
                      memory::MemoryRoot* memory_root,
                      RemoteWorker* worker,
                      ValueId value_id) {
  MakeRemoteValueMemoryRoot sub_memory_root;
  sub_memory_root.parent_ = memory_root;
  sub_memory_root.worker_tracker_ = worker->GetTracker();

  RemoteValueData* remote_value_data =
      memory_manager->Allocate<RemoteValueData>(&sub_memory_root);

  remote_value_data->worker = worker;
  remote_value_data->worker_tracker = sub_memory_root.worker_tracker_;
  remote_value_data->value_id = value_id;

  return Value(&RemoteValueLogic::kSingleton, Data(remote_value_data));
}

}  // namespace distribute
}  // namespace vm
}  // namespace evlan
