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

#ifndef EVLAN_MAP_UTIL_H_
#define EVLAN_MAP_UTIL_H_

#include "google/protobuf/stubs/map-util.h"

namespace evlan {

using google::protobuf::FindOrNull;
using google::protobuf::FindPtrOrNull;
using google::protobuf::InsertIfNotPresent;

template <class Collection, class Value>
bool InsertIfNotPresent(Collection* collection, const Value& value) {
  pair<typename Collection::iterator, bool> ret = collection->insert(value);
  return ret.second;
}

}  // namespace evlan

#endif  // EVLAN_MAP_UTIL_H_
