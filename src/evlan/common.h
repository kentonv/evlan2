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

#ifndef EVLAN_COMMON_H_
#define EVLAN_COMMON_H_

#include "google/protobuf/stubs/common.h"

namespace evlan {

using namespace std;

using google::protobuf::int8;
using google::protobuf::int16;
using google::protobuf::int32;
using google::protobuf::int64;
using google::protobuf::uint8;
using google::protobuf::uint16;
using google::protobuf::uint32;
using google::protobuf::uint64;

using google::protobuf::kint32min;
using google::protobuf::kint32max;
using google::protobuf::kint64min;
using google::protobuf::kint64max;

using google::protobuf::scoped_ptr;
using google::protobuf::scoped_array;
using google::protobuf::implicit_cast;
using google::protobuf::down_cast;

using google::protobuf::Closure;
using google::protobuf::DoNothing;
using google::protobuf::NewCallback;
using google::protobuf::NewPermanentCallback;

using google::protobuf::Mutex;
using google::protobuf::MutexLock;
using google::protobuf::MutexLockMaybe;
using google::protobuf::ReaderMutexLock;
using google::protobuf::WriterMutexLock;

template <typename Out, typename In>
inline Out bit_cast(const In& in) {
  GOOGLE_COMPILE_ASSERT(sizeof(In) == sizeof(Out), bit_cast_size_mismatch);

  Out out;
  memcpy(&out, &in, sizeof(out));
  return out;
}

#define arraysize(A) GOOGLE_ARRAYSIZE(A)

#define DISALLOW_EVIL_CONSTRUCTORS(NAME) GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(NAME)
#define DISALLOW_COPY_AND_ASSIGN(NAME) GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(NAME)

#define VLOG(LEVEL) GOOGLE_LOG(INFO)

template <typename T>
class ThreadLocal {
 public:
  ThreadLocal() {
    GOOGLE_CHECK_EQ(pthread_key_create(&key, &Destroy), 0);
  }
  ~ThreadLocal() {
    GOOGLE_CHECK_EQ(pthread_key_delete(key), 0);
  }

  const T& get() { return *pointer(); }
  void set(const T& value) { *pointer() = value; }

  T* pointer() {
    void* ptr;

    ptr = pthread_getspecific(key);
    if (ptr == NULL) {
      ptr = new T;
      GOOGLE_CHECK_EQ(0, pthread_setspecific(key, ptr));
    }

    return reinterpret_cast<T*>(ptr);
  }

 private:
  pthread_key_t key;

  static void Destroy(void* t) {
    delete reinterpret_cast<T*>(t);
  }
};

template <typename T>
class ManualConstructor {
 public:
  ManualConstructor() : is_initialized_(false) {}
  ~ManualConstructor() {
    Destroy();
  }

  T* get() { return &value_; }
  const T* get() const { return &value_; }

  T* operator->() { return &value_; }
  const T* operator->() const { return &value_; }

  T& operator*() { return value_; }
  const T& operator*() const { return value_; }

  template <typename... Params>
  void Init(Params&&... params) {
    Destroy();
    new (&value_) T(std::forward<Params>(params)...);
    is_initialized_ = true;
  }

  void Destroy() {
    if (is_initialized_) {
      is_initialized_ = false;
      value_.~T();
    }
  }

 private:
  union {
    T value_;
  };
  bool is_initialized_;
};

}  // namespace evlan

#endif  // EVLAN_COMMON_H_
