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
//
// Evlan server, which evaluates code sent to it by connected Evlan clients.

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <functional>

#include "evlan/public/evaluator.h"
#include "evlan/public/import_resolver.h"
#include "evlan/public/interactive_interpreter.h"
#include "evlan/proto/evaluator.pb.h"
#include "evlan/vm/builtin/builtin.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/array.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/type_error.h"
#include "evlan/vm/distribute/evlan_worker_impl.h"
#include "evlan/vm/distribute/value_registry_impl.h"
#include "evlan/vm/distribute/remote_value.h"
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/vm/runtime.h"

#include "evlan/common.h"
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/printer.h>
#include "evlan/stl-util.h"
#include "evlan/strutil.h"
#include "evlan/rpc.h"
#include "evlan/fake_rpc.h"

namespace evlan {
namespace {

string NiceDebugString(const google::protobuf::Message& message) {
  google::protobuf::TextFormat::Printer printer;
  printer.SetUseShortRepeatedPrimitives(true);
  string output;
  printer.PrintToString(message, &output);
  return output;
}

class EvaluatorServiceImpl : public EvaluatorService {
 public:
  EvaluatorServiceImpl(EvlanEvaluator* evaluator,
                       ImportResolver* import_resolver)
      : evaluator_(evaluator), import_resolver_(import_resolver) {}
  virtual ~EvaluatorServiceImpl() {}

  // implements EvaluatorService -------------------------------------

  virtual void Evaluate(google::protobuf::RpcController* controller,
                        const EvaluatorRequest* request,
                        EvaluatorResponse* response,
                        Closure* done) {
    GOOGLE_LOG(INFO) << "Request: " << NiceDebugString(*request);

    InteractiveInterpreter* interpreter;
    if (request->has_context_id()) {
      InteractiveInterpreter** map_slot = &interpreters_[request->context_id()];
      if (*map_slot == NULL) {
        *map_slot = new InteractiveInterpreter(evaluator_);
        (*map_slot)->MapImportPrefix("", import_resolver_);
      }
      interpreter = *map_slot;
    } else {
      interpreter = new InteractiveInterpreter(evaluator_);
      interpreter->MapImportPrefix("", import_resolver_);
      done = NewCallback(&DeleteInterpreter, interpreter, done);
    }

    vector<string> input_variables(request->input_variable().begin(),
                                   request->input_variable().end());

    interpreter->EvaluateNonBlocking(
        request->module(), input_variables,
        request->assign_to(), request->map_as_import(),
        response->mutable_result(),
        request->debug_mode() ? response->mutable_debug_info() : NULL,
        done);
  }

 private:
  EvlanEvaluator* evaluator_;
  ImportResolver* import_resolver_;
  unordered_map<uint64, InteractiveInterpreter*> interpreters_;

  static void DeleteInterpreter(InteractiveInterpreter* interpreter,
                                Closure* done) {
    delete interpreter;
    done->Run();
  }
};

// ===================================================================

class RetainedValueLogic : public vm::Logic {
 public:
  static const RetainedValueLogic kSingleton;

  RetainedValueLogic() {}

  // implements Logic ------------------------------------------------

  virtual void Run(vm::Context* context) const {
    context->SetNext(*context->GetData().As<vm::Value>(),
                     context->GetParameter(),
                     context->GetCallback());
  }

  virtual void Accept(const vm::Data* data,
                      vm::memory::MemoryVisitor* visitor) const {
    data->AcceptAs<vm::Value>(visitor);
  }

  virtual int Marshal(vm::Data data, vm::distribute::Marshaller* marshaller,
                      const vm::ValueId* known_id = NULL) const {
    vm::ValueId id;
    data.As<vm::Value>()->GetId(&id);
    return marshaller->AddRetainedValue(
        vm::ValueId(), id, *data.As<vm::Value>());
  }

