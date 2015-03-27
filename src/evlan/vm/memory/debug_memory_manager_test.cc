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

// TODO(kenton):  This unit test could probably use more work.

#include "evlan/vm/memory/debug_memory_manager.h"
#include "evlan/vm/memory/debug_allocator.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace memory {
namespace {

class MockRoot : public MemoryRoot {
 public:
  virtual ~MockRoot() {}

  // implements MemoryRoot -------------------------------------------
  void Accept(MemoryVisitor* visitor) const {}
};

class MockObject {
 public:
  int value_;

  int Accept(MemoryVisitor* visitor) const {
    return sizeof(*this);
  }
};

class MockRootWithPointer : public MemoryRoot {
 public:
  MockRootWithPointer(int* call_count): call_count_(call_count) {}

  const MockObject* pointer_;

  // implements MemoryRoot -------------------------------------------

  void Accept(MemoryVisitor* visitor) const {
    ++(*call_count_);
    visitor->VisitPointer(&pointer_);
  }

 private:
  int* call_count_;
};

class MockTrackedObject : public TrackedObject {
 public:
  MockTrackedObject(): not_reachable_called_(false) {}
  ~MockTrackedObject() {}

  bool not_reachable_called() const { return not_reachable_called_; }

  // implements TrackedObject ----------------------------------------
  void NotReachable() { not_reachable_called_ = true; }
  void Accept(MemoryVisitor* visitor) const {}

 private:
  bool not_reachable_called_;
};

class DebugMemoryManagerTest : public testing::Test {
 protected:
  DebugMemoryManagerTest()
    : debug_allocator_(&malloc_allocator_) {}

  MallocAllocator malloc_allocator_;
  DebugAllocator debug_allocator_;
};

TEST_F(DebugMemoryManagerTest, AllocateBytes) {
  DebugMemoryManager manager(&debug_allocator_);

  MockRoot root;
  EXPECT_TRUE(manager.Allocate<int>(&root) != NULL);

  // The DebugAllocator will CHECK-fail if memory is not freed.
}

TEST_F(DebugMemoryManagerTest, TrackedObjects) {
  MockTrackedObject tracked_object;

  {  // scope for manager
    DebugMemoryManager manager(&debug_allocator_);
    ObjectTracker* tracker = manager.AddTrackedObject(&tracked_object);
    EXPECT_EQ(&tracked_object, tracker->GetTrackedObject());
    EXPECT_FALSE(tracked_object.not_reachable_called());
  }

  EXPECT_TRUE(tracked_object.not_reachable_called());
}

TEST_F(DebugMemoryManagerTest, TrackedObjectNotReachable) {
  MockTrackedObject tracked_object;

  DebugMemoryManager manager(&debug_allocator_);
  manager.AddTrackedObject(&tracked_object);

  MockRoot root;
  manager.Collect(&root);

  EXPECT_TRUE(tracked_object.not_reachable_called());
}

TEST_F(DebugMemoryManagerTest, TrackedObjectExternalReferences) {
  MockTrackedObject tracked_object;

  DebugMemoryManager manager(&debug_allocator_);
  ObjectTracker* tracker = manager.AddTrackedObject(&tracked_object);
  tracker->AddExternalReference();

  MockRoot root;
  manager.Collect(&root);

  EXPECT_FALSE(tracked_object.not_reachable_called());

  tracker->RemoveExternalReference();

  manager.Collect(&root);

  EXPECT_TRUE(tracked_object.not_reachable_called());
}

TEST_F(DebugMemoryManagerTest, Backlinks) {
  DebugMemoryManager manager(&debug_allocator_);

  MockRoot root;
  MockObject* dummy = manager.Allocate<MockObject>(&root);

  Backlink* backlink = manager.MakeBacklink();
  EXPECT_TRUE(backlink->GetBytes() == NULL);

  backlink->Set(dummy);
  EXPECT_EQ(dummy, backlink->Get<MockObject>());
}

TEST_F(DebugMemoryManagerTest, Collect) {
  // Allocate an object, run a sweep, then verify that the object has moved
  // but still contains the same value.

  DebugMemoryManager manager(&debug_allocator_);

  MockRoot root;
  MockObject* pointer = manager.Allocate<MockObject>(&root);
  ASSERT_TRUE(pointer != NULL);

  pointer->value_ = 12345;

  int call_count = 0;
  MockRootWithPointer new_root(&call_count);
  new_root.pointer_ = pointer;
  manager.Collect(&new_root);

  EXPECT_EQ(1, call_count);
  EXPECT_NE(new_root.pointer_, pointer);
  EXPECT_EQ(12345, new_root.pointer_->value_);
}

// Structure to hold a linked list in managed memory.
class ConstLinkedList {
 public:
  ConstLinkedList(int data_, const ConstLinkedList* next_)
    : data(data_),
      next(next_) {}

