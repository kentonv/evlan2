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
// This file defines abstract interfaces used to interact with a garbage
// collector.  The MemoryManager interface is the basic interface implemented
// by all garbage collection implementations.  Several peripheral interfaces
// are needed as well, some implemented by the GC and some implemented by
// users.
//
// The MemoryManager interface is designed with the following features in mind:
// * Generational garbage collection should be easy to implement.  To that end,
//   older memory is never allowed to contain pointers to newer memory,
//   *except* by using explicit backlinks (see the Backlink class).  In
//   practice, since Evlan is a purely-functional language, this constraint
//   is easy to satisfy.
// * The MemoryManager should have freedom to move objects around in memory,
//   in order to avoid fragmentation and allowing most allocations to be
//   reduced to a simple pointer increment.  To make this easy, most memory
//   is not allowed to be modified at all except immediately after allocation.
//   This way, the GC can copy the memory to a new location and gradually
//   update pointers to the memory over time rather than having to copy and
//   update all at once.  (In other words, the value may exist in two locations
//   in memory at once.)
// * The interface should allow a stackless VM to be efficient.  Where other
//   languages allocate stack frames on the runtime stack, the current plan
//   is for Evlan to allocate them on the heap.  If heap allocation is just
//   a pointer increment, then this should be just as fast as stack allocation.
//   However, this gets trickier due to most memory being read-only, since
//   most functions want to write to their stack frames throughout the function,
//   not just at the end.  To that end, a function can "withdraw" its stack
//   frame when it returns, possibly making the callee's stack frame writable
//   again.  The callee can then check for this and take advantage of it.  More
//   experiments will need to be done to determine whether or not this is the
//   best solution.

#ifndef EVLAN_VM_MEMORY_MEMORY_MANAGER_H__
#define EVLAN_VM_MEMORY_MEMORY_MANAGER_H__

#include "evlan/vm/base.h"

namespace evlan {
namespace vm {
namespace memory {

class MemoryManager;
class MemoryVisitor;

// ===================================================================
// Classes implemented by clients.

// A class which can guide a visitor to all pointers within an object in
// memory.
class MemoryGuide {
 public:
  virtual ~MemoryGuide();

  // Traverses the object or array of objects at *data and calls
  // visitor->Visit*() to visit all pointers.  Returns the size of the memory
  // at *data.  |count| is the array size; when not dealing with arrays, it
  // is always 1.
  virtual int Accept(const void* data, int count,
                     MemoryVisitor* visitor) const = 0;
};

// For types which define a method:
//   int Accept(MemoryVisitor* visitor) const;
// GenericMemoryGuide can be used as the MemoryGuide implementation.  Note that
// we do not want the visited objects themselves to subclass an abstract
// interface because we do not want to force them to have vtable pointers.
template <typename Type>
class GenericMemoryGuide : public MemoryGuide {
 public:
  GenericMemoryGuide() {}
  ~GenericMemoryGuide() {}

  inline static const GenericMemoryGuide* GetSingleton() { return &kSingleton; }

  // implements MemoryGuide ------------------------------------------
  int Accept(const void* data, int count, MemoryVisitor* visitor) const {
    int offset = 0;
    for (int i = 0; i < count; i++) {
      offset +=
        reinterpret_cast<const Type*>(
          reinterpret_cast<const byte*>(data) + offset)->Accept(visitor);
    }
    return offset;
  }

 private:
  static const GenericMemoryGuide kSingleton;
};

template <typename Type>
const GenericMemoryGuide<Type> GenericMemoryGuide<Type>::kSingleton;

// An object which is tracked by the memory manager but whose memory
// is managed externally.
class TrackedObject {
 public:
  virtual ~TrackedObject();

  // Called when the object is no longer reachable.  Once this returns,
  // the ObjectTracker for this TrackedObject is no longer valid.
  virtual void NotReachable() = 0;

