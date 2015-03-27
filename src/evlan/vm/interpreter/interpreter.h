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

#ifndef EVLAN_VM_INTERPRETER_INTERPRETER_H__
#define EVLAN_VM_INTERPRETER_INTERPRETER_H__

#include <map>
#include <string>
#include "evlan/vm/base.h"

namespace evlan {
  class Module;
  namespace vm {
    namespace memory {
      class MemoryManager;
      class ObjectTracker;
    }
    class Context;
    class Value;
  }
}

namespace evlan {
namespace vm {
namespace interpreter {

// Interprets the given module to produce a Value.  After InterpretModule()
// returns, context->Run() must be called to finish the computation.
//
// Parameters:
//   context        A context in which this module can be executed.
//   module         The module to interpret.  This object only needs to remain
//                  valid until InterpretModule returns -- it can be destroyed
//                  before calling context->Run().
//   module_tracker If |module| is part of a TrackedObject, this is the
//                  ObjectTracker for that object.  Otherwise, this can be NULL.
//   imports        Other values which may be imported by this module.  It is
//                  an error if any name in the module's import table is not
//                  found in this map.
//   callback       A callback to call with the final result of evaluating the
//                  module.
void InterpretModule(Context* context,
                     const Module& module,
                     memory::ObjectTracker* module_tracker,
                     const map<string, Value>& imports,
                     Value callback);

// Interprets a raw code tree (e.g. from a Module) and attaches the given
// environment to form a function.  This is a slightly lower-level version of
// InterpretModule(), used particularly when unmarshalling marshalled values.
void InterpretCodeTree(Context* context,
                       const int32 code_tree[], int code_tree_size,
                       memory::ObjectTracker* code_tree_tracker,
                       Value environment_values[], int environment_size,
                       Value callback);

// Get a sub-function of the given function.  This is needed when unmarshalling
// marshalled functions.
// Parameters:
//   context        A context in which this module can be executed.
//   parent_function
//                  The function in which the desired function is nested.
//   code_tree_offset
//                  The difference between the code tree indexes of the
//                  ABSTRACTION instruction for the parent function and the
//                  ABSTRACTION instruction for the child.
//   sub_environment_values
//   sub_environment_size
//                  An array of values which should be added to
//                  parent_function's environment in order to produce the child
//                  function's environment.
//   sub_environment_is_tracked
//                  True if sub_environment_values resides in tracked memory.
//   callback       A callback to call with the final result.
void GetSubFunction(Context* context,
                    Value parent_function,
                    int code_tree_offset,
                    const Value sub_environment_values[],
                    int sub_environment_size,
                    bool sub_environment_is_tracked,
                    Value callback);

}  // namespace interpreter
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_INTERPRETER_INTERPRETER_H__