  virtual void DebugPrint(vm::Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("retainedValue(");
    if (depth > 0) {
      data.As<vm::Value>()->DebugPrint(depth - 1, printer);
    } else {
      printer->Print("...");
    }
    printer->Print(")");
  }
};

const RetainedValueLogic RetainedValueLogic::kSingleton;

class RetainLogic : public vm::Logic {
 public:
  static const vm::ValueId kId;
  static const RetainLogic kSingleton;

  RetainLogic() {}

  // implements Logic ------------------------------------------------

  virtual void Run(vm::Context* context) const {
    vm::Value* retained_value = context->Allocate<vm::Value>();
    *retained_value = context->GetParameter();
    context->Return(
        vm::Value(&RetainedValueLogic::kSingleton, vm::Data(retained_value)));
  }

  virtual void Accept(const vm::Data* data,
                      vm::memory::MemoryVisitor* visitor) const {
    // nothing
  }

  virtual void GetId(vm::Data data, vm::ValueId* output) const {
    *output = kId;
  }

  virtual void DebugPrint(vm::Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("retain");
  }
};

const vm::ValueId RetainLogic::kId("<retain");
const RetainLogic RetainLogic::kSingleton;

struct ParallelState {
  vm::KeepAliveValue* callback;
  vm::KeepAliveValue* default_result;

  Mutex mutex;
  int num_left;
  int abandon_count;
  vector<vm::KeepAliveValue*> results;
};

struct ParallelElementState {
  int index;
  ParallelState* state;
};

class ParallelCallback : public vm::Logic {
 public:
  static const ParallelCallback kSingleton;

  ParallelCallback() {}

  // implements Logic ------------------------------------------------

  virtual void Run(vm::Context* context) const {
    ParallelElementState* element_state =
        context->GetData().AsMutable<ParallelElementState>();
    ParallelState* state = element_state->state;

    bool should_delete = false;

    {
      MutexLock lock(&state->mutex);

      --state->num_left;

      if (vm::builtin::TypeErrorUtils::IsError(context->GetParameter()) &&
          state->abandon_count > 0) {
        // Abandon this one.
        --state->abandon_count;
      } else if (state->num_left >= state->abandon_count) {
        state->results[element_state->index] =
            new vm::KeepAliveValue(context->GetMemoryManager(),
                                   context->GetParameter());

        if (state->num_left == state->abandon_count) {
          vm::Value* results = context->AllocateArray<vm::Value>(
              state->results.size());

          for (int i = 0; i < state->results.size(); i++) {
            if (state->results[i] == NULL) {
              results[i] = state->default_result->Get(
                  context->GetMemoryManager());
            } else {
              results[i] = state->results[i]->Get(context->GetMemoryManager());
              state->results[i]->Release();
            }
          }

          state->default_result->Release();

          vm::Value final_result = vm::builtin::ArrayUtils::AliasArray(
              context->GetMemoryManager(), context,
              state->results.size(), results);

          context->SetNext(state->callback->Get(context->GetMemoryManager()),
                           final_result);
          state->callback->Release();
        }
      }

      if (state->num_left == 0) {
        should_delete = true;
      }

      delete element_state;
    }

    if (should_delete) delete state;
  }

  virtual void Accept(const vm::Data* data,
                      vm::memory::MemoryVisitor* visitor) const {
  }

  virtual void DebugPrint(vm::Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("parallelCallback");
  }
};

const ParallelCallback ParallelCallback::kSingleton;

class ParallelLogic : public vm::Logic {
 public:
  static const vm::ValueId kId;
  static const ParallelLogic kSingleton;

  ParallelLogic() {}

  // implements Logic ------------------------------------------------