  // Called to traverse the object and visit its pointers.  This MUST NOT
  // visit anything which did not exist when the TrackedObject was registered
  // with the MemoryManager.  Use Backlinks to point to new objects.
  virtual void Accept(MemoryVisitor* visitor) const = 0;
};

// An object which represents the root of active memory.  When allocating
// memory, a MemoryRoot must be provided.  Any memory not reachable from
// that root (and not reachable from any TrackedObjects that have external
// references) may be reclaimed.
class MemoryRoot {
 public:
  virtual ~MemoryRoot();

  virtual void Accept(MemoryVisitor* visitor) const = 0;
};

// A memory root which does nothing in its accept method.
class NullMemoryRoot : public MemoryRoot {
 public:
  virtual ~NullMemoryRoot();

  virtual void Accept(MemoryVisitor* visitor) const;
};

// ===================================================================
// Classes implemented by MemoryManager.

class Backlink;
class ObjectTracker;

// An object which can allocate and garbage-collect memory.
class MemoryManager {
 public:
  virtual ~MemoryManager();

  // Allocate some managed memory.  The memory returned is immediately "active",
  // meaning that it may be mutated at will.  The memory may become inactive
  // the next time any method of MemoryManager is called, unless stated
  // otherwise in the method's documentation, below.  Once an allocation is
  // inactive, it MUST NOT be mutated.  Typically, only one allocation is
  // active at a time.  An allocation cannot be active if any other allocations
  // might contain pointers to it.
  //
  // "root" is the root object of memory being used by the calling thread.
  // AllocateBytes() may trigger a garbage-collection pass, in which case all
  // memory not reachable from "root" may be reclaimed, and all other managed
  // memory may be moved before returning.
  //
  // WARNING:  Again, if a garbage-collection pass does occur, all reachable
  // memory may be moved before returning.  This means that any pointers which
  // are not reachable via visitation from the root may no longer be valid on
  // return.
  virtual byte* AllocateBytes(int size, const MemoryRoot* root) = 0;

  // If "bytes" points to active memory, return a non-const pointer to that
  // memory.  Otherwise, return NULL.  This is particularly useful to call
  // immediately after TryWithdrawBytes(), to attempt to activate the
  // allocation previous to the one that was withdrawn.
  virtual byte* TryActivateBytes(const byte* bytes) = 0;

  // Attempts to allocate more memory within the active allocation.  This may
  // return NULL, in which case the only way to allocate more memory is to
  // call AllocateBytes() (and thus create a new allocation).  TryExtendBytes()
  // will never trigger a garbage collection pass, and bytes which were
  // active before TryExtendBytes() was called remain active afterwards.
  virtual byte* TryExtendBytes(int size) = 0;

  // Tells the MemoryManager that *if* the given bytes are part of the
  // active allocation then they are no longer needed.  The MemoryManager
  // may reclaim these bytes the next time AllocateBytes() or TryExtendBytes()
  // is called (but until then the bytes remain readable to the caller).  If
  // all bytes in an allocation are withdrawn, the previous allocation may
  // become active.  Returns true if the bytes were successfully withdrawn.
  //
  // Intelligent use of TryWithdrawBytes() can allow MemoryManager-allocated
  // memory to be used like a call stack.  Each function allocates its "stack
  // frame" using AllocateBytes(), then calls TryWithdrawBytes() before
  // returning.  Assuming every function withdraws all the bytes it allocated,
  // the heap then becomes an efficient stack with no need to do real garbage
  // collection.  If some function does not withdraw the bytes it allocates,
  // that is OK:  the calling functions will not be able to withdraw their own
  // bytes, but they will still function correctly.  Assuming most functions do
  // not allocate long-term memory, this optimization should work most of the
  // time, which should be good enough to allow stack frames to be allocated on
  // the heap, simplifying the rest of the system.
  virtual bool TryWithdrawBytes(const byte* bytes, int size) = 0;

  // Add a tracked object.
  virtual ObjectTracker* AddTrackedObject(TrackedObject* object) = 0;

