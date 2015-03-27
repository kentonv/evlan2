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
#include "evlan/vm/distribute/value_registry.h"
#include "evlan/vm/distribute/remote_worker.h"
#include "evlan/vm/proto/marshal.pb.h"
#include "evlan/map-util.h"
#include "evlan/stl-util.h"
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace distribute {

const Marshaller::ProvisionalIndex Marshaller::kInvalidIndex;
const int Marshaller::kTypeCount;

Marshaller::ProvisionalIndex Marshaller::AddValue(
    const Value& value, RequiredFlag required_flag,
    const ValueId* known_id) {
  bool old_value_is_required = value_is_required_;
  value_is_required_ = required_flag == REQUIRED;

  ProvisionalIndex result =
      value.GetLogic()->Marshal(value.GetData(), this, known_id);
  if (result == -1) {
    // Could not marshal this value.  Must treat it as retained.
    ValueId computed_id;
    if (known_id == NULL) {
      value.GetId(&computed_id);
      known_id = &computed_id;
    }

    // For auto-retained values, we use the same value ID for both the real
    // value and its stub, and we don't send the stub unless the receiver
    // asks for it.  This conveniently means that if the receiver already
    // has its own copy of the same value, it will use that instead of
    // requesting the stub from us.  This allows e.g. built-in functions to
    // be transfered without actually marshalling them.
    result = TryAvoidMarshalling(*known_id, value,
        vm::distribute::Marshaller::DONT_MARSHAL_UNLESS_REQUIRED);
    if (result == kInvalidIndex) {
      result = AddRetainedValue(*known_id, *known_id, value);
      RecordForDupElimination(value, result);
    }
  }

  value_is_required_ = old_value_is_required;

  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddInteger(int value) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_INTEGER, output_->integer_value_size());
  output_->add_integer_value(value);
  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddDouble(double value) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_DOUBLE, output_->double_value_size());
  output_->add_double_value(value);
  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddAtom(StringPiece name) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_ATOM, output_->atom_value_size());
  output_->add_atom_value(name.data(), name.size());
  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddData(StringPiece value) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_DATA, output_->data_value_size());
  output_->add_data_value(value.data(), value.size());
  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddBigData(
    const ValueId& id, const string& value) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_BIG_DATA, output_->big_data_value_size());
  proto::BigData* big_data = output_->add_big_data_value();
  if (!id.IsNull()) {
    StringPiece id_bytes = id.GetBytes();
    big_data->set_id(id_bytes.data(), id_bytes.size());
  }
  big_data->set_value(value);
  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddFunction(
    const ValueId& id,
    const Value environment[], int environment_size,
    const int32 code_tree[], int code_tree_size) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_FUNCTION, output_->function_value_size());

  proto::Function* function = output_->add_function_value();

  if (!id.IsNull()) {
    StringPiece id_bytes = id.GetBytes();
    function->set_id(id_bytes.data(), id_bytes.size());
  }

  function->mutable_environment()->Reserve(environment_size);
  for (int i = 0; i < environment_size; i++) {
    function->add_environment(AddValue(environment[i]));
  }

  function->mutable_code_tree()->Reserve(code_tree_size);
  for (int i = 0; i < code_tree_size; i++) {
    function->add_code_tree(code_tree[i]);
  }

  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddChildFunction(
    const ValueId& id,
    const Value environment[], int environment_size,
    Value parent, int code_offset_in_parent) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_FUNCTION, output_->function_value_size());

  proto::Function* function = output_->add_function_value();

  if (!id.IsNull()) {
    StringPiece id_bytes = id.GetBytes();
    function->set_id(id_bytes.data(), id_bytes.size());
  }

  function->mutable_environment()->Reserve(environment_size);
  for (int i = 0; i < environment_size; i++) {
    function->add_environment(AddValue(environment[i]));
  }

  function->set_parent(AddValue(parent));
  function->set_code_offset_in_parent(code_offset_in_parent);

  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddFunctionCall(
    const ValueId& id, Value function, Value parameter) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_FUNCTION_CALL, output_->function_call_size());

  proto::FunctionCall* call = output_->add_function_call();

  if (!id.IsNull()) {
    StringPiece id_bytes = id.GetBytes();
    call->set_id(id_bytes.data(), id_bytes.size());
  }

  call->set_function_index(AddValue(function));
  call->set_parameter_index(AddValue(parameter));

  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddFunctionCall(
    const ValueId& id, const ValueId& function_id, Value parameter) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_FUNCTION_CALL, output_->function_call_size());

  proto::FunctionCall* call = output_->add_function_call();

  if (!id.IsNull()) {
    StringPiece id_bytes = id.GetBytes();
    call->set_id(id_bytes.data(), id_bytes.size());
  }

  call->set_function_index(AddReceiverOwnedValue(function_id));
  call->set_parameter_index(AddValue(parameter));

  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddSequence(
    const ValueId& id, SequenceType type, const Value elements[], int size) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_SEQUENCE, output_->sequence_value_size());

  proto::Sequence* sequence = output_->add_sequence_value();

  if (!id.IsNull()) {
    StringPiece id_bytes = id.GetBytes();
    sequence->set_id(id_bytes.data(), id_bytes.size());
  }

  for (int i = 0; i < size; i++) {
    sequence->add_element(AddValue(elements[i]));
  }

  switch (type) {
    case ARRAY:
      // default
      break;
    case TUPLE:
      sequence->set_type(proto::Sequence::TUPLE);
      break;
  }

  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddRemoteValue(
    const ValueId& id, const ValueId& remote_id, RemoteWorker* worker) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_REMOTE, output_->remote_value_size());

  proto::RemoteValue* remote_value = output_->add_remote_value();

  if (!id.IsNull()) {
    StringPiece id_bytes = id.GetBytes();
    remote_value->set_id(id_bytes.data(), id_bytes.size());
  }
  StringPiece remote_id_bytes = remote_id.GetBytes();
  remote_value->set_remote_id(remote_id_bytes.data(), remote_id_bytes.size());

  // Add the RemoteWorker to the server address table if it isn't already there.
  pair<map<RemoteWorker*, int>::iterator, bool> insert_result =
      server_address_map_.insert(
        make_pair(worker, output_->server_address_size()));

  if (insert_result.second) {
    // First time we've seen this worker.  Add its address to the table.
    worker->GetAddress(output_->add_server_address());
  }

  remote_value->set_address_index(insert_result.first->second);

  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddRetainedValue(
    const ValueId& id, const ValueId& remote_id, Value actual_value) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_REMOTE, output_->remote_value_size());

  proto::RemoteValue* remote_value = output_->add_remote_value();

  if (!id.IsNull()) {
    StringPiece id_bytes = id.GetBytes();
    remote_value->set_id(id_bytes.data(), id_bytes.size());
  }
  StringPiece remote_id_bytes = remote_id.GetBytes();
  remote_value->set_remote_id(remote_id_bytes.data(), remote_id_bytes.size());

  if (my_address_index_ < 0) {
    // First time this method has been called.  Add our address to the table.
    my_address_index_ = output_->server_address_size();
    output_->add_server_address()->CopyFrom(session_->my_address_);
  }

  remote_value->set_address_index(my_address_index_);

  session_->RememberValue(remote_id, actual_value,
                          MarshallingSession::PERSISTENT);

  return result;
}

