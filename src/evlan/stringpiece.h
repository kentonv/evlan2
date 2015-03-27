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

#ifndef EVLAN_STRINGPIECE_H_
#define EVLAN_STRINGPIECE_H_

#include <pcre_stringpiece.h>
#include <google/protobuf/stubs/substitute.h>

namespace evlan {

using pcrecpp::StringPiece;

inline StringPiece substr(const StringPiece& str, size_t start, size_t size) {
  GOOGLE_DCHECK_LE(start + size, str.size());
  return StringPiece(str.data() + start, size);
}

}  // namespace evlan

inline bool operator==(const evlan::StringPiece& a, const evlan::StringPiece& b) {
  return a.size() == b.size() && memcmp(a.data(), b.data(), a.size()) == 0;
}

inline bool operator==(const string& a, const evlan::StringPiece& b) {
  return a.size() == b.size() && memcmp(a.data(), b.data(), a.size()) == 0;
}

inline bool operator==(const evlan::StringPiece& a, const string& b) {
  return a.size() == b.size() && memcmp(a.data(), b.data(), a.size()) == 0;
}

inline bool operator==(const char* a, const evlan::StringPiece& b) {
  return strlen(a) == b.size() && memcmp(a, b.data(), b.size()) == 0;
}

inline bool operator==(const evlan::StringPiece& a, const char* b) {
  return a.size() == strlen(b) && memcmp(a.data(), b, a.size()) == 0;
}

namespace std {
template<> struct hash<evlan::StringPiece> {
  size_t operator()(const evlan::StringPiece& str) const {
    // TODO(kenton):  Don't rely on libstdc++-private _Hash_bytes.
    return std::_Hash_bytes(str.data(), str.size(), static_cast<size_t>(0xc70f6907UL));
  }
};
}  // namespace std

#endif  // EVLAN_STRINGPIECE_H_