  // If you have an ObjectTracker pointer which you know is still valid
  // (because TrackedObject::NotReachable() has not yet been called), but it
  // is possible that MemoryManager::Allocate*() was called at a time when
  // the ObjectTracker was, in fact, not reachable, then it may or may not be
  // safe to just start using the ObjectTracker again.  If you find yourself
  // in such a situation, call ReviveTrackedObject().  This checks if the
  // tracker is still safe to use and, if so, returns true.  If
  // ReviveTrackedObject() returns false, this means that the object has
  // already been found to be unreachable and is in the process of being
  // deleted; in this case it cannot be used.
  //
  // If ReviveTrackedObject() returns true, then the ObjectTracker is
  // guaranteed to last at least as long as a newly-allocated ObjectTracker
  // (allocated using AddTrackedObject()) would last -- i.e. at least until
  // Allocate*() is called and longer if it remains reachable from the root
  // pointer.
  //
  // If multiple threads are in use, the caller must think about
  // synchronization between ReviveTrackedObject() and NotReachable().  In
  // particular, if it is possible that NotReachable() could be called
  // simultaneously with ReviveTrackedObject(), then the caller should hold a
  // lock that prevents NotReachable() from returning until
  // ReviveTrackedObject() returns.  Otherwise, if NotReachable() returns first,
  // then the ObjectTracker will be deleted, possibly causing the call to
  // ReviveTrackedObject() to crash.
  //
  // Note that if you wish to access pointers in a TrackedObject that was
  // originally created by some other MemoryManager object (e.g. in another
  // thread), you *must* call ReviveTrackedObject() on it even if it has
  // external references (and thus could not possibly become unreachable).
  // The reason for this is that it is assumed that a TrackedObject is only
  // known about by the MemoryManager that created it until
  // ReviveTrackedObject() is called, so if you access it from another thread,
  // the original MemoryManager could potentially be in the middle of updating
  // the TrackedObject's pointers.  By calling ReviveTrackedObject, you let
  // the system know that the object has found its way to a different thread
  // and thus any update of its pointers must be done in a thread-safe way.
  // ReviveTrackedObject() will always return true if the tracker has an
  // external references (because it could not possibly become unreachable).
  //
  // TODO(kenton):  Maybe rename this to AdoptTrackedObject(), or something
  //   else that makes more sense given the last paragraph above?
  virtual bool ReviveTrackedObject(ObjectTracker* object_tracker) = 0;

  // Create a backlink.  Initially set to NULL.
  virtual Backlink* MakeBacklink() = 0;

  // -----------------------------------------------------------------
  // Converinece type-safe versions of the above.

  // Convenience method to allocate space for a value of the given type.
  // The type's constructor is *not* called -- you should probably use only
  // POD or POD-like types with this method.
  //
  // Example usage:
  //   MyRoot root;
  //   MyObject* obj = memory_manager->Allocate<MyObject>(&root);
  template <typename Type>
  inline Type* Allocate(const MemoryRoot* root) {
    void* bytes = AllocateBytes(sizeof(Type), root);
    return reinterpret_cast<Type*>(bytes);
  }

  // Like Allocate(), but allocates an array of values.
  template <typename Type>
  inline Type* AllocateArray(int size, const MemoryRoot* root) {
    void* bytes = AllocateBytes(sizeof(Type) * size, root);
    return reinterpret_cast<Type*>(bytes);
  }

  // Like Allocate(), but allocates an object which is immediately followed
  // by an array.  You can declare such an object like so (GCC feature):
  //   struct Foo {
  //     int size;
  //     Value values[];
  //   };
  template <typename Type, typename Element>
  inline Type* AllocateObjectWithArray(int size, const MemoryRoot* root) {
    void* bytes = AllocateBytes(sizeof(Type) + sizeof(Element) * size, root);
    return reinterpret_cast<Type*>(bytes);
  }

  // Convenience method which automatically casts the result of
  // TryActivateBytes() to the input type, since the returned value is always
  // either NULL or equal to the input.
  template <typename Type>
  inline Type* TryActivate(const Type* ptr) {
    return reinterpret_cast<Type*>(
      TryActivateBytes(reinterpret_cast<const byte*>(ptr)));
  }

