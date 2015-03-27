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

#ifndef EVLAN_VM_DISTRIBUTE_REMOTE_WORKER_IMPL_H_
#define EVLAN_VM_DISTRIBUTE_REMOTE_WORKER_IMPL_H_

#include <unordered_map>
#include "evlan/vm/distribute/remote_worker.h"
#include "evlan/common.h"

namespace evlan { class RpcSystem; }

namespace evlan {
namespace vm {
namespace distribute {

class ValueRegistry;
class ValueRegistrar;

class RemoteWorkerFactoryImpl : public RemoteWorkerFactory {
 public:
  struct ServerContext {
    const proto::ServerAddress* my_address;
    RpcSystem* rpc_system;
    ValueRegistrar* persistent_value_registrar;
    ValueRegistry* known_value_registry;
    ValueRegistrar* known_value_registrar;
  };

  RemoteWorkerFactoryImpl(const ServerContext& server_context);
  virtual ~RemoteWorkerFactoryImpl();

  // implements RemoteWorkerFactory ----------------------------------
  virtual RemoteWorker* GetRemoteWorker(
      memory::MemoryManager* memory_manager,
      const proto::ServerAddress& address);

 private:
  class RemoteWorkerImpl;
  class CallSession;
  typedef unordered_map<string, RemoteWorkerImpl*> RemoteWorkerMap;

  const ServerContext server_context_;

  Mutex mutex_;
  RemoteWorkerMap remote_workers_;

  void Remove(const proto::ServerAddress& address,
              RemoteWorkerImpl* worker);
};

}  // namespace distribute
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_DISTRIBUTE_REMOTE_WORKER_IMPL_H_
