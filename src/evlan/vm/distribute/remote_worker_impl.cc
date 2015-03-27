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

#include "evlan/vm/distribute/remote_worker_impl.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <functional>

#include "evlan/vm/distribute/marshaller.h"
#include "evlan/vm/distribute/unmarshaller.h"
#include "evlan/rpc.h"
#include "evlan/vm/proto/marshal.pb.h"
#include "evlan/vm/proto/worker.pb.h"
#include "evlan/vm/builtin/type_error.h"
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace distribute {

using namespace std::placeholders;

namespace {

void RunInNewContext(ContextFactory* factory,
                     KeepAliveValue* callback,
                     KeepAliveValue* parameter) {
  Context context(factory);
  factory->RemoveReference();
  context.SetNext(callback->Get(context.GetMemoryManager()),
                  parameter->Get(context.GetMemoryManager()));
  callback->Release();
  parameter->Release();
  context.Run();
}

proto::EvlanWorker* Connect(const proto::ServerAddress& remote_address,
                            RpcSystem* rpc_system) {
  string target;

  if (remote_address.has_ip4_address()) {
    in_addr addr;
    addr.s_addr = remote_address.ip4_address();
    char buffer[INET_ADDRSTRLEN];
    GOOGLE_CHECK(inet_ntop(AF_INET, &addr, buffer, sizeof(buffer)) != NULL);
    target = buffer;
  } else if (remote_address.has_dns_address()) {
    target = remote_address.dns_address();
  }

  if (remote_address.has_port_number()) {
    target = strings::Substitute("$0:$1", target, remote_address.port_number());
  }

  return new proto::EvlanWorker::Stub(
      rpc_system->NewChannel(remote_address),
      google::protobuf::Service::STUB_OWNS_CHANNEL);
}

}  // namespace

// ===================================================================

class RemoteWorkerFactoryImpl::CallSession {
 public:
  CallSession(const ServerContext& server_context,
              RemoteWorkerFactory* remote_worker_factory,
              proto::EvlanWorker* stub,
              ReceiverInfo* receiver_info,
              Mutex* receiver_info_mutex,
              function<void(CallSession*)> return_to_freelist)
    : return_to_freelist_(return_to_freelist),
      async_executor_(server_context.rpc_system->executor()),
      stub_(stub),
      receiver_info_(receiver_info),
      receiver_info_mutex_(receiver_info_mutex),
      known_value_registrar_(server_context.known_value_registrar),
      pusher_(*server_context.my_address,
              server_context.persistent_value_registrar),
      puller_(server_context.known_value_registry, remote_worker_factory),
      state_(STANDBY),
      context_factory_(NULL),
      callback_(NULL),
      rpc_(server_context.rpc_system->NewController()) {}

  void Start(Context* context, const ValueId& function_id,
             Value parameter, Value callback) {
    GOOGLE_CHECK_EQ(state_, STANDBY);
    state_ = RUNNING;

    context_factory_ = context->GetContextFactory();
    context_factory_->AddReference();
    callback_ = new KeepAliveValue(context->GetMemoryManager(), callback);

    pusher_.Start(receiver_info_, receiver_info_mutex_);
    puller_.Start();

    pusher_.SendFunctionCall(context->GetMemoryManager(),
                             function_id, parameter,
                             request_.mutable_initial_push());

    rpc_->ReceiveStream(&stream_response_,
                        NewPermanentCallback(this, &CallSession::StreamDone));
    stub_->Call(rpc_.get(), &request_, &response_,
                NewCallback(this, &CallSession::Done));
  }

 private:
  function<void(CallSession*)> const return_to_freelist_;
  Executor* const async_executor_;
  proto::EvlanWorker* const stub_;
  ReceiverInfo* const receiver_info_;
  Mutex* const receiver_info_mutex_;
  ValueRegistrar* const known_value_registrar_;
  ValuePusher pusher_;
  ValuePuller puller_;

  enum {
    STANDBY,
    RUNNING,
    DONE,
    ERROR
  } state_;
  string error_;

  ContextFactory* context_factory_;
  KeepAliveValue* callback_;

  scoped_ptr<StreamingRpcController> rpc_;
  proto::CallRequest request_;
  proto::CallResponse response_;
  proto::PushPullValuesMessage stream_request_;
  proto::PushPullValuesMessage stream_response_;

  void ReturnToFreelist() {
    GOOGLE_CHECK(state_ == DONE || state_ == ERROR);
    state_ = STANDBY;
    error_.clear();

    context_factory_ = NULL;
    callback_ = NULL;
    rpc_->Reset();
    request_.Clear();
    response_.Clear();
    stream_request_.Clear();
    stream_response_.Clear();

    return_to_freelist_(this);
  }

  void StreamDone() {
    if (state_ != RUNNING) return;

    stream_request_.Clear();

    Context context(context_factory_);

    if (stream_response_.has_pull()) {
      if (pusher_.RespondToPull(context.GetMemoryManager(),
                                stream_response_.pull(),
                                stream_request_.mutable_push(),
                                &error_)) {
        rpc_->SendStream(stream_request_);
      } else {
        state_ = ERROR;
        error_ = strings::Substitute("RPC client error: $0", error_);
        rpc_->StartCancel();
      }
    } else if (stream_response_.has_push()) {
      switch (puller_.ReceivePush(context.GetMemoryManager(),
                                  stream_response_.push(),
                                  stream_request_.mutable_pull(),
                                  &error_)) {
        case ValuePuller::PULL:
          rpc_->SendStream(stream_request_);
          break;
        case ValuePuller::WAITING:
          // Nothing to do; wait for next message.
          break;
        case ValuePuller::READY:
          state_ = DONE;
          rpc_->StartCancel();
          break;
        case ValuePuller::ERROR:
          state_ = ERROR;
          error_ = strings::Substitute("RPC client error: $0", error_);
          rpc_->StartCancel();
          break;
      }
    }
  }

  void Done() {
    GOOGLE_CHECK_NE(state_, STANDBY);

    pusher_.Finish();

    Context context(context_factory_);

    if (state_ == RUNNING) {
      // TODO(kenton):  Handle transient errors better.
      if (rpc_->Failed()) {
        state_ = ERROR;
        error_ = strings::Substitute("RPC network error: $0", rpc_->ErrorText());
      } else if (response_.has_error()) {
        state_ = ERROR;
        error_ = strings::Substitute("RPC server error: $0", response_.error().description());
      } else {
        switch (puller_.ReceivePush(context.GetMemoryManager(),
                                    stream_response_.push(),
                                    stream_request_.mutable_pull(),
                                    &error_)) {
          case ValuePuller::PULL:
          case ValuePuller::WAITING:
            state_ = ERROR;
            error_ = "RPC client error: Server sent final response without "
                     "resolving all outstanding value IDs.";
            break;
          case ValuePuller::READY:
            state_ = DONE;
            break;
          case ValuePuller::ERROR:
            state_ = ERROR;
            error_ = strings::Substitute("RPC client error: $0", error_);
            break;
        }
      }
    }

    if (state_ != ERROR) {
      // Try to unmarshal.
      function<void(Context*)> unmarshal_done =
          std::bind(&CallSession::UnmarshalDone, this, _1);
      if (puller_.Unmarshal(&context, unmarshal_done)) {
        context.Run();
        return;
      } else {
        state_ = ERROR;
        error_ = "RPC client error: Received invalid marshalled values from "
                 "server.";
      }
    }

    // ERROR

    GOOGLE_CHECK_EQ(state_, ERROR);

    puller_.Cancel();

    memory::NullMemoryRoot null_root;
    Value error = builtin::TypeErrorUtils::MakeError(
        context.GetMemoryManager(), &null_root, error_);
    KeepAliveValue* error_keepalive = new KeepAliveValue(
        context.GetMemoryManager(), error);
    async_executor_->Add(
        std::bind(&RunInNewContext, context_factory_,
                  callback_, error_keepalive));

    ReturnToFreelist();
  }

  void UnmarshalDone(Context* context) {
    Value final_result =
        puller_.Finish(known_value_registrar_, context->GetMemoryManager());

    KeepAliveValue* final_result_keepalive =
        new KeepAliveValue(context->GetMemoryManager(), final_result);

    async_executor_->Add(
        std::bind(&RunInNewContext, context_factory_,
                  callback_, final_result_keepalive));

    ReturnToFreelist();
  }
};

// ===================================================================

class RemoteWorkerFactoryImpl::RemoteWorkerImpl
    : public RemoteWorker, public memory::TrackedObject {
 public:
  RemoteWorkerImpl(const ServerContext& server_context,
                   RemoteWorkerFactoryImpl* remote_worker_factory,
                   const proto::ServerAddress& remote_address,
                   memory::MemoryManager* memory_manager)
      : server_context_(server_context),
        remote_worker_factory_(remote_worker_factory),
        remote_address_(remote_address),
        stub_(Connect(remote_address, server_context.rpc_system)),
        tracker_(memory_manager->AddTrackedObject(this)),
        call_session_factory_(this),
        return_to_freelist_(
          std::bind(&CallSessionFactory::Delete, &call_session_factory_, _1)) {}
  virtual ~RemoteWorkerImpl() {}

  void Detach() {
    remote_worker_factory_ = NULL;
  }

  // implements RemoteWorker -----------------------------------------

  virtual memory::ObjectTracker* GetTracker() { return tracker_; }

  virtual void GetAddress(proto::ServerAddress* output) {
    output->CopyFrom(remote_address_);
  }

  virtual void Call(Context* context, const ValueId& id,
                    Value parameter, Value callback) {
    call_session_factory_.New()->Start(context, id, parameter, callback);
  }

  // implements TrackedObject ----------------------------------------

  virtual void NotReachable() {
    if (remote_worker_factory_ != NULL) {
      remote_worker_factory_->Remove(remote_address_, this);
    }
    delete this;
  }

  virtual void Accept(memory::MemoryVisitor* visitor) const {
    // Nothing.
  }

 private:
  // In theory this factory object could be used by a freelist
  // implementation.
  class CallSessionFactory {
   public:
    CallSessionFactory(RemoteWorkerImpl* worker)
        : worker_(worker) {}
    virtual ~CallSessionFactory() {}

    virtual CallSession* New() {
      worker_->tracker_->AddExternalReference();
      return new CallSession(
          worker_->server_context_,
          worker_->remote_worker_factory_,
          worker_->stub_.get(),
          &worker_->receiver_info_,
          &worker_->receiver_info_mutex_,
          worker_->return_to_freelist_);
    }

    virtual void Delete(CallSession* session) {
      worker_->tracker_->RemoveExternalReference();
      delete session;
    }

   private:
    RemoteWorkerImpl* worker_;
  };

  ServerContext const server_context_;
  RemoteWorkerFactoryImpl* remote_worker_factory_;
  proto::ServerAddress const remote_address_;
  scoped_ptr<proto::EvlanWorker> const stub_;
  memory::ObjectTracker* const tracker_;

  ReceiverInfo receiver_info_;
  Mutex receiver_info_mutex_;

  CallSessionFactory call_session_factory_;
  function<void(CallSession*)> return_to_freelist_;

  void FreeCallSession(CallSession* session) {
    // TODO(kenton):  Keep a freelist.
    call_session_factory_.Delete(session);
    tracker_->RemoveExternalReference();
  }
};

// ===================================================================

RemoteWorkerFactoryImpl::RemoteWorkerFactoryImpl(
    const ServerContext& server_context)
  : server_context_(server_context) {}

RemoteWorkerFactoryImpl::~RemoteWorkerFactoryImpl() {
  // RemoteWorkerFactoryImpl should not be destroyed while Evlan code is still
  // running.  But, even after all code has stopped, deletion order can be
  // unpredictable.  So, we make sure to NULL out the pointer to the factory
  // from each worker that still exists.
  for (RemoteWorkerMap::iterator iter = remote_workers_.begin();
       iter != remote_workers_.end(); ++iter) {
    iter->second->Detach();
  }
}

RemoteWorker* RemoteWorkerFactoryImpl::GetRemoteWorker(
    memory::MemoryManager* memory_manager,
    const proto::ServerAddress& address) {
  string key = address.SerializeAsString();

  MutexLock lock(&mutex_);
  RemoteWorkerImpl** map_slot = &remote_workers_[key];
  if (*map_slot != NULL &&
      memory_manager->ReviveTrackedObject((*map_slot)->GetTracker())) {
    return *map_slot;
  } else {
    *map_slot = new RemoteWorkerImpl(server_context_, this,
                                     address, memory_manager);
    return *map_slot;
  }
}

void RemoteWorkerFactoryImpl::Remove(const proto::ServerAddress& address,
                                     RemoteWorkerImpl* worker) {
  string key = address.SerializeAsString();

  MutexLock lock(&mutex_);

  // We must be sure that the map key exists in the map and is actually mapped
  // to the removed worker.  If we simply remove the key without checking what
  // it is mapped to, a race condition can result:  If someone calls
  // GetRemoteWorker() for this address at the same time that some other thread
  // decides that the worker is unreachable, then ReviveTrackedObject() in
  // GetRemoteWorker() will return false, and so a new worker will be created.
  // This may all happen while Remove() is waiting on the lock, above.  Once
  // it gets the lock, it tries to remove the worker, but finds that its
  // map slot now points at something new.  We don't want to remove that new
  // worker!
  RemoteWorkerMap::iterator iter = remote_workers_.find(key);
  if (iter != remote_workers_.end() && iter->second == worker) {
    remote_workers_.erase(iter);
  }
}

}  // namespace distribute
}  // namespace vm
}  // namespace evlan