  // Convenience method to extend an allocation with a value of the given type.
  template <typename Type>
  inline Type* TryExtend() {
    byte* bytes = TryExtendBytes(sizeof(Type));
    if (bytes == NULL) {
      return NULL;
    } else {
      return new(bytes) Type;
    }
  }

  // Convenience method to extend an allocation with an array of values of the
  // given type.
  template <typename Type>
  inline Type* TryExtendArray(int size) {
    byte* bytes = TryExtendBytes(sizeof(Type) * size);
    if (bytes == NULL) {
      return NULL;
    } else {
      Type* result = reinterpret_cast<Type*>(bytes);
      for (int i = 0; i < size; i++) {
        new(result + i) Type;
      }
      return result;
    }
  }

  // Convenience method which calls TryWithdrawBytes() to withdraw an object
  // of a particular type.
  template <typename Type>
  inline bool TryWithdraw(const Type* ptr) {
    return TryWithdrawBytes(reinterpret_cast<const byte*>(ptr), sizeof(*ptr));
  }

  // Convenience method which calls TryWithdrawBytes() to withdraw an array
  // of a particular type.
  template <typename Type>
  inline bool TryWithdrawArray(const Type ptr[], int size) {
    return TryWithdrawBytes(reinterpret_cast<const byte*>(ptr),
                            size * sizeof(ptr[0]));
  }

  // Convenience method which calls TryWithdrawBytes() to withdraw an array
  // of a particular type.  The second parameter is actually ignored, but
  // lets you to avoid explicitly instantiating this template.
  template <typename Type, typename Element>
  inline bool TryWithdrawObjectWithArray(
      const Type* ptr, const Element array[], int size) {
    return TryWithdrawBytes(reinterpret_cast<const byte*>(ptr),
                            sizeof(*ptr) + sizeof(array[0]) * size);
  }
};

// A reference to a TrackedObject.  Created by
// MemoryManager::AddTrackedObject().  Once the ObjectTracker is no longer
// reachable, it will be destroyed, and TrackedObject::NotReachable() will
// be called on the TrackedObject.
class ObjectTracker {
 public:
  virtual ~ObjectTracker();

  virtual TrackedObject* GetTrackedObject() = 0;

  // Call AddExternalReference() to indicate that some memory not tracked
  // by the MemoryManager has a reference to this object and thus the object
  // needs to stay alive even if it is not reachable from tracked memory.
  // Call RemoveExternalReference() once the reference no longer exists.  These
  // maintain a reference count, so multiple external references may exist at
  // the same time.
  //
  // Note that AddExternalReference() may only be called in a thread owning
  // a MemoryManager which knows about this object (either because it created
  // this ObjectTracker or because ReviveTrackedObject() was called).  However,
  // RemoveExternalReference() may be called from any thread.
  virtual void AddExternalReference() = 0;
  virtual void RemoveExternalReference() = 0;
};

// A pointer to managed memory which is allowed to change after it is created.
// Created by MemoryManager::MakeBacklink().  This is the ONLY way that old
// objects may refer to new ones.
class Backlink {
 public:
  virtual ~Backlink();

  virtual const byte* GetBytes() = 0;
  virtual void SetBytes(const byte* target, const MemoryGuide* guide) = 0;

  // -----------------------------------------------------------------
  // Converinece type-safe versions of the above.

  template <typename Type>
  inline const Type* Get() {
    return reinterpret_cast<const Type*>(GetBytes());
  }

  template <typename Type>
  inline void Set(const Type* target) {
    SetBytes(reinterpret_cast<const byte*>(target),
             GenericMemoryGuide<Type>::GetSingleton());
  }
};

// The MemoryManager uses a MemoryVisitor to determine what memory is reachable
// and to find pointers that will need to be updated if memory is moved.
//
// See also:  MemoryRoot, MemoryGuide
class MemoryVisitor {
 public:
  virtual ~MemoryVisitor();