Marshaller::ProvisionalIndex Marshaller::AddReceiverOwnedValue(
    const ValueId& remote_id) {
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_VALUE_ID, output_->remote_value_size());
  StringPiece remote_id_bytes = remote_id.GetBytes();
  output_->add_other_value_id(remote_id_bytes.data(), remote_id_bytes.size());
  return result;
}

Marshaller::ProvisionalIndex Marshaller::TryAvoidMarshalling(
    const ValueId& id, Value value, AvoidMarshallingHint hint) {
  // Have we already marshalled this value as part of the same MarshalledValues?
  ValueMap::iterator iter = value_map_.find(value);
  if (iter != value_map_.end()) {
    // Yep.
    ProvisionalIndex result = iter->second;
    if (value_is_required_ &&
        GetProvisionalIndexType(result) == TYPE_VALUE_ID) {
      // Oops, we apparently wrote this value as an ID earlier, and now someone
      // has declared that it is required.  The caller will have to actually
      // marshal it.  This means that the MarshalledValues will end up
      // containing this value *and* an other_value_id referring to it, but
      // this is not a big deal.
      return kInvalidIndex;
    } else {
      return result;
    }
  }

  // If we were only asked to eliminate dups, stop here.
  if (hint == ELIMINATE_DUPS) return kInvalidIndex;

  // We can't use an other_value_id if the value is required.
  if (value_is_required_) return kInvalidIndex;

  if (hint == MARSHAL_IF_NOT_KNOWN) {
    // Check if we think this value is on the server already.
    if (!receiver_info_->HasSentValue(id)) return kInvalidIndex;
  }

  // Use an other_value_id.
  ProvisionalIndex result =
      MakeProvisionalIndex(TYPE_VALUE_ID, output_->other_value_id_size());
  StringPiece id_bytes = id.GetBytes();
  output_->add_other_value_id(id_bytes.data(), id_bytes.size());
  RecordForDupElimination(value, result);
  session_->RememberValue(id, value, MarshallingSession::TRANSIENT);
  return result;
}

