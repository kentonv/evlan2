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
// This file defines a facade for the Evlan virtual machine.  It is designed
// for use by other C++ apps which intend to execute Evlan code.

#ifndef EVLAN_PUBLIC_EVALUATOR_H__
#define EVLAN_PUBLIC_EVALUATOR_H__

#include <map>
#include <string>
#include <functional>

#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/stringpiece.h"

// Forward declarations...
class Mutex;

namespace evlan {
  class Module;
  class ImportResolver;
  class CppGlue;

  namespace vm {
    class Value;
    class KeepAliveValue;
    class Context;
    class ContextFactory;

    namespace memory {
      class Allocator;
      class MallocAllocator;
      class MemoryManager;
    }

    namespace builtin {
      class AtomRegistry;
      class SupportCodeManager;
    }
  }
}

namespace google {
  namespace protobuf {
    class Message;
    class FileDescriptor;
    class MessageFactory;
  }
}

namespace evlan {

// Defined in this file.
class EvlanValue;
class EvlanEvaluator;
class AtomId;
class CustomValue;
class NonBlockingCustomValue;

// Represents an Evlan value.  To construct, either use an EvlanEvaluator or
// invoke another EvlanValue's Call() method.
class EvlanValue {
 public:
  ~EvlanValue();

  // Creates a shallow copy of this value.  (Useful for managing ownership.)
  EvlanValue* Clone();

  // Invoke the value as a function and return the result.  The caller takes
  // ownership of the returned value.
  EvlanValue* Call(EvlanValue* parameter) {
    return InternalCall(parameter, NULL);
  }

  // Like above, but passes multiple parameters to the function.
  EvlanValue* Call(const vector<EvlanValue*>& parameters) {
    return InternalCall(parameters, NULL);
  }

  // Get a named member of an object-typed value.  This is equivalent to
  // constructing an atom with the given name and then calling Call() with that
  // atom as the parameter.  As with Call(), the caller owns the result.
  EvlanValue* GetMember(const StringPiece& name) {
    return InternalGetMember(name, NULL);
  }

  // Like above but avoids blocking and returns the result via a callback.
  // The callback may be called on any thread, and may be called synchronously.
  // The callback takes ownership of the EvlanValue passed to it.
  void CallNonBlocking(EvlanValue* parameter,
                       function<void(EvlanValue*)> callback) {
    InternalCall(parameter, callback);
  }
  void CallNonBlocking(const vector<EvlanValue*>& parameters,
                       function<void(EvlanValue*)> callback) {
    InternalCall(parameters, callback);
  }
  void GetMemberNonBlocking(const StringPiece& name,
                            function<void(EvlanValue*)> callback) {
    InternalGetMember(name, callback);
  }

  // What type of value is this?
  enum Type {
    BOOLEAN,
    INTEGER,
    DOUBLE,
    ATOM,
    STRING,
    PROTOBUF,
    CUSTOM_VALUE,
    NON_BLOCKING_CUSTOM_VALUE,
    TYPE_ERROR,
    OTHER
  };
  Type GetType();

  // Convert the value into a C++ value.  Each of these functions returns true
  // if the EvlanValue is of the given type and stores the value to the pointer.
  // If the EvlanValue is not of the given type, these return false.
  //
  // TODO(kenton):  The methods returning StringPiece may not be safe if the
  //   evaluator is being accessed concurrently in another thread -- the bytes
  //   that the returned StringPiece points at could be relocated.  Change to
  //   strings instead?
  bool GetBooleanValue(bool* value);
  bool GetIntegerValue(int* value);
  bool GetDoubleValue(double* value);
  bool GetAtomValue(StringPiece* value);
  bool GetStringValue(StringPiece* value);
  bool GetProtobufValue(const google::protobuf::Message** value);
  bool GetCustomValue(CustomValue** value);
  bool GetNonBlockingCustomValue(NonBlockingCustomValue** value);
  bool GetTypeErrorDescription(StringPiece* description);

  template<typename Class>
  bool GetCppObjectValue(const Class** value);

