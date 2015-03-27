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

#ifndef EVLAN_VM_MARSHALLER_H_
#define EVLAN_VM_MARSHALLER_H_

#include <map>
#include <unordered_map>
#include <unordered_set>
#include "evlan/vm/runtime.h"
#include "evlan/common.h"

namespace evlan {
  namespace vm {
    namespace proto {
      class MarshalledValues;
      class ServerAddress;
      class PushValuesMessage;
      class PullValuesMessage;
    }
  }
}

namespace evlan {
namespace vm {
namespace distribute {

class RemoteWorker;
class ValueRegistrar;

class MarshallingSession;
class ReceiverInfo;

// Logic::Marshal() receives a Marshaller on which it calls the appropriate
// method to marshal itself.
class Marshaller {
 public:
  // To construct a Marshaller, use a MarshallingSession.

  // Methods which add a value to the Marshaller return a "provisional index".
  // This is an opaque number which identifies the value's position in the
  // MarshalledValues, but isn't a direct index yet because the Marshaller
  // doesn't yet know how many of each value will exist.  If the caller is
  // implementing Logic::Marshal(), it should simply return the provisional
  // index.  Provisional indexes can also be passed to
  // MarshallerManager::ToFinalIndex().
  typedef int ProvisionalIndex;
  static const ProvisionalIndex kInvalidIndex = -1;

  enum RequiredFlag {
    REQUIRED,
    OPTIONAL
  };

  // Calls Logic::Marshal to marshal the value, which should in turn call one
  // of the below methods.
  ProvisionalIndex AddValue(const Value& value,
                            RequiredFlag required_flag = OPTIONAL,
                            const ValueId* known_id = NULL);

  // Marshal specific types of values.  Normally called by Logic::Marshal.
  ProvisionalIndex AddInteger(int value);
  ProvisionalIndex AddDouble(double value);
  ProvisionalIndex AddAtom(StringPiece name);
  ProvisionalIndex AddData(StringPiece value);
  ProvisionalIndex AddBigData(const ValueId& id, const string& value);
  ProvisionalIndex AddFunction(
      const ValueId& id,
      const Value environment[], int environment_size,
      const int32 code_tree[], int code_tree_size);
  ProvisionalIndex AddChildFunction(
      const ValueId& id,
      const Value environment[], int environment_size,
      Value parent, int code_offset_in_parent);
  ProvisionalIndex AddFunctionCall(const ValueId& id, Value function,
                                   Value parameter);
  // Add a FunctionCall while identifying the function to call by ID only --
  // useful when calling a function that already lives on the receiver.
  ProvisionalIndex AddFunctionCall(const ValueId& id,
                                   const ValueId& function_id,
                                   Value parameter);
  enum SequenceType { ARRAY, TUPLE };
  ProvisionalIndex AddSequence(const ValueId& id, SequenceType type,
                               const Value elements[], int size);
  ProvisionalIndex AddRemoteValue(const ValueId& id,
                                  const ValueId& remote_id,
                                  RemoteWorker* worker);

  // Like AddRemoteValue() but sets the remote worker to *this* machine.  The
  // caller is NOT responsible for ensuring that the value is persistent (i.e.
  // subject to distributed garbage collection); this will be handled
  // automatically.
  ProvisionalIndex AddRetainedValue(const ValueId& id,
                                    const ValueId& remote_id,
                                    Value actual_value);

  // Adds a value that is already on the receiver.  This should only be used
  // when calling a retained value.
  ProvisionalIndex AddReceiverOwnedValue(const ValueId& remote_id);

  // Hint passed to TryAvoidMarshalling to specify how hard it should try.
  enum AvoidMarshallingHint {
    // The Marshaller should only check for dups within the current
    // MarshalledValues.  ValueId is ignored in this case.  If marshalling is
    // not avoided, then the caller must call RecordForDupElimination() after
    // marshalling.
    ELIMINATE_DUPS,

    // This value should be marshalled unless the Marshaller has good reason to
    // believe that the value is already known by the receiver.
    MARSHAL_IF_NOT_KNOWN,

    // This value should not be marshalled unless absolutely required.  It will
    // only be marshalled if the receiver explicitly requested it.
    DONT_MARSHAL_UNLESS_REQUIRED
  };

  // Try to avoid marshalling a value.  The Marshaller looks for ways that
  // marshalling can be avoided.  If it finds a way, it returns a provisional
  // index for the value, otherwise it returns kInvalidIndex.  Depending on the
  // hint, this method may add an other_value_id to the output, and return its
  // index.
  //
  // This is typically called by code which is about to marshal a large value.
  // If TryAvoidMarshalling() returns a valid index, then the calling code can
  // go ahead and use that as the provisional index.  Otherwise, it must go on
  // and call other Marshaller methods to actually marshal the value, then use
  // that.  In this case the caller should call RecordForDupElimination() after
  // marshalling the value.
  ProvisionalIndex TryAvoidMarshalling(const ValueId& id, Value value,
                                       AvoidMarshallingHint hint);

