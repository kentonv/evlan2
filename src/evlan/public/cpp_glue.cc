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

#include "evlan/public/cpp_glue.h"
#include <cxxabi.h>                 // for __cxa_demangle
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/strutil.h"
#include "evlan/stl-util.h"
#include "evlan/map-util.h"

namespace evlan {

CppGlue::CppGlue(EvlanEvaluator* evaluator)
  : parent_(NULL), evaluator_(evaluator) {}

CppGlue::CppGlue(CppGlue* parent, EvlanEvaluator* evaluator)
  : parent_(parent), evaluator_(evaluator) {}

CppGlue::~CppGlue() {
  STLDeleteValues(&type_map_);
}

void CppGlue::RegisterClass(const TypeInfo* type_info) {
  // First check if the glue exists in this or a parent.
  for (const CppGlue* glue = this; glue != NULL; glue = glue->parent_) {
    // Note:  The entry in type_map_ may be NULL if we're currently in the
    //   process of creating it.  In this case we still want to return to
    //   avoid infinite recursion.  So, using FindPtrOrNull() wouldn't work,
    //   since we wouldn't be able to tell the difference between the entry
    //   not being in the map at all vs. in the map but set to NULL.  Using
    //   count() is easier anyway.
    if (glue->type_map_.count(type_info) > 0) return;
  }

  // Not found.  Construct a new one.  We need to make sure to add the slot
  // to the map before we call NewClassGlue() to avoid infinite recursion.
  ClassGlue** map_slot = &type_map_[type_info];
  *map_slot = type_info->NewClassGlue(this, evaluator_);
}

EvlanValue* CppGlue::WrapObject(EvlanEvaluator* evaluator,
                                const TypeInfo* type,
                                const void* object,
                                bool adopt) const {
  for (const CppGlue* glue = this; glue != NULL; glue = glue->parent_) {
    ClassGlue* class_glue = FindPtrOrNull(glue->type_map_, type);
    if (class_glue != NULL) {
      return evaluator->NewCustomValue(
        new CustomObject(class_glue, object, adopt));
    }
  }

  string error("Tried to convert unregistered type to Evlan: ");
  error.append(type->GetName());
  GOOGLE_LOG(DFATAL) << error;
  return evaluator->NewTypeError(error);
}

string CppGlue::CppNameToEvlanName(const StringPiece& name) {
  // This function converts CapitalizedCamelCase and lower_with_underscores
  // names to nonCapitalizedCamelCase, e.g.:
  //   FooBar -> fooBar
  //   foo_bar -> fooBar

  string result;

  bool capitalize_next_letter = false;
  for (int i = 0; i < name.size(); i++) {
    if (!isalnum(name[i])) {
      // Presumably this is an underscore.  Don't use it in the Evlan name,
      // but capitalize the next letter.
      capitalize_next_letter = true;
    } else if (capitalize_next_letter) {
      result.push_back(toupper(name[i]));
      capitalize_next_letter = false;
    } else {
      result.push_back(name[i]);
    }
  }

  // Make sure the first letter is lower-case.
  if (!result.empty()) result[0] = tolower(result[0]);

  return result;
}

EvlanValue* CppGlue::ArityError(
    EvlanEvaluator* evaluator, const TypeInfo* type,
    const StringPiece& method_name, EvlanValue* parameter, int arity) {
  string description = strings::Substitute(
    "Method \"$0::$1\" takes $2 arguments, but was given $3.",
    type->GetName(), method_name.as_string(), arity, parameter->GetTupleSize());
  return evaluator->NewTypeError(description);
}

EvlanValue* CppGlue::ParameterTypeError(
    EvlanEvaluator* evaluator, const TypeInfo* type,
    const StringPiece& method_name, EvlanValue* parameter,
    const string& type_name, int index) {
  string description = strings::Substitute(
    "Parameter type mismatch for parameter $0 of method \"$1::$2\".  "
    "Evlan value could not be converted to type \"$3\": $4",
    index, type->GetName(), method_name.as_string(), type_name, parameter->DebugString(0));
  return evaluator->NewTypeError(description);
}

string CppGlue::GetTypeName(const std::type_info& type) {
  int status;
  char* malloced_name = abi::__cxa_demangle(type.name(), NULL, NULL, &status);
  string result;
  if (status == 0) {
    result = malloced_name;
  } else {
    result = type.name();
    GOOGLE_LOG(ERROR) << "Received error code " << status
               << " when trying to demangle type name: " << result;
  }
  ::free(malloced_name);
  return result;
}

// ===================================================================

CppGlue::ClassGlue::ClassGlue(const TypeInfo* type_info)
  : type_info_(type_info) {}

CppGlue::ClassGlue::~ClassGlue() {
  for (MethodMap::iterator iter = methods_.begin();
       iter != methods_.end(); ++iter) {
    delete iter->second.second;
  }
}

void CppGlue::ClassGlue::AddMethod(MethodGlue* glue) {
  methods_[glue->GetNameAtomId()] = make_pair(false, glue);
}

void CppGlue::ClassGlue::AddField(MethodGlue* glue) {
  methods_[glue->GetNameAtomId()] = make_pair(true, glue);
}

CppGlue::TypeInfo::~TypeInfo() {}

CppGlue::CustomObject::CustomObject(
    ClassGlue* class_glue, const void* object, bool adopt)
    : class_glue_(class_glue), object_(object), owned_(adopt) {}

CppGlue::CustomObject::~CustomObject() {
  if (owned_) {
    class_glue_->type_info_->Delete(object_);
  }
}

EvlanValue* CppGlue::CustomObject::Call(EvlanEvaluator* evaluator,
                                        EvlanValue* self,
                                        EvlanValue* parameter) {
  AtomId member_name;
  if (!parameter->GetAtomValue(&member_name)) {
    return evaluator->NewTypeError(
      "Member name for \"" + class_glue_->type_info_->GetName() +
      "\" must be atom.");
  }

  const pair<bool, MethodGlue*>* member =
    FindOrNull(class_glue_->methods_, member_name);
  if (member == NULL) {
    return evaluator->NewTypeError(
      "Class \"" + class_glue_->type_info_->GetName() +
      "\" has no member \"" + member_name.GetName().as_string() + "\".");
  }

  MethodGlue* method_glue = member->second;

  if (member->first) {
    // It's a field.  Call the accessor method with no parameters.
    scoped_ptr<EvlanValue> empty_tuple(
      evaluator->NewTuple(vector<EvlanValue*>()));
    return method_glue->Call(evaluator, object_, empty_tuple.get());
  } else {
    // It's a method.  Wrap it in a CustomMethod.
    return evaluator->NewCustomValue(new CustomMethod(self, this, method_glue));
  }
}

string CppGlue::CustomObject::DebugString(int depth) {
  return strings::Substitute("cppGluedObject(\"$0\")",
    class_glue_->type_info_->GetName());
}

CppGlue::CustomMethod::CustomMethod(
    EvlanValue* object_value, CustomObject* custom_object,
    MethodGlue* method_glue)
    : object_value_(object_value->Clone()),
      custom_object_(custom_object),
      method_glue_(method_glue) {}

CppGlue::CustomMethod::~CustomMethod() {}

EvlanValue* CppGlue::CustomMethod::Call(EvlanEvaluator* evaluator,
                                        EvlanValue* self,
                                        EvlanValue* parameter) {
  return method_glue_->Call(evaluator, custom_object_->object_, parameter);
}

string CppGlue::CustomMethod::DebugString(int depth) {
  return strings::Substitute("cppGluedMethod(\"$0::$1\")",
    custom_object_->class_glue_->type_info_->GetName(),
    method_glue_->GetOriginalName().as_string());
}

}  // namespace evlan