  // Get the value of an atom based on AtomId.  As with the above methods,
  // this returns true if the value is actually an atom, false otherwise.
  //
  // WARNING:  The AtomId is no longer valid once the EvlanValue is destroyed,
  //   as atoms are garbage-collected.  If this is a problem, use
  //   GetPermanentAtomValue().
  bool GetAtomValue(AtomId* value);

  // Like GetAtomValue(), but the result remains valid even after the
  // EvlanValue is destroyed.  You should avoid using this if possible since
  // it harms the garbage collector's ability to do its job.
  bool GetPermanentAtomValue(AtomId* value);

  // If the value is a tuple, returns its size.  Otherwise, returns 1, because
  // a tuple of size 1 is the same thing as a plain old value.
  int GetTupleSize();

  // Extracts the contents of a tuple and fills in the given array with the
  // corresponding values.  The caller takes ownership of the EvlanValue
  // objects placed in the array.  If the value is not a tuple of the given
  // size, GetTupleContents() returns false and does not touch the values
  // array.  (GetTupleContents() works even on size-1 tuples, by cloning the
  // value.)
  bool GetTupleContents(int size, EvlanValue* values[]);
  bool GetTupleContents(int size, scoped_ptr<EvlanValue> values[]);

  // If the value is an array, fills *elements with its contents and returns
  // true.  Otherwise, returns false.  Ownership of the EvlanValues placed in
  // *elements is passed to the caller.
  bool GetArrayElements(vector<EvlanValue*>* elements);

  // Construct a human-readable string representing the value, for debugging
  // purposes.  |depth| indicates how deeply to look into compound values; keep
  // this low to avoid getting unexpectedly massive strings.
  string DebugString(int depth = 4);

  // -----------------------------------------------------------------
  // Hooks for Evlan-internal code

  // Get the underlying vm::Value for the given EvlanValue.  Make sure it is
  // owned by the given MemoryManager before returning.
  vm::Value InternalGetValue(vm::memory::MemoryManager* memory_manager);

 private:
  friend class EvlanEvaluator;
  class DoneCallback;
  class NonBlockingDoneCallback;

  EvlanEvaluator* evaluator_;
  vm::KeepAliveValue* value_;

  EvlanValue(EvlanEvaluator* evaluator);
  EvlanValue(EvlanEvaluator* evaluator, vm::Value value);
  EvlanValue(EvlanEvaluator* evaluator, vm::Value value,
             vm::memory::MemoryManager* value_owner);

  // Hybrid versions of the "call" methods that share code.
  EvlanValue* InternalCall(EvlanValue* parameter,
                           function<void(EvlanValue*)> callback);
  EvlanValue* InternalCall(const vector<EvlanValue*>& parameters,
                           function<void(EvlanValue*)> callback);
  EvlanValue* InternalGetMember(const StringPiece& name,
                                function<void(EvlanValue*)> callback);

  EvlanValue* InternalCall(EvlanEvaluator* evaluator,
                           const vm::Value& parameter,
                           function<void(EvlanValue*)> callback);

  // Is value_ non-NULL?  Used as mutex wait condition.
  bool IsReady();

  vm::Value GetValue();

  DISALLOW_COPY_AND_ASSIGN(EvlanValue);
};

// Class which can evaluate Evlan code.
class EvlanEvaluator {
 public:
  class Options;

  EvlanEvaluator();
  EvlanEvaluator(const Options& options);
  ~EvlanEvaluator();

