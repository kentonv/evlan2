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
// The Evlan language has a basic data type called the "atom".  An atom is
// like a string which can be compared to other atoms much more quickly than
// strings can be compared.  This is accomplished by eliminating duplicate
// atoms across the VM, so that all atoms with the same text actually share
// the same position in memory, and thus can be compared by pointer.
//
// Atoms are written in Evlan using an at sign followed by an identifier.  For
// example, the atom "foo" is written in Evlan as:
//   @foo
// Note that it is technically possible for an atom's value to be something
// other than a valid identifier (e.g. containing non-alphanumeric characters),
// but since such atoms cannot be written in the Evlan language they are
// generally not used.
//
// Atoms serve two primary purposes in Evlan:
// * "Enum" values.  The Evlan equivalent to an enum is a type whose possible
//   values are some set of atoms.
// * Member names.  An Evlan object is simply a function whose input is an
//   atom which names one of the object's members.  The function then returns
//   that member.

#ifndef EVLAN_VM_ATOM_H__
#define EVLAN_VM_ATOM_H__

#include "evlan/common.h"
#include "evlan/stringpiece.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/vm/runtime.h"

namespace evlan {
namespace vm {
namespace builtin {

// Atoms must be registered in the VM's AtomRegistry in order to ensure that
// two atoms with the same name end up having the same pointer value.  A
// pointer to the global AtomRegistry can always be obtained from the current
// Context (see evlan/vm/runtime.h).
class AtomRegistry {
 public:
  AtomRegistry();

  // Create a forked AtomRegistry.  See EvlanEvaluator::Fork() in
  // evlan/public/evaluator.h.  The forked registry shares any values in the
  // parent, but when a new atom is created, the new atom is kept only in the
  // child and the parent is not modified.  The parent must not be modified or
  // deleted while the child exists.
  explicit AtomRegistry(AtomRegistry* parent);

  ~AtomRegistry();

  // Get an atom value, allocating it if necessary.  Note that multiple calls
  // to GetAtom() *must* use either the same memory_manager or memory_managers
  // which share a single set of ObjectTrackers.
  Value GetAtom(memory::MemoryManager* memory_manager,
                const StringPiece& name);

  // Just get the Data part of an atom, which can be manually combined with the
  // Logic returned by GetAtomLogic().
  Data GetAtomData(memory::MemoryManager* memory_manager,
                   const StringPiece& name);

 private:
  class Atom;
  class AtomLogic;
  struct State;

  friend class Atom;
  friend class AtomLogic;
  friend StringPiece GetAtomName(Data data);
  friend const Logic* GetAtomLogic();

  // The State is a separate object because it needs to be refcounted in case
  // the AtomRegistry is destroyed before the atoms inside it.
  State* state_;
};

// Compare atoms for equality.  Fast.
inline bool AtomsAreEqual(Data atom1, Data atom2) {
  return atom1.As<void>() == atom2.As<void>();
}

// Get the atom's text value, given a Data.  It's up to the caller to ensure
// that the Data came from a Value of atom type.
StringPiece GetAtomName(Data data);

// Get the Logic object associated with atoms.
const Logic* GetAtomLogic();

// Returns a Logic for an object containing various functions that operate on
// atoms.  This Logic completely ignores its associated Data.
const Logic* GetAtomBuiltins();

// Check if a Value represents an atom.
inline bool IsAtom(const Value& value) {
  return value.GetLogic() == GetAtomLogic();
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_ATOM_H__
