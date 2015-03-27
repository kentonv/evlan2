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
// This file contains the core classes used throughout the Evlan VM's
// internals.

#ifndef EVLAN_VM_RUNTIME_BASE_H__
#define EVLAN_VM_RUNTIME_BASE_H__

#include <string>
#include "evlan/vm/base.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/common.h"
#include "evlan/stringpiece.h"

namespace google {
  namespace protobuf {
    namespace io {
      class Printer;                    // google/protobuf/io/public/printer.h
    }
  }
}

namespace evlan {
  class EvlanEvaluator;
  namespace vm {
    class Hasher;
    namespace builtin {
      class AtomRegistry;
      class SupportCodeManager;
    }
    namespace distribute {
      class Marshaller;
    }
  }
}

namespace evlan {
namespace vm {

class ValueId;
class Data;
class Logic;
class Value;
class Context;
class ContextFactory;

// Uniquely identifies a value.  Used by the marshalling protocol to help avoid
// sending the same value multiple times and eliminate duplicates.
//
// This class is a POD.  This is important because it is commonly stored in
// tracked memory.
//
// By convention, ValueIds should have a one-character prefix indicating their
// general type.  The purpose of this convention is debugging only; nothing
// should ever rely on the ability to parse IDs, as the encoding may change at
// any time.
//
// The following prefixes are suggested:
//   prefix   type                rest of ID is
//   =========================================================
//   *        unknown type        some sort of unique bytes
//   #        integer             4-byte little-endian integer
//   %        double              8-byte little-endian double
//   @        atom                atom name
//   ?        boolean             "t" or "f"
//   "        long byte string    hash of bytes
//   '        short byte string   content of string
//   !        type error          hash of contents
//   {        array               hash of contents
//   (        tuple               hash of contents
//   >        function            hash of code and environment
//   <        stateless built-in  unique name
//   ^        stateful built-in   hash of unique type name and contents
//   ~        remote value        hash of remote address and value ID
//   =        overlong ID         see kMaxValueIdSize, below
//
// Hashes should always be computed using evlan::vm::Hasher.
class ValueId {
 public:
  // If you attempt to create a ValueId longer than this, it will automatically
  // be replaced with a hash of the ID and prefixed with '='.
  static const int kMaxValueIdSize = 24;

  ValueId() : size_(0) {}
  explicit ValueId(StringPiece bytes) { SetBytes(bytes); }

  StringPiece GetBytes() const { return StringPiece(bytes_, size_); }
  void SetBytes(StringPiece bytes);

  void SetBytesWithPrefix(char prefix, StringPiece bytes);
  void SetBytesFromHasher(char prefix, Hasher* hasher);

  template <typename Type>
  void SetBytesToRawValue(char prefix, const Type& value) {
    SetBytesWithPrefix(prefix,
        StringPiece(reinterpret_cast<const char*>(&value), sizeof(value)));
  }

  bool IsNull() const { return size_ == 0; }

  // Basic operators, defined mostly so that ValueId can be used in STL
  // containers.  A version of hash<> is also defined at the end of this file.
#define OPERATOR(OP)                                                      \
  inline bool operator OP (const ValueId& other) const {                  \
    return GetBytes() OP other.GetBytes();                                \
  }
  OPERATOR(==);
  OPERATOR(!=);
  OPERATOR(<=);
  OPERATOR(>=);
  OPERATOR(<);
  OPERATOR(>);
#undef OPERATOR

 private:
  int size_;
  char bytes_[kMaxValueIdSize];
};

// Represents some dynamic data which, when combined with a Logic, forms a
// Value.  A Data is really just 64 arbitrary bits.  Usually, these bits
// contain a pointer, so we provide methods which make it easy to access
// as a pointer.
class Data {
 public:
  inline Data() { pointer_ = NULL; }

