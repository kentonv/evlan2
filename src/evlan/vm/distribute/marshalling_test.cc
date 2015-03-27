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

#include "evlan/vm/distribute/marshaller.h"
#include "evlan/vm/distribute/unmarshaller.h"
#include "evlan/vm/distribute/value_registry.h"
#include "evlan/vm/distribute/remote_worker.h"
#include "evlan/vm/proto/marshal.pb.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/testing/test_util.h"
#include <google/protobuf/io/printer.h>
#include "evlan/map-util.h"

#include "evlan/testing/test_util.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace distribute {
namespace {

using namespace std::placeholders;

class MockValueRegistry : public ValueRegistry, public ValueRegistrar {
 public:
  MockValueRegistry() {}
  virtual ~MockValueRegistry() {
    for (unordered_map<string, KeepAliveValue*>::iterator iter = value_map_.begin();
         iter != value_map_.end(); ++iter) {
      iter->second->Release();
    }
  }

  // implements ValueRegistry ----------------------------------------

  virtual Value Find(const ValueId& id, memory::MemoryManager* memory_manager) {
    KeepAliveValue* value = FindPtrOrNull(value_map_, id.GetBytes().as_string());
    if (value == NULL) {
      return Value();
    } else {
      return value->Get(memory_manager);
    }
  }

  // implements ValueRegistrar ---------------------------------------

  virtual void Register(const ValueId& id, Value value,
                        memory::MemoryManager* memory_manager,
                        memory::MemoryRoot* memory_root) {
    KeepAliveValue** map_slot = &value_map_[id.GetBytes().as_string()];
    if (*map_slot != NULL) {
      (*map_slot)->Release();
    }
    *map_slot = new KeepAliveValue(memory_manager, value);
  }

 private:
  unordered_map<string, KeepAliveValue*> value_map_;
};

class MockRemoteWorker : public RemoteWorker,
                         public memory::TrackedObject {
 public:
  MockRemoteWorker(memory::MemoryManager* memory_manager,
                   const proto::ServerAddress& address,
                   MockValueRegistry* persistent_value_registry)
    : address_(address),
      tracker_(memory_manager->AddTrackedObject(this)),
      persistent_value_registry_(persistent_value_registry) {}
  virtual ~MockRemoteWorker() {}

  // implements RemoteWorker -----------------------------------------
  virtual memory::ObjectTracker* GetTracker() {
    return tracker_;
  }

  virtual void GetAddress(proto::ServerAddress* output) {
    output->CopyFrom(address_);
  }

  virtual void Call(Context* context, const ValueId& id,
                    Value parameter, Value callback) {
    GOOGLE_CHECK(persistent_value_registry_ != NULL)
        << "Remote value calls were not expected for this test.";
    Value function =
        persistent_value_registry_->Find(id, context->GetMemoryManager());
    GOOGLE_CHECK(!function.IsNull());
    context->SetNext(
        persistent_value_registry_->Find(id, context->GetMemoryManager()),
        parameter, callback);
  }

  // implements TrackedObject ----------------------------------------
  virtual void NotReachable() { delete this; }
  virtual void Accept(memory::MemoryVisitor* visitor) const {}

 private:
  proto::ServerAddress address_;
  memory::ObjectTracker* tracker_;
  MockValueRegistry* persistent_value_registry_;
};

class MockRemoteWorkerFactory : public RemoteWorkerFactory {
 public:
  MockRemoteWorkerFactory() : persistent_value_registry_(NULL) {}
  virtual ~MockRemoteWorkerFactory() {}

  void AllowRemoteValues(MockValueRegistry* persistent_value_registry) {
    persistent_value_registry_ = persistent_value_registry;
  }

  // implements RemoteWorkerFactory ----------------------------------
  virtual RemoteWorker* GetRemoteWorker(memory::MemoryManager* memory_manager,
                                        const proto::ServerAddress& address) {
    return new MockRemoteWorker(memory_manager, address,
                                persistent_value_registry_);
  }

 private:
  MockValueRegistry* persistent_value_registry_;
};

class MarshallingRoundTrip {
 public:
  MarshallingRoundTrip()
      : pusher_(my_address_, &persistent_value_registry_),
        puller_(&receiver_registry_, &remote_worker_factory_) {
    my_address_.set_service_name("MyService");
  }

  void AllowRemoteValues() {
    remote_worker_factory_.AllowRemoteValues(&persistent_value_registry_);
  }

