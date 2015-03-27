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

#include "evlan/vm/runtime.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "evlan/vm/hasher.h"
#include "evlan/common.h"
#include "evlan/strutil.h"
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace evlan {
namespace vm {

const int ValueId::kMaxValueIdSize;

GOOGLE_COMPILE_ASSERT(Hasher::kHashSize + 2 < ValueId::kMaxValueIdSize,
                      must_increase_max_id_size_to_match_hash_size_increase);

void ValueId::SetBytes(StringPiece bytes) {
  size_ = bytes.size();
  if (size_ > kMaxValueIdSize) {
    Hasher hasher;
    hasher.AddBytes(bytes);
    bytes_[0] = '=';
    hasher.Finish(bytes_ + 1);
    size_ = Hasher::kHashSize + 1;
  } else {
    memcpy(bytes_, bytes.data(), size_);
  }
}

void ValueId::SetBytesWithPrefix(char prefix, StringPiece bytes) {
  size_ = bytes.size() + 1;
  if (size_ > kMaxValueIdSize) {
    Hasher hasher;
    hasher.AddBytes(bytes);
    bytes_[0] = '=';
    bytes_[1] = prefix;
    hasher.Finish(bytes_ + 2);
    size_ = Hasher::kHashSize + 2;
  } else {
    bytes_[0] = prefix;
    memcpy(bytes_ + 1, bytes.data(), size_ - 1);
  }
}

void ValueId::SetBytesFromHasher(char prefix, Hasher* hasher) {
  size_ = Hasher::kHashSize + 1;
  bytes_[0] = prefix;
  hasher->Finish(bytes_ + 1);
}

Logic::~Logic() {}

namespace {

uint64 NewGlobalID() {
  uint64 result;
  int fd = open("/dev/urandom", O_RDONLY);
  GOOGLE_CHECK_GE(fd, 0) << "open(/dev/urandom): " << strerror(errno);
  GOOGLE_CHECK_EQ(read(fd, &result, sizeof(result)), sizeof(result))
       << "read(/dev/urandom): " << strerror(errno);
  GOOGLE_CHECK_GE(close(fd), 0)
        << "close(/dev/urandom): " << strerror(errno);
  return result;
}

// Class which generates a sequence of globally-unique IDs -- intended to be
// unique enough that one can be assigned to every individual value in an
// entire Evlan cluster.
//
// You might be wondering: "Is this secure?  These IDs are easily guessable if
// you have already received some other value from the same thread.".  Remember
// that guessability is not the point.  The question is whether a malicious
// user could write *Evlan* code that constructs a value that collides with
// this.  The answer is no:  Any value constructed by Evlan code would be
// assigned a new ID.
class IdGenerator {
 public:
  IdGenerator() {
    // NewGlobalID() should generate something unique enough that we can assume
    // no two threads cluster-wide will end up with the same ID.
    // (At 64 bits, it would NOT be unique enough for us to call NewGlobalID()
    // for every value marshalled.)
    state_.id = NewGlobalID();

    // We additionally append a counter which we will increment for every ID
    // we generate.  No one thread should be able to generate more than 2^64
    // IDs (it would take an impossibly long time), so the counter should never
    // wrap.
    state_.counter = 0;
  }

  StringPiece Next() {
    ++state_.counter;
    GOOGLE_CHECK_NE(state_.counter, 0) << "64-bit counter overflow!?";
    return StringPiece(reinterpret_cast<const char*>(&state_), sizeof(state_));
  }

 private:
  struct State {
    uint64 id;
    uint64 counter;
  };
  State state_;

  GOOGLE_COMPILE_ASSERT(sizeof(State) == sizeof(uint64) * 2,
                        struct_contained_unexpected_padding);
};

}  // namespace

ThreadLocal<IdGenerator> per_thread_id_generator_;

void Logic::GetId(Data data, ValueId* output) const {
  output->SetBytesWithPrefix('*', per_thread_id_generator_.pointer()->Next());
}

int Logic::Marshal(Data data, distribute::Marshaller* marshaller,
                   const ValueId* known_id) const {
  return -1;
}

void Logic::DebugPrintToStderr(Data data, int depth) const {
  google::protobuf::io::FileOutputStream output(STDERR_FILENO);
  google::protobuf::io::Printer printer(&output, '$');
  DebugPrint(data, depth, &printer);
  printer.Print("\n");
}

string Logic::DebugString(Data data, int depth) const {
  string result;
  google::protobuf::io::StringOutputStream output(&result);
  google::protobuf::io::Printer printer(&output, '$');
  DebugPrint(data, depth, &printer);
  return result;
}

void Value::DebugPrint(int depth, google::protobuf::io::Printer* printer) const {
  return logic_->DebugPrint(data_, depth, printer);
}

void Value::DebugPrintToStderr(int depth) const {
  return logic_->DebugPrintToStderr(data_, depth);
}

string Value::DebugString(int depth) const {
  return logic_->DebugString(data_, depth);
}

KeepAliveValue::KeepAliveValue(memory::MemoryManager* memory_manager,
                               Value value)
    : value_(value),
      tracker_(memory_manager->AddTrackedObject(this)) {
  tracker_->AddExternalReference();
}
KeepAliveValue::~KeepAliveValue() {}

Value KeepAliveValue::Get(memory::MemoryManager* memory_manager) const {
  if (memory_manager != NULL) {
    GOOGLE_CHECK(memory_manager->ReviveTrackedObject(tracker_))
        << "Tried to call Get() on a KeepAliveValue after it had been "
           "released (and was being deleted in another thread).";
  }
  return value_;
}

void KeepAliveValue::NotReachable() {
  delete this;
}

void KeepAliveValue::Accept(memory::MemoryVisitor* visitor) const {
  value_.Accept(visitor);
}

Context::Context(ContextFactory* factory)
  : factory_(factory),
    environment_(factory->NewContext()),
    set_next_called_(false),
    has_callback_(false) {}

Context::~Context() {
  factory_->ReleaseContext(environment_);
}

bool Context::Step() {
  // Make sure that calling Step() again won't work unless SetNext() is called.
  GOOGLE_CHECK(set_next_called_);
  set_next_called_ = false;
  function_.GetLogic()->Run(this);
  return set_next_called_;
}

void Context::Run() {
  while (Step()) {}
}

void Context::Accept(memory::MemoryVisitor* visitor) const {
  function_.Accept(visitor);
  parameter_.Accept(visitor);
  if (has_callback_) callback_.Accept(visitor);
}

ContextFactory::~ContextFactory() {}

}  // namespace vm
}  // namespace evlan

