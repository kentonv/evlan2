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

#ifndef EVLAN_VM_HASHER_H_
#define EVLAN_VM_HASHER_H_

#include "evlan/common.h"
#include "evlan/common.h"
#include <openssl/sha.h>
#include "evlan/stringpiece.h"

namespace evlan {
namespace vm {

// Class used to generate secure hashes, which are used to identify values in
// the marshalling protocol.  See ValueId in runtime.h.
//
// Currently the hash function is SHA-1, but it may change in the future.
class Hasher {
 public:
  Hasher();
  ~Hasher();

  // Add bytes to the data to be hashed.
  void AddBytes(StringPiece bytes);

  // Equivalent to AddValue(bytes.size()) followed by AddBytes(bytes).  This
  // is useful e.g. if hashing "foo" followed by "barbaz" is not supposed to
  // be the same as "foobar" followed by "baz" -- if you used AddBytes(),
  // the hash would come out the same, but using AddString() they will be
  // different.
  void AddString(StringPiece bytes);

  // Adds the raw bytes from the given value to the hash.
  template <typename Value>
  void AddValue(const Value& value);

  // Adds the raw bytes from the given array to the hash.
  template <typename Value>
  void AddArray(const Value value[], int size);

  // Compute the hash for all data passed to AddBytes() and write it to *output,
  // which must be an array of kHashSize bytes.  In order to preserve
  // implementation freedom, it is currently illegal to call AddBytes() again
  // after Finish(), or to call Finish() twice.
  void Finish(void* output);

  // Remember:  This number can change!  We might change hash algorithms, or
  // we might decide not to use the entire hash in order to save space.
  static const int kHashSize = SHA_DIGEST_LENGTH;

 private:
  SHA_CTX sha1_context_;
  bool finished_;
};

// ===================================================================
// inline implementation

inline Hasher::Hasher() : finished_(false) {
  SHA1_Init(&sha1_context_);
}
inline Hasher::~Hasher() {}

inline void Hasher::AddBytes(StringPiece bytes) {
  GOOGLE_DCHECK(!finished_);
  SHA1_Update(&sha1_context_, bytes.data(), bytes.size());
}

inline void Hasher::AddString(StringPiece bytes) {
  AddValue(bytes.size());
  AddBytes(bytes);
}

template <typename Value>
inline void Hasher::AddValue(const Value& value) {
  // TODO(kenton):  Enforce somehow that this is a POD type.
  AddBytes(StringPiece(reinterpret_cast<const char*>(&value), sizeof(value)));
}

template <typename Value>
inline void Hasher::AddArray(const Value array[], int size) {
  // TODO(kenton):  Enforce somehow that this is a POD type.
  AddBytes(StringPiece(reinterpret_cast<const char*>(array),
                       sizeof(Value) * size));
}

inline void Hasher::Finish(void* output) {
  GOOGLE_DCHECK(!finished_);
  finished_ = true;
  SHA1_Final(reinterpret_cast<unsigned char*>(output), &sha1_context_);
}

}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_HASHER_H_
