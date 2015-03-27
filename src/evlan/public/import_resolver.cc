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

#include "evlan/public/import_resolver.h"

#include "evlan/public/evaluator.h"
#include <google/protobuf/descriptor.h>
#include "evlan/stringpiece.h"
#include "evlan/stl-util.h"

namespace evlan {

ImportResolver::~ImportResolver() {}

NullImportResolver::NullImportResolver() {}
NullImportResolver::~NullImportResolver() {}

EvlanValue* NullImportResolver::Import(EvlanEvaluator* evaluator,
                                       const StringPiece& name) {
  return NULL;
}

MappedImportResolver::MappedImportResolver() {}
MappedImportResolver::~MappedImportResolver() {
  for (ImportMap::iterator iter = import_map_.begin();
       iter != import_map_.end(); ++iter) {
    if (!iter->second.is_resolver) delete iter->second.value;
  }
}

void MappedImportResolver::Map(const StringPiece& name, EvlanValue* value) {
  ValueOrResolver* map_slot = &import_map_[name.as_string()];

  // Delete the previous value, if any.
  if (!map_slot->is_resolver) delete map_slot->value;

  *map_slot = ValueOrResolver(value->Clone());
}

void MappedImportResolver::MapPrefix(const StringPiece& prefix,
                                     ImportResolver* sub_resolver) {
  import_map_[prefix.as_string()] = ValueOrResolver(sub_resolver);
}

EvlanValue* MappedImportResolver::Import(EvlanEvaluator* evaluator,
                                         const StringPiece& name) {
  // TODO(kenton):  This is technically O(n) worst-case.  However, assuming
  //   there are no overlapping prefix mappings, and assuming all imports
  //   find a match, then it is O(lg n), because upper_bound() will always
  //   return the element immediately after the matching one in this case.
  //   If there are overlaps, it should still be reasonably fast most of the
  //   time.  If performance turns out to be a problem, we could use a trie
  //   instead.

  // Find the first mapping which comes lexically after this name.
  ImportMap::const_iterator iter = import_map_.upper_bound(name.as_string());

  // Search backwards until we find a name which is a prefix of this value.
  do {
    if (iter == import_map_.begin()) return NULL;
    --iter;
  } while (!name.starts_with(iter->first));

  if (iter->second.is_resolver) {
    // Forward to the sub-resolver.
    StringPiece sub_name(name);
    sub_name.remove_prefix(iter->first.size());
    return iter->second.resolver->Import(evaluator, sub_name);
  } else {
    // It's a value, so the name must match exactly.
    if (iter->first == name) {
      return iter->second.value->Clone();
    } else {
      return NULL;
    }
  }
}

ProtoImportResolver::ProtoImportResolver(
    const google::protobuf::DescriptorPool* pool,
    google::protobuf::MessageFactory* factory,
    Mutex* factory_mutex)
    : pool_(pool), factory_(factory), factory_mutex_(factory_mutex) {
}

ProtoImportResolver::~ProtoImportResolver() {}

// implements ImportResolver ---------------------------------------
EvlanValue* ProtoImportResolver::Import(
    EvlanEvaluator* evaluator, const StringPiece& name) {
  const google::protobuf::FileDescriptor* file = pool_->FindFileByName(name.as_string());
  if (file == NULL) {
    return NULL;
  } else {
    return evaluator->NewProtoFile(file, factory_, factory_mutex_);
  }
}

}  // namespace evlan
