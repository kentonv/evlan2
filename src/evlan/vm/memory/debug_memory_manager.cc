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

#include "evlan/vm/memory/debug_memory_manager.h"

#include <set>
#include <stack>

#include "evlan/vm/memory/debug_allocator.h"
#include "evlan/stl-util.h"
#include "evlan/common.h"

namespace evlan {
namespace vm {
namespace memory {

class DebugMemoryManager::Block {
 public:
  inline Block(Allocation* allocation, int index, int size,
               Allocator* allocator)
    : allocator_(allocator),
      allocation_(allocation),
      index_(index),
      size_(size),
      original_size_(size),
      bytes_(allocator->Allocate(size)) {
  }

  inline ~Block() {
    allocator_->Free(bytes_, original_size_);
  }

  inline Allocation* allocation() const { return allocation_; }
  inline int index() const              { return index_; }
  inline int size() const               { return size_; }

  inline       byte* bytes()       { return bytes_; }
  inline const byte* bytes() const { return bytes_; }

  inline void Withdraw(int size) { size_ -= size; }

 private:
  Allocator* allocator_;
  Allocation* allocation_;
  int index_;

  int size_;
  int original_size_;
  byte* bytes_;
};

class DebugMemoryManager::Allocation {
 public:
  inline Allocation(int index): index_(index), activated_(false) {}
  inline ~Allocation() { STLDeleteElements(&blocks_); }

  inline int block_count() const { return blocks_.size(); }

  inline       Block* block(int index)       { return blocks_[index]; }
  inline const Block* block(int index) const { return blocks_[index]; }

  inline Block* AddBlock(int size, Allocator* allocator) {
    Block* result = new Block(this, blocks_.size(), size, allocator);
    blocks_.push_back(result);
    return result;
  }

  inline int index() const { return index_; }

  inline void Activate() { activated_ = true; }
  inline void Deactivate() { activated_ = false; }

  inline bool activated() const { return activated_; }

 private:
  vector<Block*> blocks_;
  const int index_;
  bool activated_;
};

class DebugMemoryManager::DebugObjectTracker : public ObjectTracker {
 public:
  DebugObjectTracker(DebugMemoryManager* manager, int allocation_index,
                     TrackedObject* object)
    : manager_(manager), allocation_index_(allocation_index),
      external_refcount_(0), object_(object) {}
  ~DebugObjectTracker() { object_->NotReachable(); }

  inline DebugMemoryManager* manager() const { return manager_; }

  // A TrackedObject can only contain pointers to allocations created before
  // it.  To enforce this, we keep track of the index of the last allocation
  // that existed when AddTrackedObject() was called.  Note that -1 is a valid
  // allocation index, meaning that the object predates all allocations.
  inline int allocation_index() const { return allocation_index_; }
  inline void set_allocation_index(int index) { allocation_index_ = index; }

  inline TrackedObject* object() const { return object_; }

  // implements ObjectTracker ----------------------------------------
  TrackedObject* GetTrackedObject() { return object_; }

  void AddExternalReference() {
    if (external_refcount_ == 0) {
      manager_->external_references_.insert(this);
    }

    ++external_refcount_;
  }

  void RemoveExternalReference() {
    --external_refcount_;
    GOOGLE_CHECK_GE(external_refcount_, 0);

    if (external_refcount_ == 0) {
      manager_->external_references_.erase(this);
    }
  }

 private:
  DebugMemoryManager* manager_;
  int allocation_index_;
  int external_refcount_;

  TrackedObject* object_;
};

class DebugMemoryManager::DebugBacklink : public Backlink {
 public:
  DebugBacklink(DebugMemoryManager* manager, int allocation_index)
    : manager_(manager), allocation_index_(allocation_index),
      bytes_(NULL), guide_(NULL) {}
  ~DebugBacklink() {}

  void Accept(MemoryVisitor* visitor) const {
    if (bytes_ != NULL) {
      visitor->VisitBytesPointer(&bytes_, guide_);
    }
  }

  inline DebugMemoryManager* manager() const { return manager_; }

  // Same as DebugObjectTracker::allocation_index().
  inline int allocation_index() const { return allocation_index_; }
  inline void set_allocation_index(int index) { allocation_index_ = index; }

  // implements Backlink ---------------------------------------------
  const byte* GetBytes() { return bytes_; }
  void SetBytes(const byte* target, const MemoryGuide* guide) {
    if (target != NULL) {
      Block* block = manager_->FindBlock(target);
      GOOGLE_CHECK(block != NULL)
        << ": Backlink::SetBytes() called with unknown pointer.";
    }

    bytes_ = target;
    guide_ = guide;
  }

