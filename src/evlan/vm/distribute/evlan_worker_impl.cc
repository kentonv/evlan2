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
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/vm/distribute/unmarshaller.h"
#include "evlan/rpc.h"

namespace evlan {
namespace vm {
namespace distribute {

using namespace std::placeholders;

class EvlanWorkerImpl::CallSession {
 public:
  CallSession(EvlanWorkerImpl* worker)
      : worker_(worker),
        context_factory_(worker->context_factory_),
        refcount_(0),
        state_(STANDBY),
        rpc_(NULL),
        final_response_(NULL),
        done_(NULL),
        pusher_(*worker->server_context_.my_address,
                worker->server_context_.persistent_value_registrar),
        puller_(worker->server_context_.known_value_registry,
                worker->worker_factory_) {}

  ~CallSession() {}

  void Start(StreamingRpcController* rpc,
             const proto::CallRequest* request,
             proto::CallResponse* response,
             Closure* done) {
    VLOG(1) << "Client -> Server: " << request->DebugString();

    MutexLock lock(&mutex_);

    GOOGLE_CHECK_EQ(state_, STANDBY);
    state_ = RECEIVING;

    receiver_info_.reset(new ReceiverInfo);

    rpc_ = rpc;
    final_response_ = response;
    done_ = done;
    Ref();  // To be unrefed when RpcDone() is called.

    Ref();  // To be unrefed when Canceled() is called.
    rpc->NotifyOnCancel(NewCallback(this, &CallSession::Canceled));

    rpc->ReceiveStream(&stream_request_,
                       NewPermanentCallback(this, &CallSession::StreamDone));

    puller_.Start();

    ReceivePush(request->initial_push());
  }

 private:
  EvlanWorkerImpl* const worker_;
  ContextFactory* const context_factory_;

  Mutex mutex_;
  int refcount_;

  enum {
    STANDBY,
    RECEIVING,
    PROCESSING,
    SENDING,
    SHUTDOWN
  } state_;

  // TODO(kenton):  Share ReceiverInfo with other requests from the same client.
  scoped_ptr<ReceiverInfo> receiver_info_;
  Mutex receiver_info_mutex_;

  StreamingRpcController* rpc_;
  proto::CallResponse* final_response_;
  Closure* done_;

  ValuePusher pusher_;
  ValuePuller puller_;

  proto::PushPullValuesMessage stream_response_;
  proto::PushPullValuesMessage stream_request_;

  // -----------------------------------------------------------------

  void Delete() {
    // Make sure that whatever thread queued the Delete call unlocks the mutex
    // before we start deleting.
    mutex_.Lock();
    mutex_.Unlock();

    GOOGLE_CHECK_EQ(refcount_, 0);

    state_ = STANDBY;

    receiver_info_.reset();

    GOOGLE_CHECK(rpc_ == NULL);
    GOOGLE_CHECK(final_response_ == NULL);
    GOOGLE_CHECK(done_ == NULL);

    stream_response_.Clear();
    stream_request_.Clear();

    worker_->call_session_factory_.Delete(this);
  }

  void Ref() {
    mutex_.AssertHeld();
    ++refcount_;
  }

  void Unref() {
    mutex_.AssertHeld();
    if (--refcount_ == 0) {
      worker_->server_context_.rpc_system->executor()->Add(
          NewCallback(this, &CallSession::Delete));
    }
  }

  // -----------------------------------------------------------------

  void StreamDone() {
    VLOG(1) << "Client -> Server: " << stream_request_.DebugString();

    MutexLock lock(&mutex_);

    switch (state_) {
      case STANDBY:
        GOOGLE_LOG(FATAL) << "Unused session object should not be receiving messages.";
        break;
      case RECEIVING:
        if (stream_request_.has_push()) {
          ReceivePush(stream_request_.push());
        } else {
          ReturnError("Expected stream message from client to contain a push.");
        }
        break;
      case PROCESSING:
        ReturnError("Unexpected stream message from client while processing "
                    "call.");
        break;
      case SENDING:
        if (stream_request_.has_pull()) {
          ReceivePull(stream_request_.pull());
        } else {
          ReturnError("Expected stream message from client to contain a pull.");
        }
        break;
      case SHUTDOWN:
        // We already built the final response and we're just waiting for
        // StopReceivingStream().  Ignore.
        break;
    }
  }

  void Canceled() {
    MutexLock lock(&mutex_);

    // We expect Canceled() to be called exactly once per request.
    // NotifyOnCancel() guarantees this:  if the client doesn't actually
    // cancel, then the notification is called after the RPC completes.
    // ReturnError() will do nothing if the RPC has already been sent.
    ReturnError("Canceled.");
    Unref();
  }

  // -----------------------------------------------------------------