  inline explicit Data(const void* ptr) {
    pointer_ = ptr;
  }
  inline explicit Data(void* ptr) {
    mutable_pointer_ = ptr;
  }
  inline explicit Data(int value) {
    int_value_ = value;
  }
  inline explicit Data(double value) {
    double_value_ = value;
  }

  template <typename Type>
  inline const Type* As() const {
    return reinterpret_cast<const Type*>(pointer_);
  }
  template <typename Type>
  inline Type* AsMutable() const {
    return reinterpret_cast<Type*>(mutable_pointer_);
  }

  inline int AsInteger() const { return int_value_; }

  inline double AsDouble() const { return double_value_; }

  inline uint64 AsRaw() const { return raw_value_; }

  // Visit the pointer with a MemoryVisitor.
  template <typename Type>
  inline void AcceptAs(memory::MemoryVisitor* visitor) const {
    visitor->VisitPointer(reinterpret_cast<const Type* const*>(&pointer_));
  }

 private:
  union {
    const void* pointer_;
    void* mutable_pointer_;
    int int_value_;
    double double_value_;
    uint64 raw_value_;
  };
};

// Defines application logic to go with some data.  Normally, a Logic contains
// information generated at compile time (or module load time) whereas Data
// contains information that is only known at runtime.
class Logic {
 public:
  virtual ~Logic();

  // Evaluate the function represented by the combination of this Logic with
  // some data.  |context| provides the data, as well as the parameter to the
  // function and a callback that should be applied to the result.  Run() may
  // return before the function is actually complete.  Usually, Run() will call
  // context->SetNext() to instruct the caller what funtion should be called
  // next.  If Run() does not call SetNext() then there is no more computation
  // to be done at this time; the function may have already invoked the
  // callback, or it may be waiting for an asynchronous event to occur.
  //
  // Most callers should use Context::Run() rather than calling Logic::Run()
  // directly.
  virtual void Run(Context* context) const = 0;

  // -----------------------------------------------------------------
  // Memory management

  // Does something like:
  //   data->AcceptAs<Type>(visitor);
  // Also, if any special action is needed to visit the Logic itself (e.g.
  // visiting an ObjectTracker associated with it), Accept() should
  // do that.
  virtual void Accept(const Data* data,
                      memory::MemoryVisitor* visitor) const = 0;

  // -----------------------------------------------------------------
  // Marshalling

  // Get the globally-unique ID for this value.  The default implementation
  // generates an ID randomly, but most values should make some sort of attempt
  // at generating a deterministic ID to allow values with the same ID to be
  // dup-eliminated.
  virtual void GetId(Data data, ValueId* output) const;

  // Add this value to the given Marshaller, and return the value's provisional
  // index (as returned by the Marshaller method used to encode it).  The
  // default implementation returns -1, which indicates that the value cannot
  // be marshalled -- in this case the caller should probably arrange for the
  // value to be retained locally and invoked via RPC.
  //
  // |known_id| should be provided when the caller happens to already know the
  // value's ID.
  virtual int Marshal(Data data, distribute::Marshaller* marshaller,
                      const ValueId* known_id = NULL) const;

  // -----------------------------------------------------------------
  // Debugging

  // Prints a human-readable representation of the value to the given printer,
  // for debugging.  |depth| is a hint indicating how deeply to recurse into
  // values contained within this one.  By convention, the text should look
  // like an Evlan expression, e.g. "foo(bar(1), baz)".  If a sub-value is
  // hidden because |depth| is too small, then it should be replaced with "...".
  // |printer| must use '$' as its variable delimiter.
  virtual void DebugPrint(Data data, int depth,
                          google::protobuf::io::Printer* printer) const = 0;

  // DebugPrint to stderr.  Useful in GDB.
  void DebugPrintToStderr(Data data, int depth) const;

  // DebugPrint to a string and return it.
  string DebugString(Data data, int depth) const;

