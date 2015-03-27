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

#include "evlan/vm/distribute/evlan_worker_impl.h"

#include <queue>
#include "evlan/vm/distribute/value_registry_impl.h"
#include "evlan/vm/distribute/remote_value.h"
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/rpc.h"
#include "evlan/fake_rpc.h"
#include <google/protobuf/io/printer.h>
#include "evlan/map-util.h"

#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace distribute {
namespace {

class EvlanServer {
 public:
  EvlanServer(ContextFactory* context_factory)
      : my_address_(GetMyAddress()),
        persistent_value_registry_(),
        cached_value_registry_(&persistent_value_registry_),
        server_context_(MakeServerContext()),
        remote_worker_factory_(server_context_),
        evlan_worker_(server_context_, &remote_worker_factory_,
                      context_factory) {
    rpc_system_.Export(my_address_, &evlan_worker_);
  }

  ~EvlanServer() {
    rpc_system_.Unexport(my_address_);
  }

  proto::ServerAddress GetMyAddress() {
    proto::ServerAddress result;
    result.set_dns_address("localhost");
    result.set_port_number(0);
    return result;
  }

  RemoteWorkerFactoryImpl::ServerContext MakeServerContext() {
    RemoteWorkerFactoryImpl::ServerContext result;
    result.my_address = &my_address_;
    result.rpc_system = &rpc_system_;
    result.persistent_value_registrar = &persistent_value_registry_;
    result.known_value_registry = &cached_value_registry_;
    result.known_value_registrar = &cached_value_registry_;
    return result;
  }

  proto::ServerAddress my_address_;
  PersistentValueRegistry persistent_value_registry_;
  CachedValueRegistry cached_value_registry_;
  RemoteWorkerFactoryImpl::ServerContext server_context_;

  RemoteWorkerFactoryImpl remote_worker_factory_;
  EvlanWorkerImpl evlan_worker_;

  FakeRpcSystem rpc_system_;
};

class RetainedValueLogic : public Logic {
 public:
  static const RetainedValueLogic kSingleton;

  RetainedValueLogic() {}

  // implements Logic ------------------------------------------------

  virtual void Run(Context* context) const {
    context->SetNext(*context->GetData().As<Value>(),
                     context->GetParameter(),
                     context->GetCallback());
  }

  virtual void Accept(const Data* data,
                      memory::MemoryVisitor* visitor) const {
    data->AcceptAs<Value>(visitor);
  }

  virtual int Marshal(Data data, distribute::Marshaller* marshaller,
                      const ValueId* known_id = NULL) const {
    ValueId id;
    data.As<Value>()->GetId(&id);
    return marshaller->AddRetainedValue(ValueId(), id, *data.As<Value>());
  }

  virtual void DebugPrint(Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("retainedValue(");
    if (depth > 0) {
      data.As<Value>()->DebugPrint(depth - 1, printer);
    } else {
      printer->Print("...");
    }
    printer->Print(")");
  }
};

const RetainedValueLogic RetainedValueLogic::kSingleton;

class RetainLogic : public Logic {
 public:
  static const ValueId kId;
  static const RetainLogic kSingleton;

  RetainLogic() {}

  // implements Logic ------------------------------------------------

  virtual void Run(Context* context) const {
    Value* retained_value = context->Allocate<Value>();
    *retained_value = context->GetParameter();
    context->Return(
        Value(&RetainedValueLogic::kSingleton, Data(retained_value)));
  }

  virtual void Accept(const Data* data,
                      memory::MemoryVisitor* visitor) const {
    // nothing
  }

  virtual void GetId(Data data, ValueId* output) const {
    *output = kId;
  }

  virtual void DebugPrint(Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("testRetain");
  }
};

const ValueId RetainLogic::kId("xxxTestRetainId");
const RetainLogic RetainLogic::kSingleton;

class EvlanWorkerTest : public EvlanTest {
 protected:
  EvlanWorkerTest()
      : server_(GetContextFactory()) {}

  virtual void SetUp() {
    Value retain(&RetainLogic::kSingleton, Data());
    server_.persistent_value_registry_.Register(
        RetainLogic::kId, retain,
        GetMemoryManager(), GetMemoryRoot());

    RemoteWorker* self_worker = server_.remote_worker_factory_.GetRemoteWorker(
        GetMemoryManager(), server_.my_address_);
    Value distribute =
        MakeRemoteValue(GetMemoryManager(), GetMemoryRoot(),
                        self_worker, RetainLogic::kId);
    AddImport("distribute", distribute);
  }

  virtual Closure* GetCompletionCallback() {
    return NewCallback(server_.rpc_system_.executor(),
                       &Executor::Add,
                       NewCallback(&server_.rpc_system_,
                                   &FakeRpcSystem::Stop));
  }

  virtual void WaitForCompletion() {
    server_.rpc_system_.Run();
  }

  EvlanServer server_;
};

TEST_F(EvlanWorkerTest, Basic) {
  ExpectEvaluatesToInteger(123, "import \"distribute\"(x => y => x)(123)()");
  ExpectEvaluatesToInteger(123, "import \"distribute\"(x => y => y)()(123)");
  ExpectEvaluatesToInteger(123, "import \"distribute\"(x => y => 123)()()");
}

}  // namespace
}  // namespace distribute
}  // namespace vm
}  // namespace evlan
