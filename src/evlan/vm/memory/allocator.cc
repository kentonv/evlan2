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

#include "evlan/vm/memory/allocator.h"

namespace evlan {
namespace vm {
namespace memory {

Allocator::~Allocator() {}

MallocAllocator::MallocAllocator() : total_bytes_allocated_(0) {}
MallocAllocator::~MallocAllocator() {}

uint64 MallocAllocator::GetTotalBytesAllocated() {
  return total_bytes_allocated_;
}

byte* MallocAllocator::Allocate(int size) {
  total_bytes_allocated_ += size;
  return reinterpret_cast<byte*>(operator new(size));
}

void MallocAllocator::Free(byte* bytes, int size) {
  return operator delete(bytes);
}

}  // namespace memory
}  // namespace vm
}  // namespace evlan

