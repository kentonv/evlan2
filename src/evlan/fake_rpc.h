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

#ifndef EVLAN_FAKE_RPC_H_
#define EVLAN_FAKE_RPC_H_

#include "evlan/rpc.h"
#include <queue>
#include <unordered_map>

namespace evlan {

class SimpleExecutor : public Executor {
 public:
  SimpleExecutor();
  ~SimpleExecutor();

  void Loop();
  void StopLooping();

  // implements Executor ---------------------------------------------
  void Add(Closure* callback);
  void Add(function<void()> callback);

 private:
  bool keep_looping_;
  queue<Closure*> callbacks_;

  static void RunFunction(function<void()> function) {
    function();
  }
};

class FakeRpcSystem : public RpcSystem {
 public:
  ~FakeRpcSystem();

  void Export(const vm::proto::ServerAddress address,
              google::protobuf::Service* service);
  void Unexport(const vm::proto::ServerAddress address);

  void Run();
  void Stop();

  // implements RpcSystem --------------------------------------------
  Executor* executor();
  StreamingRpcController* NewController();
  google::protobuf::RpcChannel* NewChannel(
      const vm::proto::ServerAddress& remote_address);

 private:
  SimpleExecutor executor_;
  unordered_map<string, google::protobuf::Service*> services_;
};

}  // namespace evlan

#endif  // EVLAN_FAKE_RPC_H_
