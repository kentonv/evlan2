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

#include "evlan/vm/memory/memory_manager.h"

#include <unordered_map>
#include "evlan/common.h"
#include <iostream>

// TODO(kenton):  Make these "flags" actually setable.

// Minimum number of attempts before an EVLAN_EXPECT_SOMETIMES is reported.
const int FLAGS_evlan_expect_sometimes_min_count = 100;

// Minimum number of attempts per success before an EVLAN_EXPECT_SOMETIMES is reported.
const int FLAGS_evlan_expect_sometimes_min_failure_rate = 100;

namespace evlan {
namespace vm {
namespace memory {

MemoryGuide::~MemoryGuide() {}
TrackedObject::~TrackedObject() {}
MemoryRoot::~MemoryRoot() {}
MemoryManager::~MemoryManager() {}
MemoryVisitor::~MemoryVisitor() {}
ObjectTracker::~ObjectTracker() {}
Backlink::~Backlink() {}

NullMemoryRoot::~NullMemoryRoot() {}

void NullMemoryRoot::Accept(MemoryVisitor* visitor) const {
  // do nothing
}

namespace {

struct Location {
  const char* file;
  int line;
  const char* expression;

  Location() {}
  Location(const char* file, int line, const char* expression)
      : file(file), line(line), expression(expression) {}

  inline bool operator==(const Location& location) const {
    // Both of the const char*'s should be string literals, and thus their
    // pointers should always be the same.  So, we can just compare the
    // pointers rather than the content.
    //
    // We could probably omit |expression| from the comparison but we
    // throw it in just in case someone places two expectations on the
    // same line.
    return file == location.file &&
           line == location.line &&
           expression == location.expression;
  }

  struct Hasher {
    inline size_t operator()(const Location& location) const {
      // TODO(kenton):  Is this a reasonable way to hash?
      return reinterpret_cast<size_t>(location.file) * 31 +
             reinterpret_cast<size_t>(location.expression) * 653 +
             location.line;
    }
  };
};

struct SuccessCount {
  int successes;
  int total;

  SuccessCount(): successes(0), total(0) {}
};

struct SometimesChecker {
  typedef unordered_map<Location, SuccessCount, Location::Hasher> CounterMap;
  CounterMap counters;
  Mutex mutex;

  ~SometimesChecker() {
    for (CounterMap::const_iterator iter = counters.begin();
         iter != counters.end(); ++iter) {
      const Location& location = iter->first;
      const SuccessCount& counter = iter->second;

      if (counter.total > FLAGS_evlan_expect_sometimes_min_count &&
          counter.total > counter.successes *
              FLAGS_evlan_expect_sometimes_min_failure_rate) {
        // GOOGLE_LOG doesn't work here because we're in a global destructor.
        cerr << __FILE__ << ":" << __LINE__
            << ":\n  " << location.file << ":" << location.line << ": "
            << location.expression << " succeeded " << counter.successes
            << " times in " << counter.total << " attempts ("
            << ((counter.successes * 200 / counter.total + 1) / 2) << "%)"
            << endl;
      }
    }
  }
} sometimes_checker_;

}  // namespace

bool ExpectSometimes(const char* file, int line, bool success,
                     const char* expression) {
  MutexLock lock(&sometimes_checker_.mutex);

  SuccessCount* counter = &sometimes_checker_.counters[
      Location(file, line, expression)];

  if (success) ++counter->successes;
  ++counter->total;

  return success;
}

}  // namespace memory
}  // namespace vm
}  // namespace evlan