  virtual void Run(vm::Context* context) const {
    using vm::builtin::ArrayUtils;

    if (!ArrayUtils::IsTuple(context->GetParameter()) ||
        ArrayUtils::GetArraySize(context->GetParameter()) != 3) {
      context->Return(vm::builtin::TypeErrorUtils::MakeError(context,
          "Wrong type for parallel()."));
      return;
    }

    vm::Value funcs = ArrayUtils::GetElement(context->GetParameter(), 0);
    vm::Value abandon_count =
        ArrayUtils::GetElement(context->GetParameter(), 1);
    vm::Value default_result =
        ArrayUtils::GetElement(context->GetParameter(), 2);

    if (!ArrayUtils::IsArray(funcs) ||
        !vm::builtin::IsInteger(abandon_count)) {
      context->Return(vm::builtin::TypeErrorUtils::MakeError(context,
          "Wrong type for parallel()."));
      return;
    }

    int count = ArrayUtils::GetArraySize(funcs);

    if (count == 0) {
      context->Return(funcs);
      return;
    }

    const vm::Value* values = ArrayUtils::AsCArray(funcs);

    ParallelState* state = new ParallelState;
    state->abandon_count = vm::builtin::GetIntegerValue(abandon_count);
    state->default_result = new vm::KeepAliveValue(context->GetMemoryManager(),
                                                   default_result);
    state->callback = new vm::KeepAliveValue(context->GetMemoryManager(),
                                             context->GetCallback());
    state->results.resize(count);
    state->num_left = count;

    vm::ContextFactory* context_factory = context->GetContextFactory();

    for (int i = 1; i < count; i++) {
      ParallelElementState* element_state = new ParallelElementState;
      element_state->index = i;
      element_state->state = state;
      vm::KeepAliveValue* function =
          new vm::KeepAliveValue(context->GetMemoryManager(), values[i]);

      context_factory->AddReference();
      context->GetExecutor()->Add(
        std::bind(&RunParallel, context_factory, function,
          vm::Value(&ParallelCallback::kSingleton, vm::Data(element_state))));
    }

    ParallelElementState* element_state = new ParallelElementState;
    element_state->index = 0;
    element_state->state = state;
    context->SetNext(values[0], vm::builtin::MakeInteger(-1),
        vm::Value(&ParallelCallback::kSingleton, vm::Data(element_state)));
  }

  virtual void Accept(const vm::Data* data,
                      vm::memory::MemoryVisitor* visitor) const {
    // nothing
  }

  virtual void GetId(vm::Data data, vm::ValueId* output) const {
    *output = kId;
  }

  virtual void DebugPrint(vm::Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("parallel");
  }

 private:
  static void RunParallel(vm::ContextFactory* context_factory,
                          vm::KeepAliveValue* function,
                          vm::Value callback) {
    vm::Context context(context_factory);
    context_factory->RemoveReference();
    context.SetNext(function->Get(context.GetMemoryManager()),
                    vm::builtin::MakeInteger(0), callback);
    function->Release();
    context.Run();
  }
};

const vm::ValueId ParallelLogic::kId("<parallel");
const ParallelLogic ParallelLogic::kSingleton;

class FileSizeLogic : public vm::Logic {
 public:
  static const vm::ValueId kId;
  static const FileSizeLogic kSingleton;

  FileSizeLogic() {}

  // implements Logic ------------------------------------------------

  virtual void Run(vm::Context* context) const {
    if (!vm::builtin::ByteUtils::IsByteArray(context->GetParameter())) {
      context->Return(vm::builtin::TypeErrorUtils::MakeError(context,
          "Wrong type for fileSize()."));
      return;
    }

    StringPiece filename =
        vm::builtin::ByteUtils::ToString(context->GetParameter());

    int64 size = File::GetFileSize(filename.ToString().c_str());

    if (size == -1) {
      context->Return(vm::builtin::TypeErrorUtils::MakeError(context,
          strings::Substitute("File not found: $0", filename.as_string())));
      return;
    }

    context->Return(vm::builtin::MakeInteger(size));
  }

  virtual void Accept(const vm::Data* data,
                      vm::memory::MemoryVisitor* visitor) const {
    // nothing
  }

  virtual void GetId(vm::Data data, vm::ValueId* output) const {
    *output = kId;
  }