  int Accept(MemoryVisitor* visitor) const {
    if (next != NULL) {
      visitor->VisitPointer(&next);
    }
    return sizeof(*this);
  }

  void Print() const {
    if (this == NULL) {
      printf("\n");
      return;
    }
    printf(" %d ->", data);
    if (next == NULL) {
      printf("\n");
      return;
    }
    next->Print();
  }

  const int data;
  const ConstLinkedList* const next;
};

// Memory root that holds a vector of ConstLinkedList pointers.
class LinkedListVectorRoot : public MemoryRoot {
 public:
  virtual ~LinkedListVectorRoot() {}

  // implements MemoryRoot -------------------------------------------
  void Accept(MemoryVisitor* visitor) const {
    for (int i = 0; i < nodes.size(); i++) {
      visitor->VisitPointer(&nodes[i]);
    }
  }

  vector<const ConstLinkedList*> nodes;
};


TEST_F(DebugMemoryManagerTest, LinkedList) {
  DebugMemoryManager manager(&debug_allocator_);
  LinkedListVectorRoot root;

  for (int i = 1; i <= 10; i++) {
    ConstLinkedList* p = manager.Allocate<ConstLinkedList>(&root);
    p = new(p) ConstLinkedList(i,
                               root.nodes.empty() ? NULL : root.nodes.back());
    root.nodes.push_back(p);
    for (int j = 0; j < root.nodes.size(); j++) {
      root.nodes[j]->Print();
    }
  }
}

// Structure to hold an integer, and a pointer to a sequence of structures of
// the same type. 
class DataAndRange {
 public:
  DataAndRange(int data_, const DataAndRange* range_, int count_)
    : data(data_),
      range(range_),
      count(count_) {}

  int Accept(MemoryVisitor* visitor) const {
    if (range!= NULL) {
      visitor->VisitArrayPointer(&range, count);
    }
    return sizeof(*this);
  }

  void Print(int indent = 0) const {
    if (this == NULL) {
      printf("NULL\n");
      return;
    }
    for (int i = 0; i < indent; i++) printf("  ");
    printf("%d\n", data);
    for (int i = 0; i < count; i++) {
      range[i].Print(indent + 1);
    }
  }

  const int data;
  const DataAndRange* const range;  // A sequence of size count
  const int count;
};


// Memory root that holds a vector of DataAndRange pointers.
class DataAndRangeVectorRoot : public MemoryRoot {
 public:
  DataAndRangeVectorRoot() : temp(NULL) {}
  virtual ~DataAndRangeVectorRoot() {}

  // implements MemoryRoot -------------------------------------------
  void Accept(MemoryVisitor* visitor) const {
    if (temp) visitor->VisitArrayPointer(&temp, temp_count);
    for (int i = 0; i < ranges.size(); i++) {
      if (ranges[i]) {
        visitor->VisitPointer(&ranges[i]);
      }
    }
  }

  vector<const DataAndRange*> ranges;

  // A sequence which has not yet been stored in a DataAndRange.
  const DataAndRange* temp;
  int temp_count;
};


TEST_F(DebugMemoryManagerTest, DataAndRangeStressTest) {
  for (int iter = 0; iter < 100; iter++) {
    DebugMemoryManager manager(&debug_allocator_);
    DataAndRangeVectorRoot root;

    const int A = 10;
    const int B = 10;

    for (int i = 1; i <= A; i++) {
      DataAndRange* p = manager.AllocateArray<DataAndRange>(B, &root);
      for (int j = 0; j < B; j++) {
        const DataAndRange* x;
        int count;
        if (root.ranges.empty()) {
          x = NULL;
          count = 0;
        } else {
          int idx = rand()%B;
          x = &root.ranges.back()->range[idx];
          count = rand()%(B-idx)+1;
        }
        new(p+j) DataAndRange(i * 1000000 + j, x, count);
      }
      root.temp = p; root.temp_count = B;
      DataAndRange* q = manager.Allocate<DataAndRange>(&root);
      new(q) DataAndRange(i * 1000000 + 999999, root.temp, B);
      root.temp = 0;

      root.ranges.push_back(q);
    }
  }
}


}  // namespace
}  // namespace memory
}  // namespace vm
}  // namespace evlan