  // Visit a pointer which points to managed memory.
  //   pointer:  A pointer to a pointer to the memory to be visited.
  //             The MemoryManager may need to update this pointer if the
  //             memory to which it points moves.  Hence, VisitPointer must
  //             be called for every pointer to the target memory, even
  //             though they are pointing to the same thing.  This pointer
  //             is declared "const" even though passing it to this method
  //             may cause it to be mutated.  This is because it is often
  //             the case that you need to visit pointers that are part of
  //             const structures and because the modification made by
  //             the MemoryVisitor does not change the logical meaning.
  //   guide:    A MemoryGuide which can be called to visit the memory
  //             pointed to.
  // Most callers will want to call the VisitPointer template method, below,
  // rather than this version.
  virtual void VisitBytesPointer(const byte* const* pointer,
                                 const MemoryGuide* guide) = 0;

  // Like VisitBytesPointer(), but the pointer actually points to several
  // objects in sequence.  Each object uses the same MemoryGuide and begins
  // in memory immediately after the previous object ends.
  virtual void VisitBytesSequencePointer(const byte* const* pointer,
                                         const MemoryGuide* guide,
                                         int count) = 0;

  // Visit a pointer that points to a region of raw bytes of the given size.
  // The region cannot contain any pointers.
  virtual void VisitRawBytesPointer(const byte* const* pointer, int size) = 0;

  // Visit a TrackedObject.  Since ObjectTrackers are not movable, there is
  // no need to update the pointer pointing to the ObjectTracker.
  virtual void VisitTrackedObject(ObjectTracker* tracker) = 0;

  // Visit a Backlink.  Backlinks are also non-movable, so there is no need
  // to update the pointer itself.
  virtual void VisitBacklink(const Backlink* backlink) = 0;

  // -----------------------------------------------------------------

  // Convenience type-safe version of VisitPointer.
  template <typename Type>
  inline void VisitPointer(const Type* const* pointer) {
    VisitBytesPointer(reinterpret_cast<const byte* const*>(pointer),
                      GenericMemoryGuide<Type>::GetSingleton());
  }

  // Convenience type-safe version of VisitArrayPointer.
  template <typename Type>
  inline void VisitArrayPointer(const Type* const* pointer, int size) {
    VisitBytesSequencePointer(reinterpret_cast<const byte* const*>(pointer),
                              GenericMemoryGuide<Type>::GetSingleton(),
                              size);
  }

  // Convenience type-safe version of VisitRawBytesPointer.
  template <typename Type>
  inline void VisitRawArrayPointer(const Type* const* pointer, int size) {
    VisitRawBytesPointer(reinterpret_cast<const byte* const*>(pointer),
                         size * sizeof(Type));
  }
};

// Macro which, in debug mode, checks that the given expression evaluates
// to true sometimes, printing warnings if not.  This is especially useful with
// TryWithdraw().  Example:
//    if (EVLAN_EXPECT_SOMETIMES(memory_manager->TryWithdraw(foo))) {
//      // Withdraw successful
//    }
// If the withdraw never succeeds, warnings will be printed like:
//    foo.cc:123: memory_manager->TryWithdraw(foo) failed 100 times in a row.
#ifdef NDEBUG
#define EVLAN_EXPECT_SOMETIMES(EXPRESSION)   \
    (EXPRESSION)
#else
// TODO(kenton):  Is there any way to declare a static variable within an
//   expression?  Ideally we'd declare a static variable here to store the
//   counter instead of using a big unordered_map, but we want the macro to
//   evaluate to the expression result...
#define EVLAN_EXPECT_SOMETIMES(EXPRESSION)                              \
    (::evlan::vm::memory::ExpectSometimes(                              \
        __FILE__, __LINE__, EXPRESSION, #EXPRESSION))
#endif

bool ExpectSometimes(const char* file, int line, bool success,
                     const char* expression);

}  // namespace memory
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_MEMORY_MEMORY_MANAGER_H__
