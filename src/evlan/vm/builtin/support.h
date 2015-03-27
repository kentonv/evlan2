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
//
// The builtin types are combined with supporting code which is often
// written in Evlan.  This code assists in calling non-native support code
// from native types.

#ifndef EVLAN_VM_BUILTIN_SUPPORT_H__
#define EVLAN_VM_BUILTIN_SUPPORT_H__

#include "evlan/vm/runtime.h"

namespace evlan {
namespace vm {
namespace builtin {

// The VM typically has a single SupportCodeManager which can be obtained from
// the Context.  This object allows built-in types to find the Evlan code
// attached to them, so that they can invoke it when appropriate.
class SupportCodeManager {
 public:
  SupportCodeManager();
  ~SupportCodeManager();

  // Initializes the SupportCodeManager by extracting the support code from
  // the given Evlan object.  The object must have members called "integer",
  // "string", etc.  Each of these members is a function which takes, as its
  // parameter, a value of the corresponding type, and returns an object
  // containing that value's methods.  For example:
  //   integer = self => object of
  //     add(other) = nativeInteger.add(self, other)
  //     subtract(other) = nativeInteger.subtract(self, other)
  //     ...
  // (As of this writing, the standard support code module is located at
  // evlan/api/builtinSupport.evlan.  It may serve as a better example.)
  //
  // The given context will be used to execute several functions.  Thus, the
  // Context must not currently be in-use.  That is, you cannot simply call
  // this from within an implementation of Logic::Run() and use the context
  // passed to it.
  //
  // This method allocates several permanent objects tracked by the
  // MemoryManager.  They will not be destroyed until the MemoryManager is
  // destroyed.  After than happens, the SupportCodeManager can no longer
  // be used.
  //
  // Before Init() is called, GetSupportCode() will return placeholders for
  // the sake of bootstrapping.  Calling one of these placeholders will simply
  // produce a type error.
  void Init(Context* context, const Value& support_module);

  // Must be kept in sync with kBuiltinTypeNames and kBuiltinLogics in
  // support.cc.
  enum Type {
    INTEGER,
    DOUBLE,
    BOOLEAN,
    ATOM,
    STRING,
    ARRAY,

    PROTOBUF,
    PROTOBUF_REPEATED,
    PROTOBUF_BUILDER,
    PROTOBUF_TYPE,

    NUM_TYPES
  };

  // Get a Logic representing the support code attached to the given type.
  // This Logic may be combined with the built-in type's usual Data to form
  // a callable value representing that type.
  inline const Logic* GetSupportCode(Type type) const {
    return support_code_logics_[type];
  }

 private:
  const Logic* support_code_logics_[NUM_TYPES];
  bool initialized_;
};

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_BUILTIN_SUPPORT_H__