  // Record this value in a table that will allow TryAvoidMarshalling() to
  // return the same index if given the same value.
  void RecordForDupElimination(Value value, ProvisionalIndex index);

 private:
  friend class MarshallingSession;

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

  struct ValueHash {
    inline size_t operator()(const Value& value) const {
      return reinterpret_cast<intptr_t>(value.GetLogic()) * 31 +
             value.GetData().AsRaw();
    }
  };

  struct ValueEqual {
    inline bool operator()(const Value& a, const Value& b) const {
      return a.GetLogic() == b.GetLogic() &&
             a.GetData().AsRaw() == b.GetData().AsRaw();
    }
  };

  typedef unordered_map<Value, int, ValueHash, ValueEqual> ValueMap;

  // -----------------------------------------------------------------
  // Methods called by MarshallingSession

  Marshaller(MarshallingSession* session);
  ~Marshaller();

  void Start(proto::MarshalledValues* output,
             const ReceiverInfo* receiver_info);
  void Clear();

  // -----------------------------------------------------------------

  static inline ProvisionalIndex MakeProvisionalIndex(Type type, int index) {
    return (index << 4) | type;
  }

  static inline Type GetProvisionalIndexType(ProvisionalIndex index) {
    return static_cast<Type>(index & 0x0f);
  }

  static inline int GetProvisionalIndexOffset(ProvisionalIndex index) {
    return index >> 4;
  }

  // -----------------------------------------------------------------

  proto::MarshalledValues* output_;
  const ReceiverInfo* receiver_info_;
  int my_address_index_;
  ValueMap value_map_;
  map<RemoteWorker*, int> server_address_map_;

  // Forces TryAvoidMarshalling() to refuse to marshal the current value by ID.
  bool value_is_required_;

  MarshallingSession* session_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(Marshaller);
};

// Manages one entire session of marshalling, e.g. for a single RPC call (which
// may involve multiple streamed messages back and forth).
class MarshallingSession : public memory::MemoryRoot {
 public:
  // Construct a new MarshallingSession.  |my_address| is the address of the
  // server which will be sending the marshalled data.  The referenced object
  // must remain valid until the MarshallingSession is destroyed.
  MarshallingSession(const proto::ServerAddress& my_address);
  ~MarshallingSession();

  // Reset the MarshallingSession so that it can be reused for a new request.
  void Clear();

  // Begin marshalling a new set of values.  The caller should use the returned
  // Marshaller object to call Value::Marshal() for each value that needs to
  // be sent.  Finalize() must be called before using the results.  Between
  // calling StartMarshal() and Finalize(), if the thread does anything that
  // could cause a garbage collection sweep, the MarshallingSession must be
  // listed as a memory root for that sweep.
  //
  // |receiver_info| should contain ValueIds which the sender believes that
  // the receiver already has.  It must remain valid until Finalize() is called.
  // (If the ReceiverInfo is accessible by multiple threads, it is the caller's
  // responsibility to take out a reader lock on it until Finalize().)
  Marshaller* StartMarshal(proto::MarshalledValues* output,
                           const ReceiverInfo* receiver_info);

  // Add the identified value to the Marshaller last returned by StartMarshal(),
  // returning the provisional index.  Returns Marshaller::kInvalidIndex if the
  // given ID has not yet been seen in this session (which generally indicates
  // a logic error has occurred and the session will probably have to be
  // terminated).  This is intended to be used when marshalling a second set of
  // values, after the receiver has replied to the first set with a list of
  // value IDs that were listed in other_value_id and that the receiver doesn't
  // yet have.
  //
  // A MemoryManager must be provided so that values originally remembered in
  // the context of another thread can be pulled into the current tread.
  Marshaller::ProvisionalIndex MarshalRememberedValue(
      memory::MemoryManager* memory_manager, const ValueId& id);

  // Finish marshalling the set of values started by the last call to
  // StartMarshal().  Once this is called, the Marshaller returned by
  // StartMarshal() is invalidated.
  //
  // A MemoryManager must be provided to allow the MarshallingSession to hold
  // on to some values long-term, so that they can be marshalled by future
  // calls to MarshalRememberedValue().
  //
  // A ValueRegistrar must be provided for registering values which have become
  // persistent (subject to distributed GC) as a result of this marshalling.
  void Finalize(memory::MemoryManager* memory_manager,
                ValueRegistrar* persistent_value_registrar,
                ReceiverInfo* receiver_info);

  // Convert a provisional index to a final index.  The provisional index is
  // returned by one of the methods of the Marshaller object returned by the
  // last call to StartMarshal().  Finalize() must be called before calling
  // ToFinalIndex().
  int ToFinalIndex(Marshaller::ProvisionalIndex provisional);