// Record this value in a table that will allow TryAvoidMarshalling() to
// return the same index if given the same value.
void Marshaller::RecordForDupElimination(Value value, ProvisionalIndex index) {
  value_map_[value] = index;
}

Marshaller::Marshaller(MarshallingSession* session)
    : output_(NULL),
      receiver_info_(NULL),
      my_address_index_(-1),
      value_is_required_(false),
      session_(session) {}

Marshaller::~Marshaller() {}

void Marshaller::Start(proto::MarshalledValues* output,
                       const ReceiverInfo* receiver_info) {
  output_ = output;
  receiver_info_ = receiver_info;
}

void Marshaller::Clear() {
  output_ = NULL;
  receiver_info_ = NULL;
  my_address_index_ = -1;
  value_map_.clear();
  server_address_map_.clear();
}

// ===================================================================

MarshallingSession::MarshallingSession(const proto::ServerAddress& my_address)
    : marshaller_(this),
      finalized_(true),  // true until StartMarshal() is called.
      my_address_(my_address),
      total_value_count_(0),
      new_trackers_start_(0) {}

MarshallingSession::~MarshallingSession() {
  ReleaseTrackers();
}

void MarshallingSession::Clear() {
  finalized_ = true;
  total_value_count_ = 0;
  remembered_ids_.clear();

  ReleaseTrackers();
  trackers_.clear();
  new_trackers_start_ = 0;
}

void MarshallingSession::ReleaseTrackers() {
  for (int i = 0; i < new_trackers_start_; i++) {
    trackers_[i]->tracker_->RemoveExternalReference();
  }
  for (int i = new_trackers_start_; i < trackers_.size(); i++) {
    delete trackers_[i];
  }
}

Marshaller* MarshallingSession::StartMarshal(
    proto::MarshalledValues* output, const ReceiverInfo* receiver_info) {
  GOOGLE_CHECK(finalized_) << "StartMarshal() called while already marshalling.";
  marshaller_.Start(output, receiver_info);
  finalized_ = false;
  return &marshaller_;
}

Marshaller::ProvisionalIndex MarshallingSession::MarshalRememberedValue(
    memory::MemoryManager* memory_manager, const ValueId& id) {
  ValueTracker* tracker = FindPtrOrNull(remembered_ids_, &id);
  if (tracker == NULL) return Marshaller::kInvalidIndex;

  // The tracker may have been created by a different MemoryManager (e.g. in
  // a different thread).  We must call ReviveTrackedObject() to make it
  // accessible in the current context.  Otherwise, if that other MemoryManager
  // is currently being used by another thread, it may be messing with the
  // tracker's pointers while we attempt to read it.
  GOOGLE_CHECK(memory_manager->ReviveTrackedObject(tracker->tracker_))
      << "Should never fail because tracker has an external reference.";

  return marshaller_.AddValue(tracker->value_, Marshaller::REQUIRED, &id);
}

