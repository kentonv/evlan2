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

#ifndef EVLAN_RPC_H_
#define EVLAN_RPC_H_

#include <google/protobuf/service.h>
#include <functional>
#include "evlan/common.h"

namespace evlan {
  namespace vm {
    namespace proto {
      class ServerAddress;
    }
  }
}

namespace evlan {

class StreamingRpcController : public google::protobuf::RpcController {
 public:
  virtual ~StreamingRpcController();

  // Callback must be permanent; will be called multiple times.
  virtual void ReceiveStream(google::protobuf::Message* message, Closure* callback) = 0;
  virtual void StopReceivingStream(Closure* callback) = 0;

  virtual void SendStream(const google::protobuf::Message& message) = 0;
};

class Executor {
 public:
  virtual ~Executor();

  virtual void Add(Closure* callback) = 0;
  virtual void Add(function<void()> callback) = 0;
};

class RpcSystem {
 public:
  virtual ~RpcSystem();

  virtual Executor* executor() = 0;
  virtual StreamingRpcController* NewController() = 0;
  virtual google::protobuf::RpcChannel* NewChannel(
      const vm::proto::ServerAddress& remote_address) = 0;
};

}  // namespace evlan

#endif  // EVLAN_RPC_H_
