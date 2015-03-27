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

#ifndef EVLAN_VM_DISTRIBUTE_EVLAN_WORKER_IMPL_H_
#define EVLAN_VM_DISTRIBUTE_EVLAN_WORKER_IMPL_H_

#include "evlan/vm/proto/worker.pb.h"
#include "evlan/vm/distribute/remote_worker_impl.h"

namespace evlan {
namespace vm {
namespace distribute {

class EvlanWorkerImpl : public proto::EvlanWorker {
 public:
  typedef RemoteWorkerFactoryImpl::ServerContext ServerContext;

  EvlanWorkerImpl(ServerContext server_context,
                  RemoteWorkerFactory* worker_factory,
                  ContextFactory* context_factory);
  ~EvlanWorkerImpl();

  // implements EvlanWorker ------------------------------------------

  virtual void Call(google::protobuf::RpcController* rpc,
                    const proto::CallRequest* request,
                    proto::CallResponse* response,
                    Closure* done);

 private:
  class CallSession;
  class CallSessionFactory {
   public:
    CallSessionFactory(EvlanWorkerImpl* worker);
    virtual ~CallSessionFactory();

    // implements ObjectFactory<CallSession> -------------------------
    virtual CallSession* New();
    virtual void Delete(CallSession* session);

   private:
    EvlanWorkerImpl* worker_;
  };

  ServerContext const server_context_;
  RemoteWorkerFactory* const worker_factory_;
  ContextFactory* const context_factory_;

  CallSessionFactory call_session_factory_;
};

}  // namespace distribute
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_DISTRIBUTE_EVLAN_WORKER_IMPL_H_
