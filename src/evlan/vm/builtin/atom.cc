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
// The implementation here is tricky.  On one hand, we want all atoms of a
// particular name to share one object for easy comparison.  Therefore, we
// need to keep a table containing pointers to them all.  However, we also
// want atoms to be garbage collected, so that creating an atom with a weird
// name once does not permanently bloat the VM.  But if all the atoms are
// in a central table, and everyone has a reference to that table, then they
// are all logically reachable.  We can hide the table from the memory manager,
// but then we have to be careful about the possibility that an atom in the
// table could be destroyed at the same time as we are trying to look it up
// in another thread.  The MemoryManager interface's method
// ReviveTrackedObject() allows us to do exactly that, so this is the approach
// we take.  Essentially, we maintain a table full of weak references.

#include "evlan/vm/builtin/atom.h"

#include <algorithm>

#include "evlan/vm/builtin/array.h"
#include "evlan/vm/builtin/native.h"
#include "evlan/vm/builtin/support.h"
#include "evlan/vm/hasher.h"
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/common.h"
#include "evlan/map-util.h"
#include <google/protobuf/io/printer.h>
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace builtin {

// AtomRegistry contains a pointer to a State.  Individual Atom objects alse
// point at it.  Since we can't guarantee that all Atoms will be destroyed
// before the AtomRegistry (because they are garbage collected), we must use
// refcounting to make sure that the State is not destroyed until there are
// no more users.  (Of course, in practice, the AtomRegistry will stay alive
// as long as the VM itself, which will usually be until the process
// terminates, but we like to have clean shutdown code for everything
// nevertheless.)
struct AtomRegistry::State {
  typedef unordered_map<StringPiece, Atom*> AtomMap;

  State* parent_;

  Mutex mutex_;  // protects atom_map_ and refcount_.

  // Maps atom names to objects.
  AtomMap atom_map_;

  int refcount_;

  // How many children does this registry have?
  int child_count_;
};

// ===================================================================

// An individual atom.
class AtomRegistry::Atom : public memory::TrackedObject {
 public:
  Atom(const StringPiece& name,
       State* registry_state,
       memory::MemoryManager* memory_manager);
  ~Atom();

  string name_;
  State* registry_state_;

  // The MemoryManager's handle to this object.
  memory::ObjectTracker* tracker_;

  // implements TrackedObject ----------------------------------------
  void NotReachable();
  void Accept(memory::MemoryVisitor* visitor) const;
};

AtomRegistry::Atom::Atom(const StringPiece& name,
                         State* registry_state,
                         memory::MemoryManager* memory_manager)
  : name_(name.as_string()),
    registry_state_(registry_state) {
  tracker_ = memory_manager->AddTrackedObject(this);
}

AtomRegistry::Atom::~Atom() {}

void AtomRegistry::Atom::NotReachable() {
  bool should_delete_state;

  {
    MutexLock lock(&registry_state_->mutex_);

    if (registry_state_->child_count_ != 0) {
      // TODO(kenton):  This error shouldn't happen because an EvlanEvaluator
      //   should be frozen once it has been forked.  But in theory we could
      //   also be calling AddExternalReference() on all atoms in a registry
      //   when it has children, which would prevent this from happening.
      GOOGLE_LOG(DFATAL)
        << "NotReachable() was called on an atom in an AtomRegistry which "
           "has children.";
    }

    // Remove this Atom from the AtomMap, being careful to check that someone
    // else hasn't already done so for us.  We check that iter->second == this
    // to defend against the case where this atom has already been removed and
    // replaced by a new atom of the same name.
    State::AtomMap::iterator iter = registry_state_->atom_map_.find(name_);
    if (iter != registry_state_->atom_map_.end() && iter->second == this) {
      registry_state_->atom_map_.erase(iter);
    }

    // Decrement refcount.  We must unlock the mutex before actually deleting,
    // since the mutex is part of the state object.
    --registry_state_->refcount_;
    should_delete_state = registry_state_->refcount_ == 0;
  }

  if (should_delete_state) delete registry_state_;

  delete this;
}

void AtomRegistry::Atom::Accept(memory::MemoryVisitor* visitor) const {
  // Nothing to do here, since we don't keep pointers to any other tracked
  // memory.
}

// ===================================================================

// Logic object associated with an atom Value.
class AtomRegistry::AtomLogic : public Logic {
 public:
  AtomLogic();
  ~AtomLogic();

  static const AtomLogic kSingleton;

  // implements Logic ------------------------------------------------
  void Run(Context* context) const;
  void Accept(const Data* data, memory::MemoryVisitor* visitor) const;
  void GetId(Data data, ValueId* output) const;
  int Marshal(Data data, distribute::Marshaller* marshaller,
              const ValueId* known_id = NULL) const;
  void DebugPrint(Data data, int depth, google::protobuf::io::Printer* printer) const;
};

AtomRegistry::AtomLogic::AtomLogic() {}
AtomRegistry::AtomLogic::~AtomLogic() {}

const AtomRegistry::AtomLogic AtomRegistry::AtomLogic::kSingleton;

void AtomRegistry::AtomLogic::Run(Context* context) const {
  // Delegate to the support code for atoms.
  context->GetSupportCodeManager()
         ->GetSupportCode(SupportCodeManager::ATOM)
         ->Run(context);
}

void AtomRegistry::AtomLogic::Accept(const Data* data,
                                     memory::MemoryVisitor* visitor) const {
  visitor->VisitTrackedObject(data->As<Atom>()->tracker_);
}

void AtomRegistry::AtomLogic::GetId(Data data, ValueId* output) const {
  output->SetBytesWithPrefix('@', data.As<Atom>()->name_);
}

int AtomRegistry::AtomLogic::Marshal(
    Data data, distribute::Marshaller* marshaller,
    const ValueId* known_id) const {
  return marshaller->AddAtom(data.As<Atom>()->name_);
}

void AtomRegistry::AtomLogic::DebugPrint(Data data, int depth,
                                         google::protobuf::io::Printer* printer) const {
  printer->Print("@$name$", "name", data.As<Atom>()->name_);
}

// ===================================================================

AtomRegistry::AtomRegistry()
  : state_(new State) {
  state_->parent_ = NULL;
  state_->refcount_ = 1;
  state_->child_count_ = 0;
}

AtomRegistry::AtomRegistry(AtomRegistry* parent)
  : state_(new State) {
  state_->parent_ = parent->state_;
  state_->refcount_ = 1;
  state_->child_count_ = 0;

  MutexLock lock(&state_->parent_->mutex_);
  ++state_->parent_->child_count_;
}

AtomRegistry::~AtomRegistry() {
  bool should_delete_state;
  State* parent = state_->parent_;

  {
    MutexLock lock(&state_->mutex_);
    GOOGLE_DCHECK_EQ(state_->child_count_, 0)
      << "Atom registry was destroyed before its children.";
    should_delete_state = --state_->refcount_ == 0;
  }

  if (should_delete_state) delete state_;

  if (parent != NULL) {
    MutexLock lock(&parent->mutex_);
    --parent->child_count_;
  }
}

Value AtomRegistry::GetAtom(memory::MemoryManager* memory_manager,
                            const StringPiece& name) {
  return Value(GetAtomLogic(), GetAtomData(memory_manager, name));
}

Data AtomRegistry::GetAtomData(memory::MemoryManager* memory_manager,
                               const StringPiece& name) {
  // Look for a matching atom in the table.
  {
    MutexLock lock(&state_->mutex_);

    if (state_->child_count_ != 0) {
      GOOGLE_LOG(DFATAL) << "Cannot call AtomRegistry::GetAtomData() on a registry "
                     "that has children.";
    }

    // Search this registry's map and each parent's map until we find the atom.
    // Note that we don't have to lock mutexes in parent states because they
    // are required to be unchanging while the child exists.
    Atom* result = NULL;
    State* state = state_;
    do {
      result = FindPtrOrNull(state->atom_map_, name);
      state = state->parent_;
    } while (result == NULL && state != NULL);

    if (result != NULL) {
      // Found it, but it's possible that the atom is being destroyed in
      // another thread as we speak.  We must use ReviveTrackedObject() to
      // check for this.  That is, unless we found the atom in a parent registry
      // (state != state_), in which case we know that parent registry is not
      // allowed to change so we don't have to worry about the atom going away.
      if (state != state_ ||
          memory_manager->ReviveTrackedObject(result->tracker_)) {
        // It's still alive.
        return Data(result);
      } else {
        // The Atom is about to be deleted.  Let's go ahead and remove it
        // from the table now, so that we can add a new one in its place.
        state_->atom_map_.erase(name);
      }
    }
  }

  // Need to create a new atom.  We cannot do this with the mutex locked
  // because it needs to call memory_manager->AddTrackedObject(), and
  // we do not know how expensive that might be or what other locks it might
  // grab.
  Atom* result = new Atom(name, state_, memory_manager);

  {
    MutexLock lock(&state_->mutex_);

    // Add a refcount for the new Atom.
    ++state_->refcount_;

    Atom** map_slot = &state_->atom_map_[result->name_];
    if (*map_slot == NULL) {
      // No other thread created an Atom.
      *map_slot = result;
    } else {
      // This atom was constructed in another thread simultaneously.  Can we
      // use that one?
      if (memory_manager->ReviveTrackedObject((*map_slot)->tracker_)) {
        // Yep.
        result = *map_slot;
      } else {
        // The Atom created by the other thread is already gone.  Remove it
        // and use the new one instead.
        *map_slot = result;
      }
    }
  }

  return Data(result);
}

// ===================================================================

StringPiece GetAtomName(Data data) {
  return data.As<AtomRegistry::Atom>()->name_;
}

const Logic* GetAtomLogic() {
  return &AtomRegistry::AtomLogic::kSingleton;
}

// ===================================================================
// Atom Tables
//
// The code in this section implements an "atom table", which maps a set of
// atoms to integers.  For example, the atom table constructed from the array
// {@foo, @bar, @baz} would be a function which takes @foo, @bar, or @baz as
// inputs and returns 0, 1, or 2, respectively.  Or, in code:
//
//   table = import "builtin".atom.table({@foo, @bar, @baz})
//   fooIndex = table(@foo)   # == 0
//   barIndex = table(@bar)   # == 1
//   bazIndex = table(@baz)   # == 2
//
// We implement this using a hashtable.

namespace {

struct AtomTableData {
  // Size of the "buckets" array -- this will be larger than the number of
  // atoms actually in the table.
  int size;

  // The original array of atoms used to construct this.  We keep this around
  // just to make marshalling easier.
  Value original_array;

  struct Bucket {
    Data atom;    // An atom Data, or NULL if the bucket is empty.
    int value;    // If |atom| is not NULL, this is the index of that atom in
                  // the table.
  };

  Bucket buckets[0];

  int Accept(memory::MemoryVisitor* visitor) const;
};

int AtomTableData::Accept(memory::MemoryVisitor* visitor) const {
  const Logic* atom_logic = GetAtomLogic();

  for (int i = 0; i < size; i++) {
    if (buckets[i].atom.As<void>() != NULL) {
      atom_logic->Accept(&buckets[i].atom, visitor);
    }
  }

  original_array.Accept(visitor);

  return sizeof(*this) + size * sizeof(buckets[0]);
}

// List of primes where each element is roughly double the previous.  Obtained
// from:
//   http://planetmath.org/encyclopedia/GoodHashTablePrimes.html
// Primes < 53 were added because most atom tables will be very small and
// allocating 53 buckets for them would be very wasteful.
static const int kPrimes[] = {
  3, 5, 11, 23, 53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157,
  98317, 196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843,
  50331653, 100663319, 201326611, 402653189, 805306457, 1610612741
};

// Decide how large of a table we want to use to store the given number of
// elements.
int ComputeTableSize(int element_count) {
  // Double the size to reduce hash collisions.
  int size = element_count * 2;

  // Find the smallest prime in our table that is larger than this.
  for (int i = 0; i < arraysize(kPrimes); i++) {
    if (kPrimes[i] >= size) return kPrimes[i];
  }

  // Too big, give up.
  return -1;
}

// This class generates a sequence of bucket numbers for a given lookup key.
// The sequence will include all buckets in the table before repeating.
class BucketSequnece {
 public:
  inline BucketSequnece(Data atom, int table_size)
    : table_size_(table_size) {
    uintptr_t hash = reinterpret_cast<uintptr_t>(atom.As<void>());

    // Double hashing:  We compute both the initial bucket and the linear
    //   probing interval based on the original hash.  This way, if multiple
    //   items hit the same initial bucket, their next bucket should be
    //   different.
    // TODO(kenton):  I don't know much about hashing so it's possible this is
    //   somehow dumb.  If profiles show a lot of time spent in atom table
    //   lookups then we should optimize.
    bucket_ = hash % table_size;
    interval_ = hash % (table_size - 1) + 1;
  }

  // Get the current bucket index.
  inline int Current() { return bucket_; }

  // Move to the next bucket index.
  inline void Next() {
    bucket_ = (bucket_ + interval_) % table_size_;
  }

 private:
  int table_size_;
  int bucket_;
  int interval_;
};

// Insert an atom into an atom table and map it to the given value.
void Insert(AtomTableData* table, Data atom, int value) {
  BucketSequnece sequence(atom, table->size);

  while (table->buckets[sequence.Current()].atom.As<void>() != NULL) {
    sequence.Next();
  }

  table->buckets[sequence.Current()].atom = atom;
  table->buckets[sequence.Current()].value = value;
}

// Look up an atom in the atom table, returning its value.
int Lookup(const AtomTableData* table, Data atom) {
  BucketSequnece sequence(atom, table->size);

  while (table->buckets[sequence.Current()].atom.As<void>() != NULL) {
    if (AtomsAreEqual(table->buckets[sequence.Current()].atom, atom)) {
      return table->buckets[sequence.Current()].value;
    }
    sequence.Next();
  }

  // Not found.
  return -1;
}

// Logic for a completed atom table.  Implements a function which takes an
// atom as the input and returns the index of that atom in the table.
class AtomTableLogic : public Logic {
 public:
  AtomTableLogic() {}
  ~AtomTableLogic() {}

  static inline const AtomTableLogic* GetSingleton() { return &kSingleton; }

  // implements Logic ------------------------------------------------

  void Run(Context* context) const {
    // Type-check input.
    if (!IsAtom(context->GetParameter())) {
      context->Return(TypeErrorUtils::MakeError(context,
        "Parameter to atom table must be an atom."));
      return;
    }

    // Look up the atom.
    int value = Lookup(context->GetData().As<AtomTableData>(),
                       context->GetParameter().GetData());

    if (value == -1) {
      // Not found.
      context->Return(TypeErrorUtils::MakeError(context,
        "Atom not found in table: " + context->GetParameter().DebugString()));
      return;
    }

    context->Return(MakeInteger(value));
  }

  void Accept(const Data* data,
              memory::MemoryVisitor* visitor) const {
    data->AcceptAs<AtomTableData>(visitor);
  }

  void GetId(Data data, ValueId* output) const {
    Hasher hasher;
    hasher.AddString("evlan::vm::builtin::AtomTableLogic");

    ValueId original_array_id;
    data.As<AtomTableData>()->original_array.GetId(&original_array_id);
    hasher.AddBytes(original_array_id.GetBytes());

    output->SetBytesFromHasher('^', &hasher);
  }

  int Marshal(Data data, distribute::Marshaller* marshaller,
              const ValueId* known_id = NULL) const {
    ValueId computed_id;
    if (known_id == NULL) {
      GetId(data, &computed_id);
      known_id = &computed_id;
    }

    distribute::Marshaller::ProvisionalIndex result =
        marshaller->TryAvoidMarshalling(*known_id, Value(this, data),
          distribute::Marshaller::DONT_MARSHAL_UNLESS_REQUIRED);
    if (result != distribute::Marshaller::kInvalidIndex) {
      return result;
    }

    result = marshaller->AddFunctionCall(
        *known_id, kBuildAtomTable, data.As<AtomTableData>()->original_array);
    marshaller->RecordForDupElimination(Value(this, data), result);
    return result;
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    const AtomTableData* table = data.As<AtomTableData>();
    vector<pair<int, StringPiece> > elements;
    for (int i = 0; i < table->size; i++) {
      if (table->buckets[i].atom.As<void>() != NULL) {
        elements.push_back(make_pair(table->buckets[i].value,
                                     GetAtomName(table->buckets[i].atom)));
      }
    }
    std::sort(elements.begin(), elements.end());

    printer->Print("atomTable({");
    for (int i = 0; i < elements.size(); i++) {
      if (i > 0) printer->Print(", ");
      GOOGLE_DCHECK_EQ(elements[i].first, i);
      printer->Print("@$name$", "name", elements[i].second.as_string());
    }
    printer->Print("})");
  }

 private:
  static const AtomTableLogic kSingleton;
  static const Value kBuildAtomTable;
};

const AtomTableLogic AtomTableLogic::kSingleton;

// Logic for the "table" function, which builds an atom table given an array
// of atoms.  This logic has no associated data.
class AtomTableBuilderLogic : public Logic {
 public:
  AtomTableBuilderLogic() {}
  ~AtomTableBuilderLogic() {}

  static inline const AtomTableBuilderLogic* GetSingleton() {
    return &kSingleton;
  }

  // implements Logic ------------------------------------------------

  void Run(Context* context) const {
    const Value& array = context->GetParameter();

    // Check that the input is an array.
    if (!ArrayUtils::IsArray(array)) {
      context->Return(TypeErrorUtils::MakeError(context,
        "Wrong type passed to native function atomTable().  Expected array, "
        "got: " + array.DebugString()));
      return;
    }

    int size = ArrayUtils::GetArraySize(array);

    // Make sure all elements of the input array are atoms.
    for (int i = 0; i < size; i++) {
      const Value& element = ArrayUtils::GetElement(array, i);
      if (!IsAtom(element)) {
        context->Return(TypeErrorUtils::MakeError(context,
          strings::Substitute(
            "Wrong type for element $0 of array passed to atomTable().  "
            "Expected atom, got: $1",
            i, element.DebugString())));
        return;
      }
    }

    // Decide how big our table should be.
    int table_size = ComputeTableSize(size);
    if (table_size == -1) {
      context->Return(TypeErrorUtils::MakeError(context,
        strings::Substitute(
          "Tried to build ridiculously large atom table.  Refused.")));
      return;
    }

    // Allocate and initialize the table.
    AtomTableData* table =
      context->AllocateObjectWithArray<
        AtomTableData, AtomTableData::Bucket>(table_size);
    table->size = table_size;
    table->original_array = array;

    // Initialize to zero.
    for (int i = 0; i < table_size; i++) {
      table->buckets[i].atom = Data();  // NULL
      table->buckets[i].value = -1;
    }

    // Insert all atoms into the table.
    for (int i = 0; i < size; i++) {
      Insert(table, ArrayUtils::GetElement(array, i).GetData(), i);
    }

    // Return the finished table.
    context->Return(Value(AtomTableLogic::GetSingleton(), Data(table)));
  }

  void Accept(const Data* data,
              memory::MemoryVisitor* visitor) const {
    // nothing
  }

  void GetId(Data data, ValueId* output) const {
    output->SetBytesWithPrefix('<', "nativeAtom.table");
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("makeAtomTable");
  }

 private:
  static const AtomTableBuilderLogic kSingleton;
};

const AtomTableBuilderLogic AtomTableBuilderLogic::kSingleton;

const Value AtomTableLogic::kBuildAtomTable(
    AtomTableBuilderLogic::GetSingleton(), Data());

// ===================================================================
// Built-in atom functions.

void Equals(Context* context, Value a, Value b) {
  if (!IsAtom(a)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type for first argument to equal().  Expected atom, "
      "got: " + a.DebugString()));
    return;
  }
  if (!IsAtom(b)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type for second argument to equal().  Expected atom, "
      "got: " + b.DebugString()));
    return;
  }

  context->Return(BooleanUtils::MakeBoolean(
    AtomsAreEqual(a.GetData(), b.GetData())));
}

void Name(Context* context, Value atom) {
  if (!IsAtom(atom)) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Wrong type for first argument to atomName().  Expected atom, "
      "got: " + atom.DebugString()));
    return;
  }

  context->Return(
    ByteUtils::CopyString(context->GetMemoryManager(), context,
                          GetAtomName(atom.GetData())));
}

const Logic* NewAtomBuiltins() {
  NativeModule* result = new NativeModule("nativeAtom");
  result->AddFunction("equal", &Equals);
  result->AddFunction("name", &Name);
  result->AddLogic("table", AtomTableBuilderLogic::GetSingleton());
  return result;
}

}  // namespace

const Logic* GetAtomBuiltins() {
  static const Logic* const kSingleton = NewAtomBuiltins();
  return kSingleton;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