  void Run(Context* context) {
    KeepAliveValue* final_callback =
        new KeepAliveValue(context->GetMemoryManager(), context->GetCallback());

    pusher_.Start(&receiver_info_, &receiver_info_mutex_);
    puller_.Start();
    proto::PushValuesMessage push_message;
    proto::PullValuesMessage pull_message;

    pusher_.SendInitialValue(context->GetMemoryManager(),
                             context->GetParameter(),
                             &push_message);

    while (true) {
      string error;
      bool done = false;

      // GOOGLE_LOG(ERROR) << "Push: " << push_message.DebugString();

      switch (puller_.ReceivePush(context->GetMemoryManager(),
                                  push_message, &pull_message, &error)) {
        case ValuePuller::PULL:
          break;
        case ValuePuller::WAITING:
          GOOGLE_LOG(FATAL) << "Shouldn't get here";
          break;
        case ValuePuller::READY:
          done = true;
          break;
        case ValuePuller::ERROR:
          GOOGLE_LOG(FATAL) << error;
          break;
      }

      push_message.Clear();

      if (done) break;

      // GOOGLE_LOG(ERROR) << "Pull: " << pull_message.DebugString();

      GOOGLE_CHECK(pusher_.RespondToPull(context->GetMemoryManager(),
                                  pull_message, &push_message,
                                  &error))
          << error;

      pull_message.Clear();
    }

    pusher_.Finish();

    GOOGLE_CHECK(puller_.Unmarshal(context,
        std::bind(&MarshallingRoundTrip::Finish, this, final_callback, _1)));
  }

 private:
  void Finish(KeepAliveValue* final_callback, Context* context) {
    Value result =
        puller_.Finish(&receiver_registry_, context->GetMemoryManager());
    Value callback = final_callback->Get(context->GetMemoryManager());
    final_callback->Release();

    context->SetNext(callback, result);
  }

  proto::ServerAddress my_address_;

  MockValueRegistry persistent_value_registry_;
  MockValueRegistry receiver_registry_;
  ReceiverInfo receiver_info_;
  Mutex receiver_info_mutex_;
  MockRemoteWorkerFactory remote_worker_factory_;

  ValuePusher pusher_;
  ValuePuller puller_;
};

class MarshalRoundTripLogic : public Logic {
 public:
  MarshalRoundTripLogic() {}
  virtual ~MarshalRoundTripLogic() {}

  static const MarshalRoundTripLogic kSingleton;

  // implements Logic ------------------------------------------------
  virtual void Run(Context* context) const {
    context->GetData().AsMutable<MarshallingRoundTrip>()->Run(context);
  }

  virtual void Accept(const Data* data,
                      memory::MemoryVisitor* visitor) const {
    // Nothing
  }

  virtual void DebugPrint(Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("marshalRoundTrip");
  }
};

const MarshalRoundTripLogic MarshalRoundTripLogic::kSingleton;

// ===================================================================

class MarshallingTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    AddImport("doRoundTrip", Value(&MarshalRoundTripLogic::kSingleton,
                                   Data(&round_trip_)));
  }

  MarshallingRoundTrip round_trip_;
};

TEST_F(MarshallingTest, SimpleValues) {
  ExpectEvaluatesToInteger(123, "import \"doRoundTrip\"(123)");
  ExpectEvaluatesToDouble(123.5, "import \"doRoundTrip\"(123.5)");
  ExpectEvaluatesToAtom("foo", "import \"doRoundTrip\"(@foo)");
  ExpectEvaluatesToString("foo", "import \"doRoundTrip\"(\"foo\")");
}

TEST_F(MarshallingTest, Functions) {
  ExpectEvaluatesToInteger(123, "import \"doRoundTrip\"(x => x)(123)");
  ExpectEvaluatesToInteger(123,
      "import \"doRoundTrip\"((x => y => x)(123))(456)");
}

TEST_F(MarshallingTest, Arrays) {
  ExpectEvaluatesToInteger(123,
      "import \"builtin\".$array.element("
        "import \"doRoundTrip\"({123, 456, 789}), 0)");
  ExpectEvaluatesToInteger(789,
      "import \"builtin\".$array.element("
        "import \"doRoundTrip\"({123, 456, 789}), 2)");
  ExpectEvaluatesToInteger(3,
      "import \"builtin\".$array.size("
        "import \"doRoundTrip\"({123, 456, 789}))");
}

TEST_F(MarshallingTest, RemoteValues) {
  round_trip_.AllowRemoteValues();

  // We know that doRoundTrip does not implement marshalling, so it makes a
  // perfect candidate to test automatic remote values.
  ExpectEvaluatesToInteger(123,
      "import \"doRoundTrip\"(import \"doRoundTrip\")(123)");
}

}  // namespace
}  // namespace distribute
}  // namespace vm
}  // namespace evlan