 private:
  DebugMemoryManager* manager_;
  int allocation_index_;

  const byte* bytes_;
  const MemoryGuide* guide_;
};

class DebugMemoryManager::DebugMemoryVisitor : public MemoryVisitor {
 public:
  DebugMemoryVisitor(DebugMemoryManager* manager);
  ~DebugMemoryVisitor();

  void VisitRoot(const MemoryRoot* root,
                 const set<DebugObjectTracker*>& external_references);
  void Sweep();

  // implements MemoryVisitor ----------------------------------------
  void VisitBytesPointer(const byte* const* pointer, const MemoryGuide* guide);
  void VisitBytesSequencePointer(const byte* const* pointer,
                                 const MemoryGuide* guide, int count);
  void VisitRawBytesPointer(const byte* const* pointer, int size);
  void VisitTrackedObject(ObjectTracker* tracker);
  void VisitBacklink(const Backlink* backlink);

 private:
  Block* ValidateVisitedPointer(const byte* const* pointer);

  // Identifies a segment of memory which has been found to be reachable.
  struct BlockMark {
    Block* block;
    const byte* bytes;
    int size;
    int sequence_count;
    const MemoryGuide* guide;
    set<const byte* const*> pointers;
  };

  // We order BlockMarks so that we can eliminate duplicates.  Note that if
  // two BlockMarks have the same pointer but different sequence_counts, we
  // just take the mark with the largest sequence_count, since it necessarily
  // covers the other one.  We do not consider size in the ordering because
  // it is determined by calling the MemoryGuide on the pointer, so it is not
  // variable.
  struct BlockMarkOrdering {
    inline bool operator()(const BlockMark* a, const BlockMark* b) const {
      if (a->block != b->block) return a->block < b->block;
      if (a->bytes != b->bytes) return a->bytes < b->bytes;
      if (a->guide != b->guide) return a->guide < b->guide;
      if (a->sequence_count != b->sequence_count) {
        return a->sequence_count < b->sequence_count;
      }
      return false;
    }
  };

  typedef set<BlockMark*, BlockMarkOrdering> BlockMarkSet;

  // Identifies a contiguous chunk of memory which has been moved from one
  // location to another.
  struct Segment {
    const byte* old_location;
    byte* new_location;
    int size;
  };

  DebugMemoryManager* const manager_;

  // These track things that have been found to be reachable.
  BlockMarkSet marked_blocks_;
  set<const DebugObjectTracker*> marked_object_trackers_;
  set<const DebugBacklink*> marked_backlinks_;
  vector<bool> marked_allocations_;

  // See mark_queue_, below.
  struct MarkTask {
    enum { BLOCK, OBJECT, BACKLINK } type;
    union {
      BlockMark* block_mark;
      const DebugObjectTracker* object_tracker;
      const DebugBacklink* backlink;
    };
  };

  // When scanning discovers something new that needs to be marked, it is
  // placed in the queue.  Which is actually implemented as a stack in order
  // to get depth-first semantics.
  stack<MarkTask*> mark_queue_;

  // When calling a particular MemoryGuide, we want to make sure that it does
  // not attempt to visit any pointers outside of the segment of memory it is
  // supposed to be guiding us through.  The fields below keep track of what
  // memory we expect to be visiting.  NULL = Marking non-tracked space.
  const byte* current_segment_;
  const byte* current_block_end_;

  // This field keeps track of the offset of the most distant pointer we visit
  // from the beginning of the memory we are visiting.  Once
  // MemoryGuide::Accept() returns, we verify that the returned size is larger
  // than this.
  ptrdiff_t current_segment_observed_size_;