  // Call this to verify that a pointer is a valid pointer to a Logic object.
  // If it isn't, the method will crash.
  inline void CheckValidPointer() const {
    // Typically if this crashes it will be a segfault while trying to access
    // the vtable.  We wrap it in a DCHECK anyway just to be sure, and also to
    // make the whole thing disappear in opt mode.
    GOOGLE_DCHECK_EQ(0xf00ba12, DummyVirtualFunction());
  }

 private:
  // Users should not attempt to override this.  It's just here to allow
  // CheckValidPointer() to force use of the vtable.
  virtual int DummyVirtualFunction() const { return 0xf00ba12; }
};

// Convenience class which combines a Data and a Logic.  Thus, this represents
// an arbitrary Evlan value.  And in Evlan, all values are functions.
class Value {
 public:
  inline Value();  // Sets logic and data to NULL.
  inline Value(const Logic* logic, Data data);

  inline const Logic* GetLogic() const;
  inline const Data& GetData() const;

  // Shortcut for GetLogic() == NULL.
  inline bool IsNull() const;

  // Convenience methods that just call various methods of |this->logic| and
  // automatically pass |this->data| to them.
  void GetId(ValueId* id) const;
  void DebugPrint(int depth, google::protobuf::io::Printer* printer) const;
  void DebugPrintToStderr(int depth) const;
  string DebugString(int depth = 0x7FFFFFFF) const;

  // Allows MemoryVisitor::VisitPointer() to work on pointers to Values.
  // Can also be called directly by Accept() methods for objects which directly
  // embed values.
  inline int Accept(memory::MemoryVisitor* visitor) const;

 private:
  const Logic* logic_;
  Data data_;
};

// An object which protects a Value from garbage collection even if there are
// no tracked pointers pointing at it.
class KeepAliveValue : public memory::TrackedObject {
 public:
  // Constructs a KeepAliveValue owned by the given MemoryManager containing
  // the given Value.  AddExternalReference() is immediately called on the
  // ObjectTracker, so the object will be assumed to be reachable until
  // Release() is called.
  KeepAliveValue(memory::MemoryManager* memory_manager, Value value);

  // Get the underlying value.  Makes sure the value is owned by the given
  // MemoryManager first (which in necessary to ensure that no other thread
  // is rewriting the value's pointers while you try to use it).  You may pass
  // NULL for memory_manager if you are certain that the MemoryManager you are
  // using already owns the value.
  Value Get(memory::MemoryManager* memory_manager) const;

  // Stop forcing this object to be kept alive.  Although this does not
  // necessarily immediately cause the object to be deleted, you should
  // probably treat it as if it may have, unless you know that the object
  // is owned by the current thread's MemoryManager.
  void Release() const { tracker_->RemoveExternalReference(); }

  // Add a reference, so that Release() must be called an extra time before
  // the object is actually released.
  void AddReference() const { tracker_->AddExternalReference(); }

  // implements TrackedObject ----------------------------------------
  virtual void NotReachable();
  virtual void Accept(memory::MemoryVisitor* visitor) const;

 private:
  virtual ~KeepAliveValue();

  Value value_;
  memory::ObjectTracker* const tracker_;
};

// Contains pointers to things that are necessary for normal execution.
class Context : public memory::MemoryRoot {
 public:
  Context(ContextFactory* factory);
  ~Context();

  // Get pointers to important VM components.
  inline ContextFactory* GetContextFactory() const;
  inline memory::MemoryManager* GetMemoryManager() const;
  inline builtin::AtomRegistry* GetAtomRegistry() const;
  inline builtin::SupportCodeManager* GetSupportCodeManager() const;
  inline EvlanEvaluator* GetEvlanEvaluator() const;

  // Sets the next function call to be made.
  inline void SetNext(Value function, Value parameter);
  inline void SetNext(Value function, Value parameter, Value callback);

  // Shortcut that calls SetNext(GetCallback(), result).
  inline void Return(const Value& result);