void MarshallingSession::Finalize(
    memory::MemoryManager* memory_manager,
    ValueRegistrar* persistent_value_registrar,
    ReceiverInfo* receiver_info) {
  GOOGLE_CHECK(!finalized_) << "Finalize() called without StartMarshal().";

  for (; new_trackers_start_ < trackers_.size(); new_trackers_start_++) {
    ValueTracker* tracker = trackers_[new_trackers_start_];
    if (tracker->persistence_ == PERSISTENT) {
      persistent_value_registrar->Register(
        tracker->id_, tracker->value_, memory_manager, this);
    }
    tracker->tracker_ = memory_manager->AddTrackedObject(tracker);
    tracker->tracker_->AddExternalReference();
  }

  proto::MarshalledValues* output = marshaller_.output_;
  finalized_ = true;
  marshaller_.Clear();

  int pos = 0;

#define HANDLE_TYPE(TYPE, SIZE_METHOD)                                  \
  offsets_[Marshaller::TYPE] = pos;                                     \
  pos += output->SIZE_METHOD();

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

  total_value_count_ = pos;

  for (int i = 0; i < output->function_value_size(); i++) {
    proto::Function* function = output->mutable_function_value(i);
    for (int j = 0; j < function->environment_size(); j++) {
      function->set_environment(j, ToFinalIndex(function->environment(j)));
    }
    if (function->has_parent()) {
      function->set_parent(ToFinalIndex(function->parent()));
    }
  }

  for (int i = 0; i < output->function_call_size(); i++) {
    proto::FunctionCall* call = output->mutable_function_call(i);
    call->set_function_index(ToFinalIndex(call->function_index()));
    call->set_parameter_index(ToFinalIndex(call->parameter_index()));
  }

  for (int i = 0; i < output->sequence_value_size(); i++) {
    proto::Sequence* sequence = output->mutable_sequence_value(i);
    for (int j = 0; j < sequence->element_size(); j++) {
      sequence->set_element(j, ToFinalIndex(sequence->element(j)));
    }
  }

  // Fill in the receiver_info.
  for (int i = 0; i < output->big_data_value_size(); i++) {
    if (output->big_data_value(i).has_id()) {
      receiver_info->AddSentValue(ValueId(output->big_data_value(i).id()));
    }
  }

  for (int i = 0; i < output->function_value_size(); i++) {
    if (output->function_value(i).has_id()) {
      receiver_info->AddSentValue(ValueId(output->function_value(i).id()));
    }
  }

  for (int i = 0; i < output->function_call_size(); i++) {
    if (output->function_call(i).has_id()) {
      receiver_info->AddSentValue(ValueId(output->function_call(i).id()));
    }
  }

  for (int i = 0; i < output->sequence_value_size(); i++) {
    if (output->sequence_value(i).has_id()) {
      receiver_info->AddSentValue(ValueId(output->sequence_value(i).id()));
    }
  }

  for (int i = 0; i < output->remote_value_size(); i++) {
    if (output->remote_value(i).has_id()) {
      receiver_info->AddSentValue(ValueId(output->remote_value(i).id()));
    }
  }
}

// -------------------------------------------------------------------

void MarshallingSession::Accept(memory::MemoryVisitor* visitor) const {
  for (int i = new_trackers_start_; i < trackers_.size(); i++) {
    trackers_[i]->value_.Accept(visitor);
  }
}

// -------------------------------------------------------------------

void MarshallingSession::RememberValue(const ValueId& id, Value actual_value,
                                       PersistenceFlag persistence) {
  // If a tracker for this value already exists
  ValueTracker* existing_tracker = FindPtrOrNull(remembered_ids_, &id);
  if (existing_tracker != NULL) {
    // Make sure the existing tracker has at least as strong a persistence
    // requirement was requested here.
    if (persistence == TRANSIENT ||
        existing_tracker->persistence_ == PERSISTENT) {
      return;
    }

    // If the existing tracker was created during the current marshalling
    // pass, we can go ahead and change its persistence flag because it hasn't
    // actually been processed yet.
    if (existing_tracker->tracker_ == NULL) {
      existing_tracker->persistence_ = PERSISTENT;
      return;
    }

    // Otherwise, we have to create a new tracker and overwrite the old one.
  }

  ValueTracker* tracker = new ValueTracker(id, actual_value, persistence);
  trackers_.push_back(tracker);
  remembered_ids_[&tracker->id_] = tracker;
}

// -------------------------------------------------------------------

MarshallingSession::ValueTracker::ValueTracker(
    const ValueId& id, Value value, PersistenceFlag persistence)
  : id_(id), value_(value), persistence_(persistence), tracker_(NULL) {}

MarshallingSession::ValueTracker::~ValueTracker() {}

void MarshallingSession::ValueTracker::NotReachable() {
  delete this;
}

void MarshallingSession::ValueTracker::Accept(
    memory::MemoryVisitor* visitor) const {
  value_.Accept(visitor);
}

// ===================================================================

ReceiverInfo::ReceiverInfo() {}
ReceiverInfo::ReceiverInfo(ReceiverInfo* parent) : parent_(parent) {}
ReceiverInfo::~ReceiverInfo() {}

bool ReceiverInfo::HasSentValue(const ValueId& id) const {
  if (sent_values_.count(id) > 0) return true;
  if (parent_ == NULL) return false;
  return parent_->HasSentValue(id);
}

void ReceiverInfo::AddSentValue(const ValueId& id) {
  sent_values_.insert(id);
}

