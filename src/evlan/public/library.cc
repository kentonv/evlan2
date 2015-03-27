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

#include "evlan/public/library.h"

#include "evlan/common.h"
#include "evlan/proto/module.pb.h"
#include "evlan/public/evaluator.h"
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include "evlan/stringpiece.h"
#include "evlan/map-util.h"
#include "evlan/stl-util.h"

namespace evlan {

Library::~Library() {}

LibraryLoader::LibraryLoader(EvlanEvaluator* evaluator)
    : evaluator_(evaluator) {}

LibraryLoader::~LibraryLoader() {
  STLDeleteValues(&loaded_libraries_);
}

EvlanValue* LibraryLoader::Load(Library* library) {
  EvlanValue* result = LoadInternal(library);
  if (result == NULL) {
    // TODO(kenton):  Give more information?
    result = evaluator_->NewTypeError(
      "Evlan library cyclically depends on itself: " +
      library->GetLabel().as_string());
  }
  return result;
}

EvlanValue* LibraryLoader::LoadInternal(Library* library) {
  EvlanValue* const dummy = NULL;  // C++ gets confused.
  pair<LibraryMap::iterator, bool> insert_result =
    loaded_libraries_.insert(make_pair(library, dummy));

  if (!insert_result.second) {
    // The library is already in the map.
    return insert_result.first->second->Clone();
  }

  vector<Library*> dependencies;
  vector<EvlanValue*> dependency_values;

  library->GetDependencies(&dependencies);

  for (int i = 0; i < dependencies.size(); i++) {
    dependency_values.push_back(LoadInternal(dependencies[i]));
    if (dependency_values.back() == NULL) {
      // Cycle detected.  Give up.
      // (We can leave the NULL pointer in the map since that will just make
      // us fail faster if this library is loaded again.)
      STLDeleteElements(&dependency_values);
      return NULL;
    }
  }

  EvlanValue* result =
    library->Load(evaluator_, dependency_values);
  STLDeleteElements(&dependency_values);

  // We can't use the iterator from before since the map might have had stuff
  // added in the meantime.
  loaded_libraries_[library] = result->Clone();
  return result;
}

}  // namespace evlan
