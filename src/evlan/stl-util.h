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

#ifndef EVLAN_STL_UTIL_H_
#define EVLAN_STL_UTIL_H_

#include "google/protobuf/stubs/stl_util-inl.h"

namespace evlan {

using google::protobuf::STLDeleteValues;
using google::protobuf::STLDeleteElements;

template <typename T>
inline T* vector_as_array(vector<T>* v) {
  return v->empty() ? NULL : &*v->begin();
}

template <typename T>
inline const T* vector_as_array(const vector<T>* v) {
  return v->empty() ? NULL : &*v->begin();
}

}  // namespace evlan

#endif  // EVLAN_STL_UTIL_H_
