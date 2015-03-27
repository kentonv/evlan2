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

#ifndef EVLAN_VM_DISTRIBUTE_UNMARSHALLER_H_
#define EVLAN_VM_DISTRIBUTE_UNMARSHALLER_H_

#include "evlan/vm/runtime.h"
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "evlan/common.h"
#include <google/protobuf/repeated_field.h>

namespace evlan {
  namespace vm {
    namespace proto {
      class MarshalledValues;
      class Function;
      class FunctionCall;
      class PushValuesMessage;
      class PullValuesMessage;
    }
  }
}

namespace evlan {
namespace vm {
namespace distribute {

class ValueRegistry;
class ValueRegistrar;
class RemoteWorkerFactory;

class UnmarshallingSession {
 public:
  UnmarshallingSession(ValueRegistry* registry,
                       RemoteWorkerFactory* remote_worker_factory);
  ~UnmarshallingSession();

  // Clear the object for reuse.
  void Clear();

  // Add a set of marshalled values to the session.  missing_values will be
  // filled in with any value IDs in this set which are not yet known and thus
  // must be requested from the sender, but haven't already been returned by
  // a previous call to AddValues().  Returns true if the value graph is
  // now complete, false otherwise.  Note that this method may return false
  // yet not put anything in missing_values -- this means that values from a
  // previous set are still needed.
  bool AddValues(memory::MemoryManager* memory_manager,
                 const proto::MarshalledValues& values,
                 vector<ValueId>* missing_values);

  // Return the number of values added so far.
  int GetValueCount() { return value_count_; }

  // Validate all values added using AddValues().  This cannot be called unless
  // the last call to AddValues() returned true.
  bool Validate();

  // Unmarshal all values added using AddValues().  This cannot be called unless
  // Validate() has succeeded.  Caller must run the context (e.g. call
  // context->Run()) after Unmarshal() returns.  |done| will be called when
  // unmarshalling has completed; the caller must ensure that the
  // UnmarshallingSession is not destroyed and must not call any other method on
  // it until that time.
  //
  // TODO(kenton):  Provide a way to cancel an unmarshal operation?
  void Unmarshal(Context* context, function<void(Context*)> done);

  // Get an unmarshalled value.  The index is into the concatenation of all
  // sets, so the first n values come from the first MarshalledValues added,
  // the next m come from the next, and so on.  The usual way to compute the
  // index for a particular value is to call GetValueCount() immediately before
  // calling AddValues() to add the MarshalledValues that contains it, then add
  // that to the index within that MarshalledValues.
  Value GetByIndex(int index) {
    GOOGLE_DCHECK(!final_values_topological_->empty())
        << "GetByIndex() called before Unmarshal().";
    GOOGLE_DCHECK_GE(index, 0);
    GOOGLE_DCHECK_LT(index, original_to_topological_.size());
    int topological_index = original_to_topological_[index];
    GOOGLE_DCHECK_GE(topological_index, 0);
    GOOGLE_DCHECK_LT(topological_index, final_values_topological_->size());
    return (*final_values_topological_)[topological_index];
  }

  // Like GetByIndex() but finds a value by its ValueId.  Returns a null value
  // if not found.
  Value GetById(const ValueId& id);

  // Register all values received during this session that had explicit ids, so
  // that they may potentially be reused in future sessions.
  void RegisterAll(ValueRegistrar* registrar,
                   memory::MemoryManager* memory_manager);

 private:
  enum Type {
    TYPE_INTEGER,
    TYPE_DOUBLE,
    TYPE_ATOM,
    TYPE_DATA,
    TYPE_BIG_DATA,
    TYPE_FUNCTION,
    TYPE_FUNCTION_CALL,
    TYPE_SEQUENCE,
    TYPE_REMOTE,
    TYPE_VALUE_ID
  };
  static const int kTypeCount = TYPE_VALUE_ID + 1;

  struct ValueLocation {
    int set_number;
    Type type;
    int index;

