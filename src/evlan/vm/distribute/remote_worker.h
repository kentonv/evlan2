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

#ifndef EVLAN_VM_REMOTE_WORKER_H_
#define EVLAN_VM_REMOTE_WORKER_H_

#include "evlan/vm/runtime.h"
#include "evlan/common.h"

namespace evlan {
  namespace vm {
    namespace proto {
      class ServerAddress;
      class Error;
    }
  }
}

namespace evlan {
namespace vm {
namespace distribute {

class RemoteWorker {
 public:
  virtual ~RemoteWorker();

  virtual memory::ObjectTracker* GetTracker() = 0;

  virtual void GetAddress(proto::ServerAddress* output) = 0;

  virtual void Call(Context* context, const ValueId& id,
                    Value parameter, Value callback) = 0;
};

class RemoteWorkerFactory {
 public:
  virtual ~RemoteWorkerFactory();

  virtual RemoteWorker* GetRemoteWorker(
      memory::MemoryManager* memory_manager,
      const proto::ServerAddress& address) = 0;
};

}  // namespace distribute
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_REMOTE_WORKER_H_