  virtual void DebugPrint(vm::Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("fileSize");
  }
};

const vm::ValueId FileSizeLogic::kId("<fileSize");
const FileSizeLogic FileSizeLogic::kSingleton;

class FileReadLogic : public vm::Logic {
 public:
  static const vm::ValueId kId;
  static const FileReadLogic kSingleton;

  FileReadLogic() {}

  // implements Logic ------------------------------------------------

  virtual void Run(vm::Context* context) const {
    using vm::builtin::ArrayUtils;
    using vm::builtin::ByteUtils;
    const vm::Value& parameter = context->GetParameter();

    if (!ArrayUtils::IsTuple(parameter) ||
        ArrayUtils::GetArraySize(parameter) != 3 ||
        !ByteUtils::IsByteArray(ArrayUtils::GetElement(parameter, 0)) ||
        !vm::builtin::IsInteger(ArrayUtils::GetElement(parameter, 1)) ||
        !vm::builtin::IsInteger(ArrayUtils::GetElement(parameter, 2))) {
      context->Return(vm::builtin::TypeErrorUtils::MakeError(context,
          "Wrong type for fileRead()."));
      return;
    }

    StringPiece filename =
        ByteUtils::ToString(ArrayUtils::GetElement(parameter, 0));
    int start = vm::builtin::GetIntegerValue(
        ArrayUtils::GetElement(parameter, 1));
    int end = vm::builtin::GetIntegerValue(
        ArrayUtils::GetElement(parameter, 2));

    if (start < 0 || end < start) {
      context->Return(vm::builtin::TypeErrorUtils::MakeError(context,
          "Start or end position for fileRead() was invalid."));
      return;
    }

    int size = end - start;

    File* file = File::Open(filename.ToString(), "r");
    if (file == NULL) {
      context->Return(vm::builtin::TypeErrorUtils::MakeError(context,
          StrCat("File not found: ", filename)));
      return;
    }

    vm::byte* buffer = context->AllocateArray<vm::byte>(size);

    size = file->PRead(start, buffer, size);

    if (size < 0) {
      context->Return(vm::builtin::TypeErrorUtils::MakeError(context,
          StrCat("Error reading file: ", filename)));
      return;
    }

    context->Return(ByteUtils::AliasTrackedBytes(
        context->GetMemoryManager(), context, buffer, size));
  }

  virtual void Accept(const vm::Data* data,
                      vm::memory::MemoryVisitor* visitor) const {
    // nothing
  }

  virtual void GetId(vm::Data data, vm::ValueId* output) const {
    *output = kId;
  }

