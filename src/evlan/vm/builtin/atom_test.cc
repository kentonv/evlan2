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

#include "evlan/vm/builtin/atom.h"
#include <string>
#include <vector>
#include <stdlib.h>
#include "evlan/vm/memory/debug_memory_manager.h"
#include "evlan/testing/test_util.h"
#include "evlan/strutil.h"
#include <gtest/gtest.h>

// Enable to test for a bug in atom tables that occurred when
// the process had allocated enough memory that operator new
// began returning pointers with the high bit set.  This
// only works in 32-bit mode and requires allocating over 2GB
// of RAM.
bool FLAGS_test_negative_intptr_bug = false;

namespace evlan {
namespace vm {
namespace builtin {
namespace {

struct ValueRoot : public memory::MemoryRoot {
  Value value;

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    value.Accept(visitor);
  }
};

TEST(AtomRegistryTest, AtomEquality) {
  memory::DebugMemoryManager memory_manager;
  AtomRegistry atom_registry;

  Data foo1 = atom_registry.GetAtomData(&memory_manager, "foo");
  Data bar1 = atom_registry.GetAtomData(&memory_manager, "bar");
  Data foo2 = atom_registry.GetAtomData(&memory_manager, "foo");
  Data bar2 = atom_registry.GetAtomData(&memory_manager, "bar");

  EXPECT_TRUE (AtomsAreEqual(foo1, foo2));
  EXPECT_FALSE(AtomsAreEqual(foo1, bar1));
  EXPECT_TRUE (AtomsAreEqual(bar1, bar2));
}

TEST(AtomRegistryTest, GarbageCollected) {
  memory::DebugMemoryManager memory_manager;
  AtomRegistry atom_registry;

  Data foo1 = atom_registry.GetAtomData(&memory_manager, "foo");
  Data bar1 = atom_registry.GetAtomData(&memory_manager, "bar");

  ValueRoot root;
  root.value = Value(GetAtomLogic(), foo1);
  memory_manager.Collect(&root);

  // The Sweep() should have caused the "bar" atom to be deleted.  If we simply
  // allocate it again, though, chances are it will end up in the same memory,
  // thus it will be equal to bar1.  So, let's allocate a different atom first
  // to avoid that problem.
  atom_registry.GetAtomData(&memory_manager, "dummy");

  // Now get foo and bar again.
  Data foo2 = atom_registry.GetAtomData(&memory_manager, "foo");
  Data bar2 = atom_registry.GetAtomData(&memory_manager, "bar");

  EXPECT_TRUE (AtomsAreEqual(foo1, foo2));
  EXPECT_FALSE(AtomsAreEqual(foo1, bar1));
  EXPECT_FALSE(AtomsAreEqual(bar1, bar2));
}

TEST(AtomRegistryTest, DebugString) {
  memory::DebugMemoryManager memory_manager;
  AtomRegistry atom_registry;

  EXPECT_EQ("@foo",
    atom_registry.GetAtom(&memory_manager, "foo").DebugString());
}

TEST(AtomRegistryTest, GetAtomName) {
  memory::DebugMemoryManager memory_manager;
  AtomRegistry atom_registry;

  Data atom = atom_registry.GetAtomData(&memory_manager, "foo");
  EXPECT_EQ(StringPiece("foo"), GetAtomName(atom));
}

TEST(AtomRegistryTest, ChildRegistry) {
  memory::DebugMemoryManager memory_manager;
  AtomRegistry parent;

  Data foo = parent.GetAtomData(&memory_manager, "foo");

  {
    AtomRegistry child1(&parent);
    AtomRegistry child2(&parent);

    Data foo1 = child1.GetAtomData(&memory_manager, "foo");
    Data foo2 = child2.GetAtomData(&memory_manager, "foo");
    Data bar1 = child1.GetAtomData(&memory_manager, "bar");
    Data bar2 = child2.GetAtomData(&memory_manager, "bar");

    // "foo" was created in the parent so it should be the same in both
    // children.
    EXPECT_TRUE(AtomsAreEqual(foo, foo1));
    EXPECT_TRUE(AtomsAreEqual(foo, foo2));
    EXPECT_TRUE(AtomsAreEqual(foo1, foo2));

    // "bar" was only created in each child so they should be different.
    EXPECT_FALSE(AtomsAreEqual(bar1, bar2));
    EXPECT_TRUE(
      AtomsAreEqual(bar1, child1.GetAtomData(&memory_manager, "bar")));
    EXPECT_TRUE(
      AtomsAreEqual(bar2, child2.GetAtomData(&memory_manager, "bar")));

    // We cannot create new atoms in the parent while the children exist.
    EXPECT_DEBUG_DEATH(parent.GetAtomData(&memory_manager, "baz"),
      "Cannot call AtomRegistry::GetAtomData\\(\\) on a registry that has "
      "children\\.");
  }

  // Verify that we can call parent.GetAtomData() now that the children are
  // gone.
  EXPECT_EQ(StringPiece("baz"), GetAtomName(parent.GetAtomData(&memory_manager, "baz")));
}

class AtomBuiltinFunctionsTest : public EvlanTest {
 protected:
  virtual void SetUp() {
    AddImport("atom", Value(GetAtomBuiltins(), Data()));
  }
};

TEST_F(AtomBuiltinFunctionsTest, Equal) {
  ExpectEvaluatesToBoolean(true, "import \"atom\".equal(@foo, @foo)");
  ExpectEvaluatesToBoolean(false, "import \"atom\".equal(@foo, @bar)");

  ExpectEvaluatesToError(
    "Wrong type for first argument to equal().  Expected atom, got: 123",
    "import \"atom\".equal(123, @bar)");
  ExpectEvaluatesToError(
    "Wrong type for second argument to equal().  Expected atom, got: 123",
    "import \"atom\".equal(@foo, 123)");
}

TEST_F(AtomBuiltinFunctionsTest, Name) {
  ExpectEvaluatesToString("foo", "import \"atom\".name(@foo)");

  ExpectEvaluatesToError(
    "Wrong type for first argument to atomName().  Expected atom, got: 123",
    "import \"atom\".name(123)");
}

TEST_F(AtomBuiltinFunctionsTest, Table) {
  ExpectEvaluatesToInteger(0,
    "import \"atom\".table({@foo, @bar, @baz})(@foo)");

  ExpectEvaluatesToInteger(1,
    "import \"atom\".table({@foo, @bar, @baz})(@bar)");

  ExpectEvaluatesToInteger(2,
    "import \"atom\".table({@foo, @bar, @baz})(@baz)");

  ExpectEvaluatesToError(
    "Atom not found in table: @qux",
    "import \"atom\".table({@foo, @bar, @baz})(@qux)");

  ExpectEvaluatesToError(
    "Parameter to atom table must be an atom.",
    "import \"atom\".table({@foo, @bar, @baz})(123)");

  ExpectEvaluatesToError(
    "Atom not found in table: @foo",
    "import \"atom\".table({})(@foo)");

  ExpectEvaluatesToError(
    "Wrong type passed to native function atomTable().  "
      "Expected array, got: 123",
    "import \"atom\".table(123)");

  ExpectEvaluatesToError(
    "Wrong type for element 1 of array passed to atomTable().  "
      "Expected atom, got: 123",
    "import \"atom\".table({@foo, 123, @bar})");

  ExpectEvaluatesToDebugString(
    "atomTable({@foo, @bar, @baz, @qux, @quux, @corge})",
    "import \"atom\".table({@foo, @bar, @baz, @qux, @quux, @corge})");
}

TEST_F(AtomBuiltinFunctionsTest, AtomTableStress) {
  // We keep this number low so that the test runs quickly, but if you suspect
  // a problem exists in atom tables, try using a higher kIterations in order
  // to test more random cases.
  const int kIterations = 10;

  if (FLAGS_test_negative_intptr_bug) {
    // Make sure that operator new is returning large pointer values for every
    // size of allocation from 4 bytes to 1k (in 4-byte steps).
    for (int i = 1024; i > 0; i -= 4) {
      while ((intptr_t)operator new(i) > 0) {}
    }
  }

  for (int i = 0; i < kIterations; i++) {
    // Un-comment this when running with large kIterations in order to watch
    // progress.
    //cerr << i << '\r';

    // We build an atom table with a random number of elements (up to 16) and
    // random contents, then apply it to a random atom in the table and expect
    // it to return the correct index.
    vector<int> values;
    string code = "import \"atom\".table({";

    int size = rand() % 16 + 1;
    for (int j = 0; j < size; j++) {
      int value = rand();
      values.push_back(value);
      if (j > 0) code += ", ";
      code += "@a";
      code += SimpleItoa(value);
    }
    code += "})";

    // We will look up the |index|th element.
    int index = rand() % size;
    code += "(@a";
    code += SimpleItoa(values[index]);
    code += ")";

    // At this point, |code| is something like:
    //   import "atom".table({@a58304, @a601923, @a23540})(@a601923)

    ExpectEvaluatesToInteger(index, code);
  }
}

}  // namespace
}  // namespace builtin
}  // namespace vm
}  // namespace evlan