  // Creates a new child of this EvlanEvaluator.  This is a temporary hack to
  // facilitate request-handling with shared data in Evlan.  At start-up,
  // construct a EvlanEvaluator and evaluate your shared data in it.  When a
  // request comes in, call Fork(), then do any per-request computation in the
  // child VM.  When done, delete the child.  This will ensure that all
  // resources allocated for the request are freed (even when using a
  // BraindeadMemoryManager) and that multiple requests may be handled in
  // separate threads.
  //
  // Notes:
  // * The child must be deleted before the parent.
  // * EvlanValues created by the parent can be applied to EvlanValues created
  //   by the child, and vice versa.  In either case, the resulting EvlanValue
  //   is considered to have been created by the child.
  // * Deleting the child immediately frees all resources allocated to it.
  // * Multiple children of a single parent EvlanEvaluator can be used in
  //   separate threads simultaneously so long as the parent EvlanEvaluator is
  //   not itself used in any of those threads at the same time.
  // * Children will not use DebugMemoryManager, even if the parent did.
  // * Once Fork() has been called on an evaluator, that evaluator becomes
  //   frozen and is not alowed to be used to construct or evaluate any new
  //   values.  Any further computation can only be done in the child
  //   evaluator(s).  You *can* call Fork() again to create multiple children.
  //   (Exception:  Values may still be cloned even if the evaluator that
  //   created them has been forked.)
  EvlanEvaluator* Fork();

  // Evaluate some code and return an EvlanValue representing the result.  The
  // caller takes ownership of the returned object.
  EvlanValue* Evaluate(const StringPiece& code);

  // Like above, but start from a pre-parsed Module (see module.proto).
  EvlanValue* Evaluate(const Module& code);

  // Like above, but the code is allowed to contain imports.  |import_resolver|
  // will be used to resolve imports.
  EvlanValue* Evaluate(const StringPiece& code,
                       ImportResolver* import_resolver);
  EvlanValue* Evaluate(const Module& code,
                       ImportResolver* import_resolver);

  // Constructs Evlan values of basic data types.
  EvlanValue* NewInteger(int value);
  EvlanValue* NewDouble(double value);
  EvlanValue* NewBoolean(bool value);
  EvlanValue* NewAtom(const StringPiece& name);
  EvlanValue* NewString(const StringPiece& value);

  // Constructs a new protocol message value.  NewProtobuf() takes ownership of
  // the message object; it will be deleted when it is no longer reachable.
  // NewProtobufReference() does not take ownership; in this case, the object
  // must remain valid until the EvlanEvaluator is destroyed.
  EvlanValue* NewProtobuf(const google::protobuf::Message* value);
  EvlanValue* NewProtobufReference(const google::protobuf::Message& value);

  // Creates a value representing a .proto file.  It has members for each
  // type declared in the file.  If the factory is not thread-safe,
  // |factory_lock| is a mutex to lock while using the factory; otherwise
  // |factory_lock| can be NULL.  The caller retains ownership of all
  // pointers; they must outlive the EvlanEvaluator.
  EvlanValue* NewProtoFile(const google::protobuf::FileDescriptor* file,
                           google::protobuf::MessageFactory* factory,
                           Mutex* factory_lock);

  // Constructs a new tuple value.  Does not take ownership of the EvlanValues
  // in the vector; they may be deleted as soon as this method returns.
  EvlanValue* NewTuple(const vector<EvlanValue*>& elements);

  // Make an array from a vector full of values.
  EvlanValue* NewArray(const vector<EvlanValue*>& elements);

  // Constructs an Evlan value that is implemented in C++.  See the CustomValue
  // interface, below.  The EvlanEvaluator takes ownership of the CustomValue
  // object and will delete it when it is no longer reachable from other
  // EvlanValues.
  EvlanValue* NewCustomValue(CustomValue* value);

  // Like NewCustomValue() but for NonBlockingCustomValue.
  EvlanValue* NewNonBlockingCustomValue(NonBlockingCustomValue* value);

  // Convert a C++ object whose type has been exported via EVLAN_EXPORT_TYPE
  // to an EvlanValue so that it may be called from Evlan code.  NewCppObject()
  // takes ownership of the object; it will be deleted when it is no longer
  // reachable.  NewCppObjectReference() does not take ownership; the object
  // must remain valid for the lifetime of the EvlanEvaluator.
  //
  // To use these methods, you must also #include "evlan/public/cpp_glue.h".
  // See the comments at the top of cpp_glue.h for more details on how to
  // export C++ classes such that they may be used in Evlan.
  template<typename Class>
  EvlanValue* NewCppObject(const Class* object);
  template<typename Class>
  EvlanValue* NewCppObjectReference(const Class& object);