void ReceiverInfo::MergeFrom(const ReceiverInfo& other) {
  for (SentValueSet::const_iterator iter = other.sent_values_.begin();
       iter != other.sent_values_.end(); ++iter) {
    sent_values_.insert(*iter);
  }
}

// ===================================================================

ValuePusher::ValuePusher(
    const proto::ServerAddress& my_address,
    ValueRegistrar* persistent_value_registrar)
  : persistent_value_registrar_(persistent_value_registrar),
    state_(STANDBY),
    receiver_info_(NULL),
    receiver_info_mutex_(NULL),
    marshalling_session_(my_address) {}

ValuePusher::~ValuePusher() {
  if (state_ != STANDBY) {
    sub_receiver_info_.Destroy();
  }
}

void ValuePusher::Start(ReceiverInfo* receiver_info,
                               Mutex* receiver_info_mutex) {
  GOOGLE_CHECK_EQ(state_, STANDBY);
  state_ = STARTED;

  receiver_info_ = receiver_info;
  receiver_info_mutex_ = receiver_info_mutex;
  sub_receiver_info_.Init(receiver_info);
}

void ValuePusher::SendInitialValue(
    memory::MemoryManager* memory_manager,
    Value value,
    proto::PushValuesMessage* push_message) {
  GOOGLE_CHECK_EQ(state_, STARTED);
  state_ = SENT_INITIAL_VALUE;

  ReaderMutexLock lock(receiver_info_mutex_);

  Marshaller* marshaller =
      marshalling_session_.StartMarshal(push_message->mutable_values(),
                                        sub_receiver_info_.get());

  Marshaller::ProvisionalIndex index = marshaller->AddValue(value);

  marshalling_session_.Finalize(memory_manager, persistent_value_registrar_,
                                sub_receiver_info_.get());

  push_message->set_final_result_index(
      marshalling_session_.ToFinalIndex(index));
}

void ValuePusher::SendFunctionCall(
    memory::MemoryManager* memory_manager,
    const ValueId& function_id, Value parameter,
    proto::PushValuesMessage* push_message) {
  GOOGLE_CHECK_EQ(state_, STARTED);
  state_ = SENT_INITIAL_VALUE;

  ReaderMutexLock lock(receiver_info_mutex_);

  Marshaller* marshaller =
      marshalling_session_.StartMarshal(push_message->mutable_values(),
                                        sub_receiver_info_.get());

  Marshaller::ProvisionalIndex index =
      marshaller->AddFunctionCall(ValueId(), function_id, parameter);

  marshalling_session_.Finalize(memory_manager, persistent_value_registrar_,
                                sub_receiver_info_.get());

  push_message->set_final_result_index(
      marshalling_session_.ToFinalIndex(index));
}

bool ValuePusher::RespondToPull(
    memory::MemoryManager* memory_manager,
    const proto::PullValuesMessage& pull_message,
    proto::PushValuesMessage* push_message,
    string* error) {
  GOOGLE_CHECK_EQ(state_, SENT_INITIAL_VALUE);
  state_ = SENT_INITIAL_VALUE;

  ReaderMutexLock lock(receiver_info_mutex_);

  marshalling_session_.StartMarshal(push_message->mutable_values(),
                                    sub_receiver_info_.get());

  for (int i = 0; i < pull_message.requested_value_size(); i++) {
    if (marshalling_session_.MarshalRememberedValue(
          memory_manager, ValueId(pull_message.requested_value(i))) ==
        Marshaller::kInvalidIndex) {
      *error = "Receiver asked for value ID that sender doesn't have.";
      return false;
    }
  }

  marshalling_session_.Finalize(memory_manager, persistent_value_registrar_,
                                sub_receiver_info_.get());

  if (pull_message.has_request_id()) {
    push_message->add_completed_request_id(pull_message.request_id());
  }

  return true;
}

void ValuePusher::Finish() {
  GOOGLE_CHECK_EQ(state_, SENT_INITIAL_VALUE);

  {
    MutexLock lock(receiver_info_mutex_);
    receiver_info_->MergeFrom(*sub_receiver_info_.get());
  }

  Cancel();
}

void ValuePusher::Cancel() {
  GOOGLE_CHECK_NE(state_, STANDBY);
  state_ = STANDBY;

  marshalling_session_.Clear();
  sub_receiver_info_.Destroy();
  receiver_info_ = NULL;
  receiver_info_mutex_ = NULL;
}

}  // namespace distribute
}  // namespace vm
}  // namespace evlan