  // implements MemoryRoot -------------------------------------------

  virtual void Accept(memory::MemoryVisitor* visitor) const;

 private:
  friend class Marshaller;

  enum PersistenceFlag {
    TRANSIENT,
    PERSISTENT
  };

  class ValueTracker : public memory::TrackedObject {
   public:
    ValueTracker(const ValueId& id, Value value, PersistenceFlag persistence);
    virtual ~ValueTracker();

    ValueId id_;
    Value value_;
    PersistenceFlag persistence_;
    memory::ObjectTracker* tracker_;

    // implements TrackedObject --------------------------------------
    virtual void NotReachable();
    virtual void Accept(memory::MemoryVisitor* visitor) const;
  };

  struct ValueIdPointerHash {
    inline size_t operator()(const ValueId* id) const {
      hash<StringPiece> hasher;
      return hasher(id->GetBytes());
    }
  };

  struct ValueIdPointerEqual {
    inline bool operator()(const ValueId* a, const ValueId* b) const {
      return a->GetBytes() == b->GetBytes();
    }
  };

  typedef unordered_map<const ValueId*, ValueTracker*,
                   ValueIdPointerHash, ValueIdPointerEqual> ValueByIdMap;

  // -----------------------------------------------------------------
  // Methods called by Marshaller.

  // Remember a value, so that it may later be recalled via
  // MarshalRememberedValue().
  void RememberValue(const ValueId& id, Value actual_value,
                     PersistenceFlag persistence);

  // -----------------------------------------------------------------

  // Release all the objects in trackers_, deleting those which are not yet
  // tracked by a MemoryManager.  Does not actually clear the vector.
  void ReleaseTrackers();

  // -----------------------------------------------------------------

  Marshaller marshaller_;
  bool finalized_;
  const proto::ServerAddress& my_address_;
  proto::MarshalledValues* output_;

  int offsets_[Marshaller::kTypeCount];
  int total_value_count_;

  // Values which we sent by ID during previous rounds of marshalling.  We
  // need to keep track of them in case the receiver sends back requests for
  // them.
  ValueByIdMap remembered_ids_;

  // All ValueTrackers created by this Marshaller.
  vector<ValueTracker*> trackers_;

  // Index into trackers_ of the first ValueTracker which is not yet actually
  // tracked by a MemoryManager -- it will start being tracked when Finalize()
  // is called.
  int new_trackers_start_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(MarshallingSession);
};

class ReceiverInfo {
 public:
  ReceiverInfo();
  ReceiverInfo(ReceiverInfo* parent);
  ~ReceiverInfo();

  bool HasSentValue(const ValueId& id) const;
  void AddSentValue(const ValueId& id);
  void MergeFrom(const ReceiverInfo& other);

 private:
  ReceiverInfo* parent_;
  typedef unordered_set<ValueId> SentValueSet;
  SentValueSet sent_values_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(ReceiverInfo);
};

// A layer over MarshallingSession explicitly meant to handle the sending end
// of a PushValues/PullValues exchange.
class ValuePusher {
 public:
  ValuePusher(const proto::ServerAddress& my_address,
              ValueRegistrar* persistent_value_registrar);
  ~ValuePusher();

  void Start(ReceiverInfo* receiver_info,
             Mutex* receiver_info_mutex);
  void SendInitialValue(memory::MemoryManager* memory_manager,
                        Value value,
                        proto::PushValuesMessage* push_message);
  void SendFunctionCall(memory::MemoryManager* memory_manager,
                        const ValueId& function_id, Value parameter,
                        proto::PushValuesMessage* push_message);
  bool RespondToPull(memory::MemoryManager* memory_manager,
                     const proto::PullValuesMessage& pull_message,
                     proto::PushValuesMessage* push_message,
                     string* error);
  void Finish();
  void Cancel();

 private:
  ValueRegistrar* const persistent_value_registrar_;

  enum State {
    STANDBY,
    STARTED,
    SENT_INITIAL_VALUE
  };
  State state_;

  ReceiverInfo* receiver_info_;
  Mutex* receiver_info_mutex_;
  ManualConstructor<ReceiverInfo> sub_receiver_info_;

  MarshallingSession marshalling_session_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(ValuePusher);
};

// ===================================================================
// inline implementations

inline int MarshallingSession::ToFinalIndex(
    Marshaller::ProvisionalIndex provisional) {
  GOOGLE_DCHECK(finalized_) << "ToFinalIndex() called before Finalize().";

  return offsets_[Marshaller::GetProvisionalIndexType(provisional)] +
         Marshaller::GetProvisionalIndexOffset(provisional);
}

}  // namespace distribute
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_MARSHALLER_H_