    inline ValueLocation() {}
    inline ValueLocation(int set_number, Type type, int index)
        : set_number(set_number), type(type), index(index) {}
  };

  // TrackedObject for a vector of values that grows over time.  Each
  // ValueVectorTracker actually only tracks some interval of the values, not
  // the entire vector.  Otherwise, adding new values would break any
  // existing ValueVectorTracker, since they'd now be tracking memory newer
  // than themselves without using Backlinks.
  class ValueVectorTracker : public memory::TrackedObject {
   public:
    ValueVectorTracker(const ValueVectorTracker* next,
                       memory::MemoryManager* memory_manager);
    ValueVectorTracker(const shared_ptr<vector<Value> >& values,
                       int start, memory::MemoryManager* memory_manager);
    virtual ~ValueVectorTracker();

    // The vector we are tracking (an interval of).  We use a shared_ptr since
    // multiple trackers track different intervals of this vector.
    const shared_ptr<vector<Value> > values_;

    // The interval of indexes which are "owned" by this object.
    const int start_;
    const int end_;

    // ValueVectorTracker for the interval *before* ours.
    const ValueVectorTracker* const next_;

    // ObjectTracker for this object.
    memory::ObjectTracker* const tracker_;

    // implements TrackedObject --------------------------------------
    virtual void NotReachable();
    virtual void Accept(memory::MemoryVisitor* visitor) const;
  };

  // Logic for a callback which continues the process of unmarshalling.  This
  // is used when unmarshalling a particular value happens to require calling
  // an asychronous function.  The parameter to the callback is the unmarshalled
  // value.
  //
  // The Data corresponding to this Logic is a pointer to the VectorValueTracker
  // for the final_values_topological_ vector, tracking up to whatever point it
  // was at when the callback was created.  The Data is actually NULL if this
  // is the initial callback that starts the marshalling process (in which case
  // the parameter should be ignored).
  class ContinueUnmarshallingLogic : public Logic {
   public:
    ContinueUnmarshallingLogic(UnmarshallingSession* session)
        : session_(session) {}
    virtual ~ContinueUnmarshallingLogic() {}

    // implements Logic ----------------------------------------------
    virtual void Run(Context* context) const;
    virtual void Accept(const Data* data,
                        memory::MemoryVisitor* visitor) const;
    virtual void DebugPrint(Data data, int depth,
                            google::protobuf::io::Printer* printer) const;

   private:
    UnmarshallingSession* session_;
  };

  // MemoryRoot for use while unmarshalling is in progress.
  class UnmarshallerMemoryRoot : public memory::MemoryRoot {
   public:
    UnmarshallerMemoryRoot(UnmarshallingSession* session)
        : session_(session) {}
    virtual ~UnmarshallerMemoryRoot() {}

    // implements MemoryRoot -----------------------------------------
    virtual void Accept(memory::MemoryVisitor* visitor) const;

   private:
    UnmarshallingSession* session_;
  };

  // -----------------------------------------------------------------

  // Helpers for AddValues().

  template <typename Element>
  void AddToValueMap(const google::protobuf::RepeatedPtrField<Element>& values);

  // Helpers for Validate().

  void SetupValueLocations();
  void CreateTopologicalOrdering();
  void OrderValue(int original_index);
  void OrderRelativeValue(int set_number, int index);

  // Checks if the index is actually within the given set.  If not, sets
  // valid_ to false and returns false.
  bool ValidateIndex(int set_number, int index);

  void AddToTopologicalOrder(int index) {
    original_to_topological_[index] = topological_to_original_.size();
    topological_to_original_.push_back(index);
  }

  // Helpers for Unmarshal().

  void DoSomeUnmarshalling(Context* context);

  bool UnmarshalValue(Context* context, int index);
  bool UnmarshalEnvironment(Context* context,
                            const google::protobuf::RepeatedField<int32>& indexes,
                            vector<Value>* output);
  bool UnmarshalFunction(Context* context, int index, int set_number,
                         const proto::Function& function);
  bool UnmarshalFunctionCall(Context* context, int index, int set_number,
                             const proto::FunctionCall& function_call);

