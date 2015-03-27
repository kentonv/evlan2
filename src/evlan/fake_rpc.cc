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

#include "evlan/fake_rpc.h"
#include <google/protobuf/message.h>
#include "evlan/vm/proto/worker.pb.h"
#include "evlan/map-util.h"

namespace evlan {

SimpleExecutor::SimpleExecutor() : keep_looping_(false) {}
SimpleExecutor::~SimpleExecutor() {
  GOOGLE_CHECK(callbacks_.empty());
}

void SimpleExecutor::Loop() {
  keep_looping_ = true;
  while (keep_looping_) {
    GOOGLE_CHECK(!callbacks_.empty()) << "Ran out of stuff to do!";
    callbacks_.front()->Run();
    callbacks_.pop();
  }
}

void SimpleExecutor::StopLooping() {
  keep_looping_ = false;
}

void SimpleExecutor::Add(Closure* callback) {
  callbacks_.push(callback);
}

void SimpleExecutor::Add(function<void()> callback) {
  callbacks_.push(NewCallback(&RunFunction, callback));
}

// ===================================================================

class FakeRpcController : public StreamingRpcController {
 public:
  FakeRpcController(Executor* executor)
    : owned_partner_(new FakeRpcController(this, executor)),
      partner_(owned_partner_.get()),
      executor_(executor) {
    Clear();
  }
  ~FakeRpcController() {}

  FakeRpcController* partner() { return partner_; }

  // implements RpcController ----------------------------------------

  // Client-side methods -------------------------
  void Reset() {
    GOOGLE_CHECK(owned_partner_ != NULL);
    Clear();
    owned_partner_->Clear();
  }
  bool Failed() const {
    GOOGLE_CHECK(owned_partner_ != NULL);
    return failed_;
  }
  string ErrorText() const {
    GOOGLE_CHECK(owned_partner_ != NULL);
    return error_message_;
  }
  void StartCancel() {
    GOOGLE_CHECK(owned_partner_ != NULL);
    partner_->canceled_ = true;
    if (partner_->cancel_callback_ != NULL) {
      executor_->Add(partner_->cancel_callback_);
      partner_->cancel_callback_ = NULL;
    }
  }

  // Server-side methods -------------------------
  void SetFailed(const string& reason) {
    GOOGLE_CHECK(owned_partner_ == NULL);
    partner_->failed_ = true;
    partner_->error_message_ = reason;
  }
  bool IsCanceled() const {
    GOOGLE_CHECK(owned_partner_ == NULL);
    return canceled_;
  }
  void NotifyOnCancel(Closure* callback) {
    GOOGLE_CHECK(owned_partner_ == NULL);
    if (canceled_) {
      executor_->Add(callback);
    } else {
      GOOGLE_CHECK(cancel_callback_ == NULL);
      cancel_callback_ = callback;
    }
  }

  // implements StreamingRpcController -------------------------------
  void ReceiveStream(google::protobuf::Message* message, Closure* callback) {
    stream_protobuf_ = message;
    stream_callback_ = callback;
    PumpStream();
  }

  void StopReceivingStream(Closure* callback) {
    stream_protobuf_ = NULL;
    stream_callback_ = NULL;
    executor_->Add(callback);
  }

  void SendStream(const google::protobuf::Message& message) {
    partner_->stream_messages_.push(message.SerializeAsString());
    if (partner_->stream_callback_ != NULL) {
      partner_->PumpStream();
    }
  }

 private:
  FakeRpcController(FakeRpcController* partner, Executor* executor)
    : partner_(partner), executor_(executor) { Clear(); }

  scoped_ptr<FakeRpcController> owned_partner_;
  FakeRpcController* partner_;
  Executor* executor_;

  queue<string> stream_messages_;
  google::protobuf::Message* stream_protobuf_;
  Closure* stream_callback_;
  bool canceled_;
  Closure* cancel_callback_;
  bool failed_;
  string error_message_;

  void Clear() {
    while (!stream_messages_.empty()) {
      stream_messages_.pop();
    }
    stream_protobuf_ = NULL;
    stream_callback_ = NULL;
    canceled_ = false;
    cancel_callback_ = NULL;
    failed_ = false;
    error_message_.clear();
  }

  void PumpStream() {
    while (!stream_messages_.empty()) {
      executor_->Add(std::bind(&DoStreamCallback, stream_messages_.front(),
                               stream_protobuf_, stream_callback_));
      stream_messages_.pop();
    }
  }

  static void DoStreamCallback(string data, google::protobuf::Message* message,
                               Closure* callback) {
    GOOGLE_CHECK(message->ParseFromString(data));
    callback->Run();
  }
};

// ===================================================================

class FakeRpcChannel : public google::protobuf::RpcChannel {
 public:
  FakeRpcChannel(google::protobuf::Service* service, Executor* executor)
    : service_(service), executor_(executor) {}

  // implements RpcChannel -------------------------------------------
  void CallMethod(const google::protobuf::MethodDescriptor* method,
                  google::protobuf::RpcController* controller,
                  const google::protobuf::Message* request,
                  google::protobuf::Message* response,
                  Closure* done) {
    // Must be fully asynchronous so that EvlanTest's single context lock
    // doesn't deadlock.
    executor_->Add(std::bind(&google::protobuf::Service::CallMethod, service_,
        method, down_cast<FakeRpcController*>(controller)->partner(),
        request, response, NewCallback(executor_, &Executor::Add, done)));
  }

 private:
  google::protobuf::Service* service_;
  Executor* executor_;
};

// ===================================================================

FakeRpcSystem::~FakeRpcSystem() {}

void FakeRpcSystem::Export(const vm::proto::ServerAddress address,
                           google::protobuf::Service* service) {
  services_[address.SerializeAsString()] = service;
}
void FakeRpcSystem::Unexport(const vm::proto::ServerAddress address) {
  services_.erase(address.SerializeAsString());
}

void FakeRpcSystem::Run() {
  executor_.Loop();
}

void FakeRpcSystem::Stop() {
  executor_.StopLooping();
}

Executor* FakeRpcSystem::executor() { return &executor_; }

StreamingRpcController* FakeRpcSystem::NewController() {
  return new FakeRpcController(&executor_);
}

google::protobuf::RpcChannel* FakeRpcSystem::NewChannel(
    const vm::proto::ServerAddress& remote_address) {
  google::protobuf::Service* service =
      FindPtrOrNull(services_, remote_address.SerializeAsString());
  GOOGLE_CHECK(service != NULL);
  return new FakeRpcChannel(service, &executor_);
}

}  // namespace evlan