  // Invokes function_.GetLogic()->Run() with the parameter and callback
  // specified by the last call to SetNext().  If this leads to SetNext() being
  // called again, Step() returns true without actually executing the next
  // function.  Otherwise, Step() return false.  In other words, if Step()
  // returns true, then there is more work to be done, and Step() should be
  // called again.
  bool Step();

  // Repeatedly invokes Step() until it returns false.  In other words, this
  // executes the function set with SetNext() and continues running as long as
  // there is work to be done, stopping either when execution is complete or
  // blocked.
  void Run();

  // -----------------------------------------------------------------
  // This struct is only relevant to implementers of ContextFactory
  // (and this class, which uses it internally).

  struct Environment {
    memory::MemoryManager* memory_manager;
    builtin::AtomRegistry* atom_registry;
    builtin::SupportCodeManager* support_code_manager;
    EvlanEvaluator* evaluator;

    inline Environment()
        : memory_manager(NULL),
          atom_registry(NULL),
          support_code_manager(NULL),
          evaluator(NULL) {}
  };

  // -----------------------------------------------------------------
  // The methods below may only be used while Run() is in progress.

  // Implementations of Logic->Run() should call these.
  inline const Data& GetData() const;
  inline const Value& GetParameter() const;
  inline const Value& GetCallback() const;

  // Like the methods of MemoryManager, using the Context as the memory root.
  // Note that this means it is only safe to call Allocate() once per call
  // to Logic::Run(), since if you called it again after that, the first thing
  // you had allocated would not be reachable from the root (the Context).  So,
  // if you want to call Allocate() multiple times, you will need to set up
  // your own root object.
  template <typename Type>
  inline Type* Allocate();
  template <typename Type>
  inline Type* AllocateArray(int array_size);
  template <typename Type, typename Element>
  inline Type* AllocateObjectWithArray(int array_size);

  // implements MemoryRoot -------------------------------------------

  // Visits the Context, including function_, parameter_, and callback_.  This
  // allows the Context to be used as the root pointer when allocating memory.
  void Accept(memory::MemoryVisitor* visitor) const;

 private:
  ContextFactory* const factory_;
  Environment const environment_;

  bool set_next_called_;
  bool has_callback_;
  Value function_;
  Value parameter_;
  Value callback_;
};

// Abstract interface for an object which sets up Contexts.  Pass a pointer
// to a ContextFactory to Context's constructor.  This rather odd usage pattern
// (as opposed to NewContext() returning a Context pointer) allows Context
// objects to be allocated on the stack, which improves cache locality.
//
// One of the main purposes of this interface is to allow non-blocking
// behavior.  If an implementation of Logic::Run() wishes to wait for some
// background task to complete, it should not simply block the calling thread.
// However, as soon as Logic::Run() returns, the Context passed to it could
// be destroyed (and indeed often will be, if Logic::Run() doesn't call
// Context::SetNext()).  So what happens when that background task completes?
// The solution is for the Logic::Run() implementation to save a pointer to the
// Context's ContextFactory.  When the background task completes, the callback
// should create a new Context using the factory and then continue running
// on that.  Don't forget to call MemoryManager::ReviveTrackedObject() in order
// to pull any saved values into the new Context's MemoryManager.
class ContextFactory {
 public:
  virtual ~ContextFactory();

  // Indicates that the caller intends to use this ContextFactory in the
  // future, so it should not be deleted in the meantime.
  //
  // AddReference() increments a reference count.  NewContext() also increments
  // the same reference count, while RemoveReference() and ReleaseContext()
  // decrement it, possibly causing the ContextFactory to delete itself if the
  // refcount reaches zero.
  virtual void AddReference() = 0;

  // Indicates that the caller, who previously called AddReference(), is now
  // done with the ContextFactory.  The ContextFactory may have deleted itself
  // by the time this returns.
  virtual void RemoveReference() = 0;

  // -----------------------------------------------------------------
  // These methods are called automatically by Context's constructor and
  // destructor.

