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
// This file defines an abstract interface for a low-level memory allocator.

#ifndef EVLAN_VM_MEMORY_ALLOCATOR_H__
#define EVLAN_VM_MEMORY_ALLOCATOR_H__

#include "evlan/vm/base.h"

namespace evlan {
namespace vm {
namespace memory {

// Abstract interface for an implementation of malloc()/free().  Building
// MemoryManagers on top of this allows for better testing of the
// MemoryManager itself by using a mock allocator.
class Allocator {
 public:
  virtual ~Allocator();

  // Allocate some bytes, aligned to the nearest word boundary.
  virtual byte* Allocate(int size) = 0;

  // Free some bytes.  The pointer must be one returned by Allocate() and the
  // size must be exactly the same as was passed to Allocate().
  virtual void Free(byte* bytes, int size) = 0;
};

// The obvious implementation.
class MallocAllocator : public Allocator {
 public:
  MallocAllocator();
  ~MallocAllocator();

  // Get the sum of all the sizes passed to Allocate(), whether or not they
  // were later Free()ed.
  // TODO(kenton):  It would be more modular to implement stats collection as
  //   a wrapper Allocator rather than putting it directly into MallocAllocator.
  uint64 GetTotalBytesAllocated();

  // implements Allocator --------------------------------------------
  virtual byte* Allocate(int size);
  virtual void Free(byte* bytes, int size);

 private:
  uint64 total_bytes_allocated_;
};

}  // namespace memory
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_MEMORY_ALLOCATOR_H__