  void ReturnError(StringPiece description) {
    mutex_.AssertHeld();

    if (rpc_ == NULL) {
      // Too late, we already returned a response.
      GOOGLE_CHECK_EQ(state_, SHUTDOWN);
      return;
    }

    switch (state_) {
      case STANDBY:
        GOOGLE_LOG(FATAL) << "Unused session object should not be receiving "
                      "messages.";
        break;
      case RECEIVING:
        puller_.Cancel();
        break;
      case PROCESSING:
        // We can't actually cancel the processing, so we have to wait for it
        // to finish and then notice that cancellation has occurred.
        break;
      case SENDING:
        // We assume that the client canceled after receiving all values.
        pusher_.Finish();
        break;
      case SHUTDOWN:
        GOOGLE_LOG(FATAL) << "Can't get here; response should already have been sent.";
        break;
    }

    state_ = SHUTDOWN;

    final_response_->mutable_error()->set_type(proto::PERMANENT);
    final_response_->mutable_error()->set_description(
        description.data(), description.size());

    VLOG(1) << "Server -> Client: " << final_response_->DebugString();

    rpc_->StopReceivingStream(
        NewCallback(this, &CallSession::StoppedReceivingStream, done_));

    // Clear this to make sure we can't send a second response.
    rpc_ = NULL;
    final_response_ = NULL;
    done_ = NULL;
  }

  void StoppedReceivingStream(Closure* done) {
    {
      MutexLock lock(&mutex_);
      GOOGLE_CHECK_EQ(state_, SHUTDOWN);
      Unref();
    }

    done->Run();
  }

  // -----------------------------------------------------------------

  void ReceivePush(const proto::PushValuesMessage& push) {
    mutex_.AssertHeld();
    GOOGLE_CHECK_EQ(state_, RECEIVING);

    stream_response_.Clear();

    Context context(context_factory_);
    string error;
    switch (puller_.ReceivePush(context.GetMemoryManager(),
                                push,
                                stream_response_.mutable_pull(),
                                &error)) {
      case ValuePuller::PULL:
        VLOG(1) << "Server -> Client: " << stream_response_.DebugString();
        rpc_->SendStream(stream_response_);
        break;

      case ValuePuller::WAITING:
        // Nothing.
        break;

      case ValuePuller::READY:
        Process();
        break;

      case ValuePuller::ERROR:
        ReturnError(error);
        break;
    }
  }

  void ReceivePull(const proto::PullValuesMessage& pull) {
    mutex_.AssertHeld();
    GOOGLE_CHECK_EQ(state_, SENDING);

    Context context(context_factory_);
    stream_response_.Clear();
    string error;
    if (pusher_.RespondToPull(context.GetMemoryManager(), pull,
                              stream_response_.mutable_push(),
                              &error)) {
      VLOG(1) << "Server -> Client: " << stream_response_.DebugString();
      rpc_->SendStream(stream_response_);
    } else {
      ReturnError(error);
    }
  }

  // -----------------------------------------------------------------

  void Process() {
    mutex_.AssertHeld();

    GOOGLE_CHECK_EQ(state_, RECEIVING);
    state_ = PROCESSING;

    // Start processing asynchronously.
    Ref();  // Unref() once evaluation is complete.
    worker_->server_context_.rpc_system->executor()->Add(
        NewCallback(this, &CallSession::StartProcessing));
  }

  void StartProcessing() {
    // NO LOCK:  We know that no other thread can be accessing the puller while
    //   we're in state PROCESSING.

    function<void(Context*)> done_processing =
        std::bind(&CallSession::DoneProcessing, this, _1);
    Context context(context_factory_);
    if (puller_.Unmarshal(&context, done_processing)) {
      context.Run();
    } else {
      puller_.Cancel();
      MutexLock lock(&mutex_);
      ReturnError("Client sent invalid marshalled values.");
    }
  }

  void DoneProcessing(Context* context) {
    Value result = puller_.Finish(
        worker_->server_context_.known_value_registrar,
        context->GetMemoryManager());

    MutexLock lock(&mutex_);

    if (state_ == PROCESSING) {
      state_ = SENDING;
      pusher_.Start(receiver_info_.get(), &receiver_info_mutex_);
      stream_response_.Clear();
      pusher_.SendInitialValue(context->GetMemoryManager(), result,
                               stream_response_.mutable_push());
      VLOG(1) << "Server -> Client: " << stream_response_.DebugString();
      rpc_->SendStream(stream_response_);
    } else {
      GOOGLE_CHECK_EQ(state_, SHUTDOWN);
    }

    Unref();
  }
};

// ===================================================================

EvlanWorkerImpl::EvlanWorkerImpl(ServerContext server_context,
                                 RemoteWorkerFactory* worker_factory,
                                 ContextFactory* context_factory)
    : server_context_(server_context),
      worker_factory_(worker_factory),
      context_factory_(context_factory),
      call_session_factory_(this) {}

EvlanWorkerImpl::~EvlanWorkerImpl() {
  context_factory_->RemoveReference();
}

void EvlanWorkerImpl::Call(google::protobuf::RpcController* rpc,
                           const proto::CallRequest* request,
                           proto::CallResponse* response,
                           Closure* done) {
  CallSession* session = call_session_factory_.New();
  session->Start(down_cast<StreamingRpcController*>(rpc),
                 request, response, done);
}

EvlanWorkerImpl::CallSessionFactory::CallSessionFactory(EvlanWorkerImpl* worker)
    : worker_(worker) {}
EvlanWorkerImpl::CallSessionFactory::~CallSessionFactory() {}

EvlanWorkerImpl::CallSession* EvlanWorkerImpl::CallSessionFactory::New() {
  return new CallSession(worker_);
}

void EvlanWorkerImpl::CallSessionFactory::Delete(CallSession* session) {
  delete session;
}

}  // namespace distribute
}  // namespace vm
}  // namespace evlan