  // Normally, NewCppObject() and NewCppObjectReference() build various data
  // structures the first time they see a new type.  Use
  // PreRegisterCppObjectType() to force them to build these structures
  // early.  This is especially useful when using Fork(): if each Fork()ed
  // child is going to call NewCppObject() with an object of a particular
  // type, then each child may end up building its own copy of the glue.  But
  // if you call PreRegisterCppObjectType() on the parent evaluator before
  // Fork()ing, then the children will use the glue constructed by the parent
  // and thus avoid having to rebuild it.
  template<typename Class>
  void PreRegisterCppObjectType();

  // Constructs a type error.  This should be used when an error is detected
  // that could not have happened if type checking were performed.
  EvlanValue* NewTypeError(const StringPiece& description);

  // Like calling value->Clone(), except that the returned value will be
  // associated with this EvlanEvaluator even if |value| is associated with
  // some parent evaluator (see Fork()).
  EvlanValue* CloneValue(EvlanValue* value);

  struct MemoryStats {
    // Total number of bytes allocated by this EvlanEvaluator.
    uint64 total_bytes;

    // Other stats may be added later.
  };
  MemoryStats GetMemoryStats();

  // -----------------------------------------------------------------
  // Hooks for Evlan-internal code

  // Calls AddReference() on the factory before returning.
  vm::ContextFactory* InternalGetContextFactory();

  // Creates a new EvlanValue directly from an internal vm::Value.  The given
  // Value must currently be owned by the given MemoryManager.
  EvlanValue* InternalNewValue(vm::memory::MemoryManager* memory_manager,
                               vm::Value value);

 private:
  friend class EvlanValue;

  class TrackedCustomValue;
  class TrackedNonBlockingCustomValue;
  class CustomValueLogic;
  class NonBlockingCustomValueLogic;
  class ContextFactoryImpl;

  // Protects use of the main MemoryManager (memory_manager_, below).  Note
  // that additional MemoryManagers are normally created when evaluating
  // code, so that long-running evaluation do not block access to the evaluator.
  // Also protects other mutable state in this class, though the MemoryManager
  // is the main thing that needs protecting.
  Mutex mutex_;

  EvlanEvaluator* const parent_;

  // The order of all these scoped_ptrs is important, since they must be
  // deleted in exactly the reverse order.
  scoped_ptr<CppGlue> cpp_glue_;
  scoped_ptr<vm::memory::MallocAllocator> allocator_;
  scoped_ptr<vm::memory::Allocator> pool_allocator_;
  scoped_ptr<vm::memory::MemoryManager> memory_manager_;
  scoped_ptr<vm::builtin::AtomRegistry> atom_registry_;
  scoped_ptr<vm::ContextFactory> context_factory_;
  vm::builtin::SupportCodeManager* support_code_manager_;
  bool shutting_down_;
  bool has_forked_;

  EvlanEvaluator(EvlanEvaluator* parent);

  // Set the builtin support code, which is required for most other Evlan code
  // to operate correctly.  Normally this should be obtained by parsing
  // evlan/api/builtinSupport.evlan.
  void SetSupportCode(EvlanValue* value);

  // Parse the Module embedded into the binary by //evlan/api:builtin_support
  // and set it as the support code.
  void LoadEmbeddedSupportCode();

  // Of |this| and |other|, if one evaluator is a descendent of the other
  // (created via Fork()), return that one, otherwise return NULL.
  EvlanEvaluator* FindCommonEvaluator(EvlanEvaluator* other);

  // Is this evaluator a descendent (via Fork()) of |other|?
  bool IsDescendantOf(EvlanEvaluator* other);

  // Check that Fork() has not been called.  If it has, log a DFATAL error
  // message.
  void CheckNotForked();

  DISALLOW_COPY_AND_ASSIGN(EvlanEvaluator);
};

// Can be passed to EvlatEvaluator's constructor to control its behavior.
class EvlanEvaluator::Options {
 public:
  Options();
  ~Options();

