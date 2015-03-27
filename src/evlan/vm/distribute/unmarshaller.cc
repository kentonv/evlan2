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

#include "evlan/vm/distribute/unmarshaller.h"
#include "evlan/vm/distribute/value_registry.h"
#include "evlan/vm/distribute/remote_worker.h"
#include "evlan/vm/distribute/remote_value.h"
#include "evlan/vm/proto/marshal.pb.h"
#include "evlan/vm/builtin/array.h"
#include "evlan/vm/builtin/atom.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/double.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/type_error.h"
#include "evlan/vm/interpreter/interpreter.h"
#include <google/protobuf/io/printer.h>
#include "evlan/strutil.h"
#include "evlan/map-util.h"

namespace evlan {
namespace vm {
namespace distribute {

using namespace std::placeholders;

UnmarshallingSession::UnmarshallingSession(
    ValueRegistry* registry, RemoteWorkerFactory* remote_worker_factory)
    : registry_(registry),
      remote_worker_factory_(remote_worker_factory),
      continue_unmarshalling_logic_(this),
      memory_root_(this),
      value_count_(0),
      trivial_value_count_(0),
      final_values_tracker_(NULL),
      unmarshalling_done_callback_(NULL) {}

UnmarshallingSession::~UnmarshallingSession() {
  GOOGLE_CHECK(unmarshalling_done_callback_ == NULL)
      << "UnmarshallingSession destroyed while an unmarshalling operation was "
         "in progress.  CHECK-failing here because ohterwise we'll just crash "
         "later on anyway.";

  ClearRegisteredValues();

  if (final_values_tracker_ != NULL) {
    final_values_tracker_->tracker_->RemoveExternalReference();
  }
}

void UnmarshallingSession::Clear() {
  value_sets_.Clear();
  set_offsets_.clear();
  value_count_ = 0;
  value_id_map_.clear();
  missing_values_.clear();
  ClearRegisteredValues();

  validation_errors_.clear();
  value_locations_.clear();
  original_to_topological_.clear();
  topological_to_original_.clear();
  trivial_value_count_ = 0;

  final_values_topological_.reset();
  if (final_values_tracker_ != NULL) {
    final_values_tracker_->tracker_->RemoveExternalReference();
    final_values_tracker_ = NULL;
  }
  GOOGLE_CHECK(unmarshalling_done_callback_ == NULL)
      << "UnmarshallingSession cleared while an unmarshalling operation was "
         "in progress.  CHECK-failing here because ohterwise we'll just crash "
         "later on anyway.";
}

void UnmarshallingSession::ClearRegisteredValues() {
  for (RegisteredValueMap::iterator iter = registered_values_.begin();
       iter != registered_values_.end(); ++iter) {
    iter->second->Release();
  }
  registered_values_.clear();
}

bool UnmarshallingSession::AddValues(memory::MemoryManager* memory_manager,
                                     const proto::MarshalledValues& values,
                                     vector<ValueId>* missing_values) {
  set_offsets_.push_back(value_count_);
  value_sets_.Add()->CopyFrom(values);

  value_count_ += values.integer_value_size();
  value_count_ += values.double_value_size();
  value_count_ += values.atom_value_size();
  value_count_ += values.data_value_size();

  AddToValueMap(values.big_data_value());
  value_count_ += values.big_data_value_size();

  AddToValueMap(values.function_value());
  value_count_ += values.function_value_size();

  AddToValueMap(values.function_call());
  value_count_ += values.function_call_size();

  AddToValueMap(values.sequence_value());
  value_count_ += values.sequence_value_size();

  AddToValueMap(values.remote_value());
  value_count_ += values.remote_value_size();

  value_count_ += values.other_value_id_size();

  for (int i = 0; i < values.other_value_id_size(); i++) {
    const string& id = values.other_value_id(i);
    if (value_id_map_.count(id) == 0 &&
        missing_values_.count(id) == 0 &&
        registered_values_.count(id) == 0) {
      // Look in the registry.

      Value value = registry_->Find(ValueId(id), memory_manager);
      if (value.IsNull()) {
        // Must get from client.
        missing_values_.insert(id);
        missing_values->push_back(ValueId(id));
      } else {
        // Found in the registry.  Keepalive it.
        registered_values_[id] = new KeepAliveValue(memory_manager, value);
      }
    }
  }

  return missing_values_.empty();
}

template <typename Element>
void UnmarshallingSession::AddToValueMap(
    const google::protobuf::RepeatedPtrField<Element>& values) {
  for (int i = 0; i < values.size(); i++) {
    if (values.Get(i).has_id()) {
      const string& id = values.Get(i).id();
      value_id_map_[id] = value_count_ + i;
      missing_values_.erase(id);
    }
  }
}

// ===================================================================

bool UnmarshallingSession::Validate() {
  GOOGLE_CHECK(missing_values_.empty())
      << "Validate() called after AddValues() returned false.";

  if (value_count_ == 0) {
    validation_errors_.push_back("No values were provided.");
    return false;
  }

  // Add the final value count to set_offsets_ to make ValidateIndex() easier.
  GOOGLE_CHECK_EQ(set_offsets_.size(), value_sets_.size());
  set_offsets_.push_back(value_count_);

  SetupValueLocations();

  CreateTopologicalOrdering();

  if (!validation_errors_.empty()) {
    GOOGLE_LOG(ERROR) << "Validation errors when unmarshalling values:";
    for (int i = 0; i < validation_errors_.size(); i++) {
      GOOGLE_LOG(ERROR) << "  " << validation_errors_[i];
    }
  }

  return validation_errors_.empty();
}

void UnmarshallingSession::SetupValueLocations() {
  value_locations_.resize(value_count_);

  int pos = 0;

  for (int i = 0; i < value_sets_.size(); i++) {
    const proto::MarshalledValues& value_set = value_sets_.Get(i);

#define HANDLE_TYPE(TYPE, SIZE_METHOD)                                      \
    for (int j = 0; j < value_set.SIZE_METHOD(); j++) {                     \
      value_locations_[pos++] = ValueLocation(i, TYPE, j);                  \
    }

    HANDLE_TYPE(TYPE_INTEGER      ,  integer_value_size);
    HANDLE_TYPE(TYPE_DOUBLE       ,   double_value_size);
    HANDLE_TYPE(TYPE_ATOM         ,     atom_value_size);
    HANDLE_TYPE(TYPE_DATA         ,     data_value_size);
    HANDLE_TYPE(TYPE_BIG_DATA     , big_data_value_size);
    HANDLE_TYPE(TYPE_FUNCTION     , function_value_size);
    HANDLE_TYPE(TYPE_FUNCTION_CALL,  function_call_size);
    HANDLE_TYPE(TYPE_SEQUENCE     , sequence_value_size);
    HANDLE_TYPE(TYPE_REMOTE       ,   remote_value_size);
    HANDLE_TYPE(TYPE_VALUE_ID     , other_value_id_size);
#undef HANDLE_TYPE
  }

  GOOGLE_CHECK_EQ(pos, value_count_);
}

void UnmarshallingSession::CreateTopologicalOrdering() {
  original_to_topological_.resize(value_count_, -1);
  topological_to_original_.reserve(value_count_);

  // Add all *trivial* values (i.e. ones not requiring memory management) to
  // the topological ordering first.
  for (int i = 0; i < value_count_; i++) {
    switch (value_locations_[i].type) {
      case TYPE_INTEGER:
      case TYPE_DOUBLE:
        AddToTopologicalOrder(i);
        break;
      case TYPE_ATOM:
      case TYPE_DATA:
      case TYPE_BIG_DATA:
      case TYPE_FUNCTION:
      case TYPE_FUNCTION_CALL:
      case TYPE_SEQUENCE:
      case TYPE_REMOTE:
      case TYPE_VALUE_ID:
        // Nothing.
        break;
    }
  }

  trivial_value_count_ = topological_to_original_.size();

  // Next add all values to value_
  for (int i = 0; i < value_count_; i++) {
    switch (value_locations_[i].type) {
      case TYPE_INTEGER:
      case TYPE_DOUBLE:
        // Already ordered.
        break;
      case TYPE_ATOM:
      case TYPE_DATA:
      case TYPE_BIG_DATA:
      case TYPE_REMOTE:
        AddToTopologicalOrder(i);
        break;
      case TYPE_FUNCTION:
      case TYPE_FUNCTION_CALL:
      case TYPE_SEQUENCE:
      case TYPE_VALUE_ID:
        // Complex type -- order later.
        break;
    }
  }

  // Now order the rest.
  for (int i = 0; i < value_count_; i++) {
    OrderValue(i);
  }
}

void UnmarshallingSession::OrderValue(int original_index) {
  if (original_to_topological_[original_index] != -1) {
    // We've already seen this value.
    if (original_to_topological_[original_index] == -2) {
      // This value is in-progress, meaning we have a cycle.
      validation_errors_.push_back(
          strings::Substitute("Value cyclically depended on itself: $0",
                              original_index));
    } else {
      GOOGLE_DCHECK_GE(original_to_topological_[original_index], 0);
    }
    return;
  }

  // Mark this value as in-progress.
  original_to_topological_[original_index] = -2;

  // Order this value's dependencies first.
  ValueLocation location = value_locations_[original_index];
  const proto::MarshalledValues value_set =
      value_sets_.Get(location.set_number);
  switch (location.type) {
    case TYPE_INTEGER:
    case TYPE_DOUBLE:
    case TYPE_ATOM:
    case TYPE_DATA:
    case TYPE_BIG_DATA:
    case TYPE_REMOTE:
      GOOGLE_LOG(FATAL) << "Can't get here: this should have already been ordered.";
      break;
    case TYPE_FUNCTION: {
      const proto::Function& function =
          value_set.function_value(location.index);
      if (function.has_parent()) {
        OrderRelativeValue(location.set_number, function.parent());
      }
      for (int i = 0; i < function.environment_size(); i++) {
        OrderRelativeValue(location.set_number, function.environment(i));
      }
      break;
    }
    case TYPE_FUNCTION_CALL: {
      const proto::FunctionCall& function_call =
          value_set.function_call(location.index);
      if (function_call.has_function_index()) {
        OrderRelativeValue(location.set_number, function_call.function_index());
      }
      if (function_call.has_parameter_index()) {
        OrderRelativeValue(location.set_number,
                           function_call.parameter_index());
      }
      break;
    }
    case TYPE_SEQUENCE: {
      const proto::Sequence& sequence =
          value_set.sequence_value(location.index);
      for (int i = 0; i < sequence.element_size(); i++) {
        OrderRelativeValue(location.set_number, sequence.element(i));
      }
      break;
    }
    case TYPE_VALUE_ID: {
      const string& id = value_set.other_value_id(location.index);
      int index = IdToIndex(id);
      if (index != -1) {
        OrderValue(index);
      }
      break;
    }
  }

  // Finally, add this value.
  AddToTopologicalOrder(original_index);
}

void UnmarshallingSession::OrderRelativeValue(int set_number, int index) {
  if (ValidateIndex(set_number, index)) {
    OrderValue(SetIndexToOverallIndex(set_number, index));
  }
}

bool UnmarshallingSession::ValidateIndex(int set_number, int index) {
  if (set_offsets_[set_number] + index < set_offsets_[set_number + 1]) {
    return true;
  } else {
    validation_errors_.push_back(
        strings::Substitute(
            "Set number $0 contained reference to out-of-range index: $1",
            set_number, index));
    return false;
  }
}

// ===================================================================

void UnmarshallingSession::Unmarshal(
    Context* context, function<void(Context*)> done) {
  GOOGLE_CHECK(!original_to_topological_.empty())
      << "Unmarshal() called without calling Validate().";
  GOOGLE_CHECK(validation_errors_.empty())
      << "Unmarshal() called after Validate() failed.";

  final_values_topological_.reset(new vector<Value>);
  final_values_topological_->reserve(value_count_);
  unmarshalling_done_callback_ = done;

  // Make sure all unmarshalling happens asynchronously so that the caller does
  // not have to worry about |done| being called before they call
  // context->Run().
  context->SetNext(
    Value(&continue_unmarshalling_logic_, Data()),
    builtin::MakeInteger(0));   // dummy parameter
}

void UnmarshallingSession::DoSomeUnmarshalling(Context* context) {
  while (final_values_topological_->size() < value_count_) {
    int topological_index = final_values_topological_->size();
    if (UnmarshalValue(context,
                       topological_to_original_[topological_index])) {
      // Value was unmarshalled synchronously.
      GOOGLE_DCHECK_EQ(final_values_topological_->size(), topological_index + 1);
    } else {
      // Value is being unmarshalled asynchronously.  Need to wait for callback.
      GOOGLE_DCHECK_EQ(final_values_topological_->size(), topological_index);
      return;
    }
  }

  // Make sure the values don't go away until the object is destroyed or
  // cleared.
  final_values_tracker_ = NewFinalValuesTracker(context->GetMemoryManager());
  final_values_tracker_->tracker_->AddExternalReference();

  function<void(Context*)> done = unmarshalling_done_callback_;
  unmarshalling_done_callback_ = NULL;
  done(context);
}

bool UnmarshallingSession::UnmarshalValue(Context* context, int index) {
  ValueLocation location = value_locations_[index];
  const proto::MarshalledValues value_set =
      value_sets_.Get(location.set_number);

  switch (location.type) {
    case TYPE_INTEGER:
      final_values_topological_->push_back(
          builtin::MakeInteger(value_set.integer_value(location.index)));
      return true;
    case TYPE_DOUBLE:
      final_values_topological_->push_back(
          builtin::MakeDouble(value_set.double_value(location.index)));
      return true;
    case TYPE_ATOM:
      final_values_topological_->push_back(
          context->GetAtomRegistry()->GetAtom(
            context->GetMemoryManager(),
            value_set.atom_value(location.index)));
      return true;
    case TYPE_DATA:
      final_values_topological_->push_back(
          builtin::ByteUtils::CopyString(
            context->GetMemoryManager(), &memory_root_,
            value_set.data_value(location.index)));
      return true;
    case TYPE_BIG_DATA: {
      const string& value = value_set.big_data_value(location.index).value();
      final_values_topological_->push_back(
          builtin::ByteUtils::CopyString(
            context->GetMemoryManager(), &memory_root_, value));
      return true;
    }
    case TYPE_FUNCTION:
      return UnmarshalFunction(context, index, location.set_number,
                               value_set.function_value(location.index));
    case TYPE_FUNCTION_CALL:
      return UnmarshalFunctionCall(context, index, location.set_number,
                                   value_set.function_call(location.index));
    case TYPE_SEQUENCE: {
      const proto::Sequence& sequence =
          value_set.sequence_value(location.index);

      Value* elements = context->GetMemoryManager()->AllocateArray<Value>(
          sequence.element_size(), &memory_root_);

      for (int i = 0; i < sequence.element_size(); i++) {
        elements[i] = GetByIndex(SetIndexToOverallIndex(
            location.set_number, sequence.element(i)));
      }

      Value result = builtin::ArrayUtils::AliasArray(
          context->GetMemoryManager(), &memory_root_,
          sequence.element_size(), elements);
      switch (sequence.type()) {
        case proto::Sequence::ARRAY:
          // Nothing.
          break;
        case proto::Sequence::TUPLE:
          result = builtin::ArrayUtils::ArrayToTuple(result);
          break;
      }
      final_values_topological_->push_back(result);
      return true;
    }
    case TYPE_REMOTE: {
      const proto::RemoteValue& remote_value =
          value_set.remote_value(location.index);
      RemoteWorker* worker = remote_worker_factory_->GetRemoteWorker(
          context->GetMemoryManager(),
          value_set.server_address(remote_value.address_index()));
      final_values_topological_->push_back(
          MakeRemoteValue(
            context->GetMemoryManager(), &memory_root_,
            worker, ValueId(remote_value.remote_id())));
      return true;
    }
    case TYPE_VALUE_ID: {
      const string& id = value_set.other_value_id(location.index);
      int index = IdToIndex(id);
      if (index == -1) {
        KeepAliveValue* registered_value =
            FindPtrOrNull(registered_values_, id);
        GOOGLE_CHECK(registered_value != NULL)
            << "Missing value for ID; we shouldn't have been able to get here "
               "in this case.";
        final_values_topological_->push_back(
            registered_value->Get(context->GetMemoryManager()));
      } else {
        final_values_topological_->push_back(GetByIndex(index));
      }
      return true;
    }
  }

  GOOGLE_LOG(FATAL) << "Can't get here.";
  return false;  // make compiler happy
}

bool UnmarshallingSession::UnmarshalFunction(
    Context* context, int index, int set_number,
    const proto::Function& function) {
  vector<Value> environment;
  environment.reserve(function.environment_size());
  for (int i = 0; i < function.environment_size(); i++) {
    int element_index = SetIndexToOverallIndex(
        set_number, function.environment(i));
    environment.push_back(GetByIndex(element_index));
  }

  if (function.has_parent()) {
    int parent_index = SetIndexToOverallIndex(set_number, function.parent());

    interpreter::GetSubFunction(
        context,
        GetByIndex(parent_index),
        function.code_offset_in_parent(),
        &environment[0], environment.size(),
        false,  // sub_environment_is_tracked
        ContinueUnmarshallingCallback(context->GetMemoryManager()));
  } else {
    interpreter::InterpretCodeTree(
        context,
        function.code_tree().data(),
        function.code_tree().size(),
        NULL,  // code_tree_tracker
        &environment[0], environment.size(),
        ContinueUnmarshallingCallback(context->GetMemoryManager()));
  }

  // Need to wait for callback.
  return false;
}

bool UnmarshallingSession::UnmarshalFunctionCall(
    Context* context, int index, int set_number,
    const proto::FunctionCall& function_call) {
  int function_index = SetIndexToOverallIndex(
      set_number, function_call.function_index());
  int parameter_index = SetIndexToOverallIndex(
      set_number, function_call.parameter_index());

  context->SetNext(GetByIndex(function_index),
                   GetByIndex(parameter_index),
                   ContinueUnmarshallingCallback(context->GetMemoryManager()));

  // Need to wait for callback.
  return false;
}

// -------------------------------------------------------------------

UnmarshallingSession::ValueVectorTracker::ValueVectorTracker(
    const shared_ptr<vector<Value> >& values,
    int start, memory::MemoryManager* memory_manager)
  : values_(values),
    start_(start),
    end_(values->size()),
    next_(NULL),
    tracker_(memory_manager->AddTrackedObject(this)) {}

UnmarshallingSession::ValueVectorTracker::ValueVectorTracker(
    const ValueVectorTracker* next,
    memory::MemoryManager* memory_manager)
  : values_(next->values_),
    start_(next == NULL ? 0 : next->end_),
    end_(values_->size()),
    next_(next),
    tracker_(memory_manager->AddTrackedObject(this)) {}

UnmarshallingSession::ValueVectorTracker::~ValueVectorTracker() {}

void UnmarshallingSession::ValueVectorTracker::NotReachable() {
  delete this;
}

void UnmarshallingSession::ValueVectorTracker::Accept(
    memory::MemoryVisitor* visitor) const {
  if (next_ != NULL) {
    visitor->VisitTrackedObject(next_->tracker_);
  }
  for (int i = start_; i < end_; i++) {
    (*values_)[i].Accept(visitor);
  }
}

// -------------------------------------------------------------------

void UnmarshallingSession::ContinueUnmarshallingLogic::Run(
    Context* context) const {
  session_->final_values_tracker_ = context->GetData().As<ValueVectorTracker>();

  if (session_->final_values_tracker_ == NULL) {
    GOOGLE_CHECK_EQ(session_->final_values_topological_->size(), 0)
        << "Callback called multiple times?";
  } else {
    GOOGLE_CHECK_EQ(session_->final_values_topological_->size(),
             session_->final_values_tracker_->end_)
        << "Callback called multiple times?";

    session_->final_values_topological_->push_back(context->GetParameter());
  }

  session_->DoSomeUnmarshalling(context);
}

void UnmarshallingSession::ContinueUnmarshallingLogic::Accept(
    const Data* data, memory::MemoryVisitor* visitor) const {
  const ValueVectorTracker* tracker = data->As<ValueVectorTracker>();

  if (tracker != NULL) {
    visitor->VisitTrackedObject(tracker->tracker_);
  }
}

void UnmarshallingSession::ContinueUnmarshallingLogic::DebugPrint(
    Data data, int depth, google::protobuf::io::Printer* printer) const {
  printer->Print("continueUnmarshallingCallback");
}

UnmarshallingSession::ValueVectorTracker*
UnmarshallingSession::NewFinalValuesTracker(
    memory::MemoryManager* memory_manager) {
  if (final_values_tracker_ == NULL) {
    return new ValueVectorTracker(final_values_topological_,
                                  trivial_value_count_,
                                  memory_manager);
  } else {
    return new ValueVectorTracker(final_values_tracker_, memory_manager);
  }
}

// -------------------------------------------------------------------

void UnmarshallingSession::UnmarshallerMemoryRoot::Accept(
    memory::MemoryVisitor* visitor) const {
  int last_tracked;

  if (session_->final_values_tracker_ != NULL) {
    visitor->VisitTrackedObject(session_->final_values_tracker_->tracker_);
    last_tracked = session_->final_values_tracker_->end_;
  } else {
    // We can skip the trivial values as they contain no tracked pointers.
    last_tracked = session_->trivial_value_count_;
  }

  for (int i = last_tracked; i < session_->final_values_topological_->size();
       i++) {
    (*session_->final_values_topological_)[i].Accept(visitor);
  }
}

// ===================================================================

Value UnmarshallingSession::GetById(const ValueId& id) {
  const int* index = FindOrNull(value_id_map_, id.GetBytes().as_string());
  if (index == NULL) {
    return Value();
  } else {
    return GetByIndex(*index);
  }
}

void UnmarshallingSession::RegisterAll(ValueRegistrar* registrar,
                                       memory::MemoryManager* memory_manager) {
  for (unordered_map<string, int>::iterator iter = value_id_map_.begin();
       iter != value_id_map_.end(); ++iter) {
    registrar->Register(ValueId(iter->first), GetByIndex(iter->second),
                        memory_manager, &memory_root_);
  }
}

int UnmarshallingSession::IdToIndex(const string& id) {
  const int* index = FindOrNull(value_id_map_, id);
  if (index == NULL) {
    return -1;
  } else {
    return *index;
  }
}

// ===================================================================

ValuePuller::ValuePuller(ValueRegistry* registry,
                         RemoteWorkerFactory* remote_worker_factory)
    : state_(STANDBY),
      unmarshalling_session_(registry, remote_worker_factory) {}
ValuePuller::~ValuePuller() {}

void ValuePuller::Start() {
  GOOGLE_CHECK_EQ(state_, STANDBY);
  state_ = STARTED;
  next_request_id_ = 0;
  completed_request_id_count_ = 0;
  final_result_index_ = -1;
}

ValuePuller::ReceiveStatus ValuePuller::ReceivePush(
    memory::MemoryManager* memory_manager,
    const proto::PushValuesMessage& push_message,
    proto::PullValuesMessage* pull_message,
    string* error_description) {
  GOOGLE_CHECK_EQ(state_, STARTED);

  // TODO(kenton):  Actually remember which request IDs are outstanding?
  completed_request_id_count_ += push_message.completed_request_id_size();

  if (push_message.has_final_result_index()) {
    if (final_result_index_ != -1) {
      *error_description = "Sender set final_result_index on multiple pushes.";
      state_ = FAILED;
      return ERROR;
    }

    final_result_index_ = unmarshalling_session_.GetValueCount() +
                          push_message.final_result_index();
  }

  if (unmarshalling_session_.AddValues(
        memory_manager, push_message.values(), &missing_values_)) {
    GOOGLE_CHECK(missing_values_.empty());
    if (final_result_index_ == -1) {
      *error_description = "Sender never set final_result_index.";
      state_ = FAILED;
      return ERROR;
    } else {
      state_ = READY_TO_UNMARSHAL;
      return READY;
    }
  }

  if (missing_values_.empty()) {
    if (completed_request_id_count_ < next_request_id_) {
      return WAITING;
    } else {
      *error_description =
          "Sender claims to have satisfied all pull requests, but receiver "
          "thinks some are still outstanding.";
      state_ = FAILED;
      return ERROR;
    }
  }

  for (int i = 0; i < missing_values_.size(); i++) {
    StringPiece value = missing_values_[i].GetBytes();
    pull_message->add_requested_value(value.data(), value.size());
  }
  missing_values_.clear();

  pull_message->set_request_id(next_request_id_++);

  return PULL;
}

bool ValuePuller::Unmarshal(Context* context, function<void(Context*)> done) {
  GOOGLE_CHECK_EQ(state_, READY_TO_UNMARSHAL);

  if (!unmarshalling_session_.Validate()) {
    state_ = FAILED;
    return false;
  }

  state_ = UNMARSHALLING;
  unmarshalling_session_.Unmarshal(context,
      std::bind(&ValuePuller::UnmarshalDone, this, done, _1));
  return true;
}

void ValuePuller::UnmarshalDone(function<void(Context*)> done, Context* context) {
  state_ = DONE;
  done(context);
}

Value ValuePuller::Finish(ValueRegistrar* registrar,
                          memory::MemoryManager* memory_manager) {
  GOOGLE_CHECK_EQ(state_, DONE);
  state_ = STANDBY;

  unmarshalling_session_.RegisterAll(registrar, memory_manager);
  Value result = unmarshalling_session_.GetByIndex(final_result_index_);
  unmarshalling_session_.Clear();
  return result;
}

void ValuePuller::Cancel() {
  GOOGLE_CHECK_NE(state_, STANDBY);
  GOOGLE_CHECK_NE(state_, UNMARSHALLING)
      << "Canceling during unmarshal not implemented.";
  state_ = STANDBY;

  unmarshalling_session_.Clear();
}

}  // namespace distribute
}  // namespace vm
}  // namespace evlan