  // Pointers are required to only point to older memory.
  int current_allocation_index_;
};

// ===================================================================

DebugMemoryManager::DebugMemoryManager()
  : malloc_allocator_(new MallocAllocator),
    debug_allocator_(new DebugAllocator(malloc_allocator_.get())),
    allocator_(debug_allocator_.get()),
    allocate_counter_(0), sweep_frequency_(1),
    allow_activations_(true), allow_extensions_(true) {
}

DebugMemoryManager::DebugMemoryManager(Allocator* allocator)
  : allocator_(allocator),
    allocate_counter_(0), sweep_frequency_(1),
    allow_activations_(true), allow_extensions_(true) {
}

DebugMemoryManager::~DebugMemoryManager() {
  STLDeleteElements(&allocations_);
  STLDeleteElements(&object_trackers_);
  STLDeleteElements(&backlinks_);
}

void DebugMemoryManager::Collect(const MemoryRoot* root) {
  DebugMemoryVisitor visitor(this);
  visitor.VisitRoot(root, external_references_);
  visitor.Sweep();
}

byte* DebugMemoryManager::AllocateBytes(int size, const MemoryRoot* root) {
  if (++allocate_counter_ >= sweep_frequency_) {
    allocate_counter_ = 0;
    Collect(root);
  }

  if (!allocations_.empty()) {
    allocations_.back()->Deactivate();
  }

  Allocation* allocation = AddAllocation();
  allocation->Activate();

  return AddBlock(allocation, size)->bytes();
}

byte* DebugMemoryManager::TryActivateBytes(const byte* bytes) {
  Block* block = FindBlock(bytes);
  GOOGLE_CHECK(block != NULL) << ": TryActivateBytes() called with unknown pointer.";

  GOOGLE_CHECK(!allocations_.empty());
  if (allow_activations_ && block->allocation() == allocations_.back()) {
    block->allocation()->Activate();
    return const_cast<byte*>(bytes);
  } else {
    return NULL;
  }
}

byte* DebugMemoryManager::TryExtendBytes(int size) {
  GOOGLE_CHECK(!allocations_.empty())
    << ": TryExtendBytes() called before any allocations.";
  GOOGLE_CHECK(allocations_.back()->activated())
    << ": TryExtendBytes() called without first activating an allocation.";

  if (!allow_extensions_) return NULL;

  return AddBlock(allocations_.back(), size)->bytes();
}

bool DebugMemoryManager::TryWithdrawBytes(const byte* bytes, int size) {
  Block* block = FindBlock(bytes);
  GOOGLE_CHECK(block != NULL) << ": TryWithdrawBytes() called with unknown pointer.";

  GOOGLE_CHECK(!allocations_.empty());

  if (block->allocation() != allocations_.back()) {
    // Not the active allocation.
    return false;
  }

  Allocation* allocation = block->allocation();
  for (int i = block->index() + 1; i < allocation->block_count(); i++) {
    if (allocation->block(i)->size() != 0) {
      // Not the last block of the active allocation.
      return false;
    }
  }
  if (bytes != block->bytes() + block->size() - size) {
    // Not the last bytes of the block.
    return false;
  }

  // TODO(kenton):  If the whole allocation is withdrawn then we should
  //   activate the previous allocation...  if there were no tracked objects,
  //   backlinks, etc. between allocations.
  block->Withdraw(size);
  return true;
}

// ===================================================================

ObjectTracker* DebugMemoryManager::AddTrackedObject(
    TrackedObject* object) {
  DebugObjectTracker* result =
    new DebugObjectTracker(this, implicit_cast<int>(allocations_.size()) - 1,
                           object);
  object_trackers_.push_back(result);
  return result;
}

bool DebugMemoryManager::ReviveTrackedObject(ObjectTracker* object_tracker) {
  const DebugObjectTracker* debug_tracker =
    down_cast<DebugObjectTracker*>(object_tracker);
  GOOGLE_CHECK_EQ(debug_tracker->manager(), this)
    << ": ReviveTrackedObject() called with an ObjectTracker belonging to a "
       "different MemoryManager.";
  return true;
}

Backlink* DebugMemoryManager::MakeBacklink() {
  DebugBacklink* result = new DebugBacklink(this, allocations_.size() - 1);
  backlinks_.push_back(result);
  return result;
}

// ===================================================================

DebugMemoryManager::Allocation* DebugMemoryManager::AddAllocation() {
  Allocation* allocation = new Allocation(allocations_.size());
  allocations_.push_back(allocation);
  return allocation;
}

DebugMemoryManager::Block* DebugMemoryManager::AddBlock(
    Allocation* allocation, int size) {
  Block* block = allocation->AddBlock(size, allocator_);
  block_map_[block->bytes()] = block;
  return block;
}

DebugMemoryManager::Block* DebugMemoryManager::FindBlock(const byte* bytes) {
  map<const byte*, Block*>::iterator iter = block_map_.upper_bound(bytes);

  if (iter == block_map_.begin()) return NULL;

  --iter;
  Block* block = iter->second;

  GOOGLE_CHECK_GE(bytes, block->bytes());
  if (bytes < block->bytes() + block->size()) {
    return block;
  } else {
    return NULL;
  }
}

// ===================================================================

DebugMemoryManager::DebugMemoryVisitor::DebugMemoryVisitor(
    DebugMemoryManager* manager)
  : manager_(manager) {
  marked_allocations_.resize(manager_->allocations_.size());
}
DebugMemoryManager::DebugMemoryVisitor::~DebugMemoryVisitor() {
  STLDeleteElements(&marked_blocks_);
}

void DebugMemoryManager::DebugMemoryVisitor::VisitRoot(
    const MemoryRoot* root,
    const set<DebugObjectTracker*>& external_references) {
  current_segment_ = NULL;
  current_allocation_index_ = manager_->allocations_.size();

  for (set<DebugObjectTracker*>::iterator iter = external_references.begin();
       iter != external_references.end(); ++iter) {
    VisitTrackedObject(*iter);
  }

  root->Accept(this);

  while (!mark_queue_.empty()) {
    scoped_ptr<MarkTask> task(mark_queue_.top());
    mark_queue_.pop();

    switch (task->type) {
      case MarkTask::BLOCK: {
        BlockMark* block_mark = task->block_mark;
        const Block* block = block_mark->block;

        current_segment_ = block_mark->bytes;
        current_allocation_index_ = block->allocation()->index();
        current_block_end_ = block->bytes() + block->size();
        current_segment_observed_size_ = 0;

        block_mark->size =
          block_mark->guide->Accept(block_mark->bytes,
                                    block_mark->sequence_count,
                                    this);

        GOOGLE_CHECK_GE(block_mark->size, current_segment_observed_size_)
          << ": MemoryGuide::Accept() visited pointers outside the region it "
             "was supposed to be visiting, or returned a size that was "
             "smaller than the region it was visiting.";

        GOOGLE_CHECK_LE(current_segment_ + block_mark->size,
                 block->bytes() + block->size())
          << ": MemoryGuide::Accept() returned a size that extends beyond the "
             "end of the block.";
        break;
      }

      case MarkTask::OBJECT:
        current_segment_ = NULL;
        current_allocation_index_ = task->object_tracker->allocation_index();
        task->object_tracker->object()->Accept(this);
        break;

      case MarkTask::BACKLINK:
        current_segment_ = NULL;
        current_allocation_index_ = manager_->allocations_.size();
        task->backlink->Accept(this);
        break;
    }
  }
}

void DebugMemoryManager::DebugMemoryVisitor::Sweep() {
  // Set up new allocations.
  vector<Allocation*> old_allocations;
  old_allocations.swap(manager_->allocations_);
  manager_->block_map_.clear();
  manager_->allocations_.reserve(old_allocations.size());

  vector<Allocation*> allocation_map;
  allocation_map.resize(old_allocations.size());

  for (int i = 0; i < old_allocations.size(); i++) {
    if (marked_allocations_[i]) {
      allocation_map[i] = manager_->AddAllocation();
    }
  }

  // Allocate new blocks.
  vector<Segment> segments;

  BlockMarkSet::iterator iter = marked_blocks_.begin();
  while (iter != marked_blocks_.end()) {
    Block* block = (*iter)->block;

    // Collect a set of BlockMarks which cover adjacent or overlapping memory.
    // These form a segment of memory which will be copied all at once to a
    // new location.
    BlockMarkSet::iterator marks_begin = iter;
    const byte* bytes_begin = (*iter)->bytes;
    const byte* bytes_end = bytes_begin;

    while (iter != marked_blocks_.end() &&
           (*iter)->block == block &&
           (*iter)->bytes <= bytes_end) {
      // If necessary, extend bytes_end so that it covers this BlockMark.
      GOOGLE_CHECK_GE((*iter)->size, 0);
      const byte* new_end = (*iter)->bytes + (*iter)->size;
      if (new_end > bytes_end) bytes_end = new_end;
      ++iter;
    }

    BlockMarkSet::iterator marks_end = iter;

    // Allocate the new block.
    Block* new_block = manager_->AddBlock(
      allocation_map[block->allocation()->index()],
      bytes_end - bytes_begin);

    // Record this segment for later copying.  We can't copy it now because
    // we need to remap all pointers inside it first.
    Segment segment;
    segment.old_location = bytes_begin;
    segment.new_location = new_block->bytes();
    segment.size = bytes_end - bytes_begin;
    segments.push_back(segment);

    // Remap all pointers to this segment.  (These are pointers pointing
    // *to* this segment, not necessarily pointers inside this segment.)
    for (BlockMarkSet::iterator i = marks_begin; i != marks_end; ++i) {
      const set<const byte* const*>& pointers = (*i)->pointers;
      for (set<const byte* const*>::iterator j = pointers.begin();
           j != pointers.end(); ++j) {
        const byte** pointer = const_cast<const byte**>(*j);
        *pointer = *pointer - segment.old_location + segment.new_location;
      }
    }
  }

  // Copy.
  for (int i = 0; i < segments.size(); i++) {
    memcpy(segments[i].new_location, segments[i].old_location,
           segments[i].size);
  }

  // Delete unreachable ObjectTrackers and remap allocation_index for reachable
  // ones.
  vector<DebugObjectTracker*> old_trackers;
  std::swap(manager_->object_trackers_, old_trackers);

  for (int i = 0; i < old_trackers.size(); i++) {
    DebugObjectTracker* tracker = old_trackers[i];
    if (marked_object_trackers_.count(tracker) > 0) {
      manager_->object_trackers_.push_back(tracker);
      int old_index = tracker->allocation_index();
      if (old_index != -1) {
        tracker->set_allocation_index(allocation_map[old_index]->index());
      }
    } else {
      delete tracker;
    }
  }

  // Delete unreachable Backlinks and remap allocation_index for reachable
  // ones.
  vector<DebugBacklink*> old_backlinks;
  std::swap(manager_->backlinks_, old_backlinks);

  for (int i = 0; i < old_backlinks.size(); i++) {
    DebugBacklink* backlink = old_backlinks[i];
    if (marked_backlinks_.count(backlink) > 0) {
      manager_->backlinks_.push_back(backlink);
      int old_index = backlink->allocation_index();
      if (old_index != -1) {
        backlink->set_allocation_index(allocation_map[old_index]->index());
      }
    } else {
      delete backlink;
    }
  }

  // Delete old allocations.
  STLDeleteElements(&old_allocations);
}

DebugMemoryManager::Block*
DebugMemoryManager::DebugMemoryVisitor::ValidateVisitedPointer(
    const byte* const* pointer) {
  if (current_segment_ == NULL) {
    GOOGLE_CHECK(manager_->FindBlock(reinterpret_cast<const byte*>(pointer)) == NULL)
      << ": While visiting unmanaged memory (e.g. a TrackedObject or the root "
         "object), VisitPointer() was called to visit a pointer that is in "
         "tracked memory (as in, the pointer itself is tracked, in addition "
         "to the thing it points at).";
  } else {
    const byte* pointer_location = reinterpret_cast<const byte*>(pointer);

    GOOGLE_CHECK_GE(pointer_location, current_segment_)
      << ": VisitPointer() was called to visit a pointer which is not in the "
         "memory segment currently being visited.";
    GOOGLE_CHECK_LT(pointer_location, current_block_end_)
      << ": VisitPointer() was called to visit a pointer which is not in the "
         "memory segment currently being visited.";

    // Remember the largest pointer we saw so we can check it against the
    // size returned by MemoryGuide::Accept().
    current_segment_observed_size_ =
      max(current_segment_observed_size_,
          pointer_location - current_segment_ +
            implicit_cast<ptrdiff_t>(sizeof(*pointer)));
  }

  Block* block = manager_->FindBlock(*pointer);

  GOOGLE_CHECK(block != NULL)
    << ": VisitPointer() called with a pointer not pointing to tracked memory.";

  GOOGLE_CHECK_LE(block->allocation()->index(), current_allocation_index_)
    << ": VisitTrackedObject() called with an ObjectTracker which is newer "
       "than the memory currently being visited.  Only Backlinks are allowed "
       "to point to point from older memory to newer.";

  return block;
}

void DebugMemoryManager::DebugMemoryVisitor::VisitBytesPointer(
    const byte* const* pointer, const MemoryGuide* guide) {
  VisitBytesSequencePointer(pointer, guide, 1);
}

void DebugMemoryManager::DebugMemoryVisitor::VisitBytesSequencePointer(
    const byte* const* pointer, const MemoryGuide* guide, int count) {
  if (count == 0) return;

  Block* block = ValidateVisitedPointer(pointer);

  marked_allocations_[block->allocation()->index()] = true;

  BlockMark key;
  key.block = block;
  key.bytes = *pointer;
  key.size  = -1;  // will be set later
  key.sequence_count = count;
  key.guide = guide;

  BlockMarkSet::iterator iter = marked_blocks_.find(&key);
  if (iter != marked_blocks_.end()) {
    // We already know about the target segment.  Just add this pointer
    // to the list of pointers to be updated.
    (*iter)->pointers.insert(pointer);

    // Also make sure that the sequence count is large enough to cover both.
    // For example, one pointer may have pointed to an array of six objects,
    // while the other pointed at the same array but only the first three
    // objects in it.
    // TODO(jcd): separate BlockMarks are now created for different
    // sequence_counts until a related bug is fixed.
    // (*iter)->sequence_count = max(count, (*iter)->sequence_count);

    return;
  }

  // We haven't seen this segment before.
  BlockMark* mark = new BlockMark(key);
  mark->pointers.insert(pointer);
  marked_blocks_.insert(mark);

  MarkTask* task = new MarkTask;
  task->type = MarkTask::BLOCK;
  task->block_mark = mark;
  mark_queue_.push(task);
}

void DebugMemoryManager::DebugMemoryVisitor::VisitRawBytesPointer(
    const byte* const* pointer, int size) {
  Block* block = ValidateVisitedPointer(pointer);

  marked_allocations_[block->allocation()->index()] = true;

  BlockMark key;
  key.block = block;
  key.bytes = *pointer;
  key.size  = size;
  key.guide = NULL;

  BlockMarkSet::iterator iter = marked_blocks_.find(&key);
  if (iter != marked_blocks_.end()) {
    // We already know about the target segment.  Just add this pointer
    // to the list of pointers to be updated.
    (*iter)->pointers.insert(pointer);

    // Different pointers might specify different target sizes.  As long as
    // we accomodate the largest one, we have them all covered.
    (*iter)->size = max((*iter)->size, size);

    return;
  }

  // We haven't seen this segment before.
  BlockMark* mark = new BlockMark(key);
  mark->pointers.insert(pointer);
  marked_blocks_.insert(mark);

  // We don't have to add anything to mark_queue_ since there are no pointers
  // in the target memory.
}

void DebugMemoryManager::DebugMemoryVisitor::VisitTrackedObject(
    ObjectTracker* tracker) {
  const DebugObjectTracker* debug_tracker =
    down_cast<DebugObjectTracker*>(tracker);

  GOOGLE_CHECK_EQ(debug_tracker->manager(), manager_)
    << ": VisitTrackedObject() called with an ObjectTracker belonging to a "
       "different MemoryManager.";

  GOOGLE_CHECK_LE(debug_tracker->allocation_index(), current_allocation_index_)
    << ": VisitTrackedObject() called with an ObjectTracker which is newer "
       "than the memory currently being visited.  Only Backlinks are allowed "
       "to point to point from older memory to newer.";

  bool first_time_seen = marked_object_trackers_.insert(debug_tracker).second;

  if (first_time_seen) {
    if (debug_tracker->allocation_index() != -1) {
      marked_allocations_[debug_tracker->allocation_index()] = true;
    }

    MarkTask* task = new MarkTask;
    task->type = MarkTask::OBJECT;
    task->object_tracker = debug_tracker;
    mark_queue_.push(task);
  }
}

void DebugMemoryManager::DebugMemoryVisitor::VisitBacklink(
    const Backlink* backlink) {
  const DebugBacklink* debug_backlink =
    down_cast<const DebugBacklink*>(backlink);

  GOOGLE_CHECK_EQ(debug_backlink->manager(), manager_)
    << ": VisitBacklink() called with a Backlink belonging to a different "
       "MemoryManager.";

  GOOGLE_CHECK_LE(debug_backlink->allocation_index(), current_allocation_index_)
    << ": VisitBacklink() called with a Backlink which is newer than the "
       "memory currently being visited.  Only Backlinks themselves are allowed "
       "to point to point from older memory to newer.";

  bool first_time_seen = marked_backlinks_.insert(debug_backlink).second;

  if (first_time_seen) {
    if (debug_backlink->allocation_index() != -1) {
      marked_allocations_[debug_backlink->allocation_index()] = true;
    }

    MarkTask* task = new MarkTask;
    task->type = MarkTask::BACKLINK;
    task->backlink = debug_backlink;
    mark_queue_.push(task);
  }
}

}  // namespace memory
}  // namespace vm
}  // namespace evlan