  // Use a DebugMemoryManager for garbage collection.  It's recommended that
  // if you use this, you also call UseNoSupportCode(), as DebugMemoryManager
  // is generally too slow for it.
  void UseDebugMemoryManager() { use_debug_memory_manager_ = true; }

  // Overrides the built-in support code, using the given code instead.
  void SetSupportCode(const string& code);
  void SetSupportCode(const Module& module);
  void UseNoSupportCode();

 private:
  friend class EvlanEvaluator;
  bool use_debug_memory_manager_;
  bool no_support_code_;
  scoped_ptr<Module> support_code_;

  DISALLOW_COPY_AND_ASSIGN(Options);
};

// AtomId represents an atom in a way that can be quickly compared, e.g. to
// use as a map key.  Valid AtomIds can be obtained by calling
// EvlanValue::GetAtomValue(AtomId*).  Atoms are ordered arbitrarily and the
// order may be completely different between two different EvlanEvaluators
// running the same code.
//
// WARNING:  An AtomId is no longer valid once the EvlanValue it came from
//   is destroyed, unless it was created with GetPermanentAtomValue().
class AtomId {
 public:
  inline AtomId(): ptr_(NULL) {}

  inline size_t hash() const { return reinterpret_cast<size_t>(ptr_); }

  inline bool operator==(const AtomId& other) const {return ptr_ == other.ptr_;}
  inline bool operator!=(const AtomId& other) const {return ptr_ != other.ptr_;}
  inline bool operator< (const AtomId& other) const {return ptr_ <  other.ptr_;}
  inline bool operator> (const AtomId& other) const {return ptr_ >  other.ptr_;}
  inline bool operator<=(const AtomId& other) const {return ptr_ <= other.ptr_;}
  inline bool operator>=(const AtomId& other) const {return ptr_ >= other.ptr_;}

  StringPiece GetName();

 private:
  friend class EvlanEvaluator;
  friend class EvlanValue;

  inline AtomId(const void* ptr): ptr_(ptr) {}

  const void* ptr_;
};

// So you can use AtomIds in unordered_maps.  Example:
//   unordered_map<AtomId, ValueType, AtomIdHash> my_map;
struct AtomIdHash {
  inline size_t operator()(const AtomId& id) const { return id.hash(); }
};

// Implement this interface in order to create custom values which can be
// called from Evlan code.  Call EvlanEvaluator::NewCustomValue() to inject
// your CustomValue into the Evlan VM.
class CustomValue {
 public:
  virtual ~CustomValue();

  // Invokes the value as a function.  |self| and |parameter| remain property
  // of the caller; callee must clone it if it wants to keep it.  Caller takes
  // ownership of returned value.  |self| is an EvlanValue wrapping this
  // CustomValue; it is provided so that the callee has a way to prevent the
  // CustomValue from being deleted (i.e. by Clone()ing self and not deleting
  // the result until it is safe to delete the CustomObject).
  virtual EvlanValue* Call(EvlanEvaluator* evaluator,
                           EvlanValue* self,
                           EvlanValue* parameter) = 0;

  // Implementations may optionally override this to set what
  // EvlanValue::DebugString() should return for this value.
  virtual string DebugString(int depth);
};

// Like CustomValue, but for long-running non-blocking functions.
class NonBlockingCustomValue {
 public:
  virtual ~NonBlockingCustomValue();

  // Like CustomValue::Call(), but completes asynchronously via a callback.
  // The callback may be executed in any thread.  The callback takes ownership
  // of the EvlanValue passed to it.  Remember that |self| and |parameter|
  // remain property of the caller, which may destroy them immediately after
  // CallNonBlocking() returns (without waiting for |done| to be called), so if
  // you need to keep them around longer htan that, make sure to Clone() them.
  virtual void CallNonBlocking(EvlanEvaluator* evaluator,
                               EvlanValue* self,
                               EvlanValue* parameter,
                               function<void(EvlanValue*)> done) = 0;

  // Same as CustomValue::DebugString();
  virtual string DebugString(int depth);
};

}  // namespace evlan

#endif  // EVLAN_EVALUATOR_H__