  virtual void DebugPrint(vm::Data data, int depth,
                          google::protobuf::io::Printer* printer) const {
    printer->Print("fileRead");
  }
};

const vm::ValueId FileReadLogic::kId("<fileRead");
const FileReadLogic FileReadLogic::kSingleton;

// ===================================================================
// Half-assed attempt to resolve the worker addresses.

void Resolve(const string& string_address,
             const vm::proto::ServerAddress& my_address,
             vector<vm::proto::ServerAddress>* addresses) {
  if (string_address.empty()) return;

  // TODO(kenton):  Resolve string_address, and if it is not my_address, then add
  //   it to addresses.
  GOOGLE_LOG(FATAL) << "Not implemented.";
}

// ===================================================================

int ServerMain(int argc, char* argv[]) {
  GOOGLE_CHECK_EQ(argc, 1) << "Unrecognized arg: " << argv[1];

  FakeRpcSystem rpc_system;

  EvlanEvaluator evaluator;
  MappedImportResolver import_resolver;

  vm::proto::ServerAddress my_address = GetMyAddress();

  EvaluatorServiceImpl evaluator_service(&evaluator, &import_resolver);
  rpc_system.Export(my_address, &evaluator_service);

  // -----------------------------------------------------------------

  vm::distribute::PersistentValueRegistry persistent_value_registry;
  vm::distribute::CachedValueRegistry cached_value_registry(
      &persistent_value_registry);

  vm::distribute::RemoteWorkerFactoryImpl::ServerContext server_context;
  server_context.my_address = &my_address;
  server_context.rpc_system = &rpc_system;
  server_context.persistent_value_registrar = &persistent_value_registry;
  server_context.known_value_registry = &cached_value_registry;
  server_context.known_value_registrar = &cached_value_registry;

  vm::distribute::RemoteWorkerFactoryImpl remote_worker_factory(
      server_context);
  vm::distribute::EvlanWorkerImpl evlan_worker(
      server_context, &remote_worker_factory,
      evaluator.InternalGetContextFactory());

  rpc_system.Export(my_address, &evlan_worker);

  // -----------------------------------------------------------------

  vector<vm::proto::ServerAddress> addresses;

  GOOGLE_LOG(INFO) << "Resolving workers...";
  vector<string> string_addresses;
  SplitStringUsing(FLAGS_workers, ",", &string_addresses);
  for (int i = 0; i < string_addresses.size(); i++) {
    Resolve(string_addresses[i], my_address, &addresses);
  }
  GOOGLE_LOG(INFO) << "Done resolving workers.";

  {
    vm::ContextFactory* factory = evaluator.InternalGetContextFactory();
    vm::Context context(factory);
    factory->RemoveReference();
    vm::memory::NullMemoryRoot memory_root;

    vm::builtin::RegisterBuiltinLibrary(
        &persistent_value_registry, context.GetMemoryManager(), &memory_root);

    vm::Value retain(&RetainLogic::kSingleton, vm::Data());
    persistent_value_registry.Register(RetainLogic::kId,
        retain, context.GetMemoryManager(), &memory_root);
    vm::Value parallel(&ParallelLogic::kSingleton, vm::Data());
    persistent_value_registry.Register(ParallelLogic::kId,
        parallel, context.GetMemoryManager(), &memory_root);
    vm::Value filesize(&FileSizeLogic::kSingleton, vm::Data());
    persistent_value_registry.Register(FileSizeLogic::kId,
        filesize, context.GetMemoryManager(), &memory_root);
    vm::Value fileread(&FileReadLogic::kSingleton, vm::Data());
    persistent_value_registry.Register(FileReadLogic::kId,
        fileread, context.GetMemoryManager(), &memory_root);

    scoped_ptr<EvlanValue> retain_value(
        evaluator.InternalNewValue(context.GetMemoryManager(), retain));
    import_resolver.Map("retain", retain_value.get());
    scoped_ptr<EvlanValue> parallel_value(
        evaluator.InternalNewValue(context.GetMemoryManager(), parallel));
    import_resolver.Map("parallel", parallel_value.get());
    scoped_ptr<EvlanValue> filesize_value(
        evaluator.InternalNewValue(context.GetMemoryManager(), filesize));
    import_resolver.Map("filesize", filesize_value.get());
    scoped_ptr<EvlanValue> fileread_value(
        evaluator.InternalNewValue(context.GetMemoryManager(), fileread));
    import_resolver.Map("fileread", fileread_value.get());

    vector<EvlanValue*> distribute_funcs;
    for (int i = 0; i < addresses.size(); i++) {
      vm::distribute::RemoteWorker* worker =
          remote_worker_factory.GetRemoteWorker(context.GetMemoryManager(),
                                                addresses[i]);
      vm::Value remote_value = vm::distribute::MakeRemoteValue(
          context.GetMemoryManager(), &memory_root, worker, RetainLogic::kId);
      distribute_funcs.push_back(
          evaluator.InternalNewValue(context.GetMemoryManager(), remote_value));
    }

    scoped_ptr<EvlanValue> distribute_array(
        evaluator.NewArray(distribute_funcs));

    import_resolver.Map("machines", distribute_array.get());
    STLDeleteElements(&distribute_funcs);
  }

  // -----------------------------------------------------------------

  rpc_system.Run();

  return 0;
}

}  // namespace
}  // namespace evlan

int main(int argc, char* argv[]) {
  return evlan::ServerMain(argc, argv);
}