  Value ContinueUnmarshallingCallback(memory::MemoryManager* memory_manager) {
    return Value(&continue_unmarshalling_logic_,
                 Data(NewFinalValuesTracker(memory_manager)));
  }

  ValueVectorTracker* NewFinalValuesTracker(
      memory::MemoryManager* memory_manager);

  // General helpers.

  void ClearRegisteredValues();

  // Assumes already validated.
  int SetIndexToOverallIndex(int set_number, int index) {
    return set_offsets_[set_number] + index;
  }

  int IdToIndex(const string& id);

  // -----------------------------------------------------------------

  ValueRegistry* registry_;
  RemoteWorkerFactory* remote_worker_factory_;
  ContinueUnmarshallingLogic continue_unmarshalling_logic_;
  UnmarshallerMemoryRoot memory_root_;

  // Stuff filled in by AddValues().

  google::protobuf::RepeatedPtrField<proto::MarshalledValues> value_sets_;
  vector<int> set_offsets_;
  int value_count_;
  unordered_map<string, int> value_id_map_;  // Maps IDs to indexes.
  unordered_set<string> missing_values_;

  typedef unordered_map<string, KeepAliveValue*> RegisteredValueMap;
  RegisteredValueMap registered_values_;

  // Stuff filled in by Validate().

  vector<string> validation_errors_;
  // Maps (original) indexes to ValueLocations for faster lookup.
  vector<ValueLocation> value_locations_;
  // Maps between original indexes and topologically-sorted indexes.
  vector<int> original_to_topological_;
  vector<int> topological_to_original_;
  // Number of values which do not require garbage collection.  These are
  // always ordered first in the topological ordering.
  int trivial_value_count_;

  // Stuff filled in by Unmarshal().

  // The final, unmarshalled values, in topological order.
  shared_ptr<vector<Value> > final_values_topological_;
  // The ValueVectorTracker used by the last "continue unmarshalling" callback.
  const ValueVectorTracker* final_values_tracker_;
  // Final callback to call when done.
  function<void(Context*)> unmarshalling_done_callback_;

  DISALLOW_EVIL_CONSTRUCTORS(UnmarshallingSession);
};

// A layer over UnmarshallingSession explicitly meant to handle the receiving
// end of a PushValues/PullValues exchange.
class ValuePuller {
 public:
  ValuePuller(ValueRegistry* registry,
              RemoteWorkerFactory* remote_worker_factory);
  ~ValuePuller();

  enum ReceiveStatus {
    // A new PullValuesMessage was constructed and needs to be sent back to
    // the sender.
    PULL,

    // All values in this push were satisfied but we're still waiting for values
    // from a previous pull.
    WAITING,

    // Unmarshal() can now be called.
    READY,

    // A logic error was detected.
    ERROR
  };

  void Start();
  ReceiveStatus ReceivePush(memory::MemoryManager* memory_manager,
                            const proto::PushValuesMessage& push_message,
                            proto::PullValuesMessage* pull_message,
                            string* error_description);
  bool Unmarshal(Context* context, function<void(Context*)> done);
  Value Finish(ValueRegistrar* registrar,
               memory::MemoryManager* memory_manager);
  void Cancel();

 private:
  enum State {
    STANDBY,
    STARTED,
    READY_TO_UNMARSHAL,
    UNMARSHALLING,
    DONE,
    FAILED
  };
  State state_;

  UnmarshallingSession unmarshalling_session_;
  vector<ValueId> missing_values_;

  int next_request_id_;
  int completed_request_id_count_;
  int final_result_index_;

  void UnmarshalDone(function<void(Context*)> done, Context* context);

  DISALLOW_EVIL_CONSTRUCTORS(ValuePuller);
};

}  // namespace distribute
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_DISTRIBUTE_UNMARSHALLER_H_