  // Creates a new Context::Environment containing the pointers needed by a
  // new Context.  Context's constructor calls this automatically and
  // initializes itself with the result.
  virtual Context::Environment NewContext() = 0;

  // Release a Context previously created with NewContext().  The pointers in
  // |environment| must excatly match those returned by a previous call to
  // NewContext().  This is automatically called by the Context destructor.
  //
  // The ContextFactory may have deleted itself by the time this returns if
  // no other references remain.
  virtual void ReleaseContext(const Context::Environment& environment) = 0;
};

// ===================================================================
// implementation details

inline Value::Value(): logic_(NULL) {}
inline Value::Value(const Logic* logic, Data data)
  : logic_(logic), data_(data) {}

inline const Logic* Value::GetLogic() const { return logic_; }
inline const Data& Value::GetData() const   { return data_;  }
inline bool Value::IsNull() const           { return logic_ == NULL; }

inline void Value::GetId(ValueId* id) const {
  logic_->GetId(data_, id);
}

inline int Value::Accept(memory::MemoryVisitor* visitor) const {
  logic_->Accept(&data_, visitor);
  return sizeof(Value);
}

inline ContextFactory* Context::GetContextFactory() const {
  return factory_;
}

inline memory::MemoryManager* Context::GetMemoryManager() const {
  return environment_.memory_manager;
}

inline builtin::AtomRegistry* Context::GetAtomRegistry() const {
  return environment_.atom_registry;
}

inline builtin::SupportCodeManager* Context::GetSupportCodeManager() const {
  return environment_.support_code_manager;
}

inline EvlanEvaluator* Context::GetEvlanEvaluator() const {
  return environment_.evaluator;
}

inline void Context::SetNext(Value function,
                             Value parameter) {
  GOOGLE_DCHECK(!set_next_called_);
  function.GetLogic()->CheckValidPointer();
  parameter.GetLogic()->CheckValidPointer();
  set_next_called_ = true;
  has_callback_ = false;
  function_ = function;
  parameter_ = parameter;
  callback_ = Value();
}

inline void Context::SetNext(Value function, Value parameter, Value callback) {
  GOOGLE_DCHECK(!set_next_called_);
  function.GetLogic()->CheckValidPointer();
  parameter.GetLogic()->CheckValidPointer();
  callback.GetLogic()->CheckValidPointer();
  set_next_called_ = true;
  has_callback_ = true;
  function_ = function;
  parameter_ = parameter;
  callback_ = callback;
}

inline void Context::Return(const Value& result) {
  SetNext(callback_, result);
}

inline const Data& Context::GetData() const {
  GOOGLE_DCHECK(!set_next_called_);
  return function_.GetData();
}

inline const Value& Context::GetParameter() const {
  GOOGLE_DCHECK(!set_next_called_);
  return parameter_;
}

inline const Value& Context::GetCallback() const {
  GOOGLE_DCHECK(!set_next_called_);
  GOOGLE_DCHECK(has_callback_);
  return callback_;
}

template <typename Type>
inline Type* Context::Allocate() {
  return environment_.memory_manager->Allocate<Type>(this);
}

template <typename Type>
inline Type* Context::AllocateArray(int array_size) {
  return environment_.memory_manager->AllocateArray<Type>(array_size, this);
}

template <typename Type, typename Element>
inline Type* Context::AllocateObjectWithArray(int array_size) {
  return environment_.memory_manager->AllocateObjectWithArray<Type, Element>(
    array_size, this);
}

}  // namespace vm
}  // namespace evlan

namespace std {
template<> struct hash<evlan::vm::ValueId> {
  size_t operator()(const evlan::vm::ValueId& id) const {
    hash<evlan::StringPiece> hasher;
    return hasher(id.GetBytes());
  }
};
}  // namespace std

#endif  // EVLAN_VM_RUNTIME_BASE_H__
