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
// How it works:
//
// First, if you haven't read module.proto, go do that now.
//
// So, the module contains a code tree.  When the interpreter loads that, it
// creates a tree of Evlan values matching that code tree.  Each value in the
// tree is a function which takes, as its parameter, an environment, and
// returns the result of evaluating the tree in that environment.
//
// An environment is simply a list of variable bindings that exist in a
// particular context.  This includes both the constant table (always at the
// end of the list) and bindings for function parameters for all "abstraction"
// nodes that are ancestors of the node being evaluated.  For example, take
// this code:
//
//   # Outside of any functions, the environment only contains the module's
//   # constant table, which in this case contains only the number 2.  So,
//   # the environment list is just {2}.
//   x =>
//     # At this location, the environment is {x, 2}, where x is the value
//     # passed in when the function was called.
//     y =>
//       # Here, it is {x, y, 2}.
//       x + y * 2
//
// Thus, evaluating each node is simple:
//   Abstraction:  Return a function which, when called, adds the parameter
//     to the top of the environment and then uses that environment to evaluate
//     the abstraction's body node.
//   Application:  Evaluate the function and parameter nodes with the same
//     environment, then apply the function to the parameter.
//   Variable:  The variable's index is a direct index into the environment
//     list.  Just look it up.
//
// Once we've constructed the entire code tree, we just have to call the root
// of the tree with an environment that contains only the constant table.

#include <stack>
#include <vector>

#include "evlan/vm/interpreter/interpreter.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/vm/runtime.h"
#include "evlan/vm/hasher.h"
#include "evlan/vm/builtin/array.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/boolean.h"
#include "evlan/vm/builtin/atom.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/type_error.h"
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/proto/module.pb.h"
#include "evlan/strutil.h"
#include "evlan/map-util.h"
#include "evlan/stl-util.h"
#include <google/protobuf/io/printer.h>

namespace evlan {
namespace vm {
namespace interpreter {

namespace {

// ===================================================================
// CodeTree

// A simple struct storing a module's code tree array.
struct CodeTree {
  int size;
  int32 instructions[];

  int Accept(memory::MemoryVisitor* visitor) const {
    return sizeof(*this) + size * sizeof(instructions[0]);
  }

  // Find the index where a branch of the code tree ends.
  int FindEndOfBranch(int branch_start) const {
    int expected_branches = 1;
    int pos = branch_start;
    for (; pos < size && expected_branches > 0; pos++) {
      switch (instructions[pos]) {
        case Module::APPLICATION:
          ++expected_branches;
          break;

        case Module::ABSTRACTION:
          // No change in expected branches.
          break;

        default:  // variable
          --expected_branches;
          break;
      }
    }

    return pos;
  }
};

// ===================================================================
// Environment

struct FunctionData;

// Represents an environment, which is just a list of values (see above).
//
// Each "Environment" struct can actually contain multiple values, which allows
// us to put the entire constant table into a single Environment struct for
// faster indexing.
//
// Note that the last element in the environment is always an array of compiled
// code tree nodes corresponding to every instruction in the code tree.
struct Environment {
  // Pointer to the rest of the environment list, or NULL if this is the last
  // node in the list.
  const Environment* next;

  // The FunctionData for the function which is the parent of the current
  // environment.  In other words, parent_function_data->environment == next.
  // NULL for the outermost frame.
  const FunctionData* parent_function_data;

  // The CodeTree for the module.
  // TODO(kenton):  We actually only need this on the *last* frame, i.e. when
  //   next == NULL.  Therefore we could set up a union containing code_tree
  //   and next, and use the lower bit of both pointers to indicate which one
  //   is used.  This would save a few bytes, which could be useful since
  //   lots of Environment frames are allocated currently.
  const CodeTree* module_code_tree;

  int size;           // Size of the |values| array.
  Value values[0];

  int Accept(memory::MemoryVisitor* visitor) const {
    if (next != NULL) {
      visitor->VisitPointer(&next);
      visitor->VisitPointer(&parent_function_data);
    }
    visitor->VisitPointer(&module_code_tree);
    for (int i = 0; i < size; i++) {
      values[i].Accept(visitor);
    }

    return sizeof(*this) + size * sizeof(values[0]);
  }

  void ToVector(vector<Value>* output,
                const FunctionData** outer_function_data) const {
    const Environment* frame;
    const Environment* previous_frame = NULL;
    for (frame = this; frame->next != NULL; frame = frame->next) {
      int size;
      size = frame->size;
      output->insert(output->end(), frame->values, frame->values + size);
      previous_frame = frame;
    }

    if (outer_function_data == NULL) {
      // Caller wants the whole thing in the Vector, except for the last element
      // which is the array of compiled code tree nodes.
      output->insert(output->end(), frame->values,
                     frame->values + frame->size - 1);
    } else {
      // Caller does not want the last environment's values included, but
      // instead wants a pointer to them.
      *outer_function_data = previous_frame->parent_function_data;
    }
  }

  // Tries to withdraw this Environment from the heap.  If successful, tries to
  // withdraw the parent as well, and so on, until a withdraw fails.
  // Additionally, if the Environment has only one value and it is a tuple --
  // common in the case of multi-parameter function calls -- that tuple is
  // withdrawn as well, unless dont_withdraw_value points at the value in which
  // case it is skipped.
  bool TryWithdrawSelf(memory::MemoryManager* memory_manager,
                       const Value* dont_withdraw_value = NULL) const {
    if (memory_manager->TryWithdrawObjectWithArray(this, values, size)) {
      if (size == 1 && builtin::ArrayUtils::IsTuple(values[0]) &&
          &values[0] != dont_withdraw_value) {
        EVLAN_EXPECT_SOMETIMES(
            builtin::ArrayUtils::TryWithdraw(memory_manager, values[0]));
      }
      if (next != NULL) {
        EVLAN_EXPECT_SOMETIMES(
            next->TryWithdrawSelf(memory_manager, dont_withdraw_value));
      }
      return true;
    } else {
      return false;
    }
  }
};

// To evaluate a code tree node, the node must be invoked as a function with
// the environment as a parameter.  Therefore, we need to be able to represent
// an environment as a Value, so we need an implementation of Logic to use.
// The corresponding Data is just a pointer to an Environment.
class EnvironmentLogic : public Logic {
 public:
  static const EnvironmentLogic kSingleton;

  EnvironmentLogic() {}

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<Environment>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("environment(");
    const Environment* environment = data.As<Environment>();
    bool first = true;
    while (environment != NULL) {
      // Don't print last element of environment.
      int size = environment->next == NULL ? environment->size - 1 :
                                             environment->size;
      for (int i = 0; i < size; i++) {
        if (first) {
          first = false;
        } else {
          printer->Print(", ");
        }
        if (depth > 0) {
          environment->values[i].DebugPrint(depth - 1, printer);
        } else {
          printer->Print("...");
        }
      }
      environment = environment->next;
    }
    printer->Print(")");
  }

  void Run(Context* context) const {
    context->Return(
      builtin::TypeErrorUtils::MakeError(context,
        "Environment cannot be called as a function."));
  }
};

const EnvironmentLogic EnvironmentLogic::kSingleton;

// ===================================================================
// Constant
//
// A code tree node that always returns a constant value.

class ConstantLogic : public Logic {
 public:
  static const ConstantLogic kSingleton;

  ConstantLogic() {}

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<Value>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("constant(");
    if (depth == 0) {
      printer->Print("...");
    } else {
      data.As<Value>()->DebugPrint(depth - 1, printer);
    }
    printer->Print(")");
  }

  void Run(Context* context) const {
    // We may be able to withdraw the environment if this is the last
    // instruction to be executed in this function.
    EVLAN_EXPECT_SOMETIMES(
        context->GetParameter().GetData().As<Environment>()
            ->TryWithdrawSelf(context->GetMemoryManager()));

    context->Return(*context->GetData().As<Value>());
  }
};

const ConstantLogic ConstantLogic::kSingleton;

bool IsConstant(Value value) {
  return value.GetLogic() == &ConstantLogic::kSingleton;
}

// ===================================================================
// Function
//
// This is the kind of value returned when an abstraction node is evaluated.
// It combines the abstraction's body with the environment, which is still
// missing the binding for the abstraction's own parameter.  So, when the
// Function is called, it adds its parameter to the environment and then
// uses that to evaluate the abstraction's body.

struct FunctionData {
  // Environment, containing everything except the function's own parameter.
  const Environment* environment;

  // Code tree for the body of the function.
  Value body_code;

  // Index of the ABSTRACTION instruction defining this function within the
  // module's code tree.
  int code_tree_index;

  // Pointer to the module's code tree.
  // TODO(kenton):  Maybe we should actually attach each outer function's
  //   sub-tree directly to the FunctionData?
  const CodeTree* module_code_tree;

  int Accept(memory::MemoryVisitor* visitor) const {
    visitor->VisitPointer(&environment);
    body_code.Accept(visitor);
    visitor->VisitPointer(&module_code_tree);
    return sizeof(*this);
  }
};

class FunctionLogic : public Logic {
 public:
  static const FunctionLogic kSingleton;

  FunctionLogic() {}

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<FunctionData>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    const FunctionData* function_data = data.As<FunctionData>();

    printer->Print("function(");
    if (depth > 0) {
      function_data->body_code.DebugPrint(depth - 1, printer);
    } else {
      printer->Print("...");
    }
    // TODO(kenton):  Print some representation of the environment?  Currently
    //   it's often way too big.
    // printer->Print(", ");
    // EnvironmentLogic::kSingleton.DebugPrint(
    //   Data(function_data->environment), depth, printer);
    printer->Print(")");
  }

  void Run(Context* context) const {
    // Add the parameter to the top of the environment.
    Environment* new_environment =
      context->AllocateObjectWithArray<Environment, Value>(1);
    const FunctionData* function_data = context->GetData().As<FunctionData>();
    new_environment->size = 1;
    new_environment->values[0] = context->GetParameter();
    new_environment->next = function_data->environment;
    new_environment->parent_function_data = function_data;
    new_environment->module_code_tree = new_environment->next->module_code_tree;

    // Invoke the body code with this environment.
    context->SetNext(context->GetData().As<FunctionData>()->body_code,
                     Value(&EnvironmentLogic::kSingleton,
                           Data(new_environment)),
                     context->GetCallback());
  }

  void GetId(Data data, ValueId* output) const {
    const FunctionData* function_data = data.As<FunctionData>();

    // TODO(kenton):  TEMPORARY HACK DO NOT SUBMIT
    static ThreadLocal<unordered_map<const FunctionData*, ValueId> > known_ids;
    static ThreadLocal<bool> is_running;

    bool is_top = !is_running.get();
    if (is_top) {
      is_running.set(true);
    }

    if (known_ids.pointer()->count(function_data) > 0) {
      *output = (*known_ids.pointer())[function_data];
      return;
    }
    // END HACK (but there is more below)

    // Figure out the size of this function's sub-tree.
    int start = function_data->code_tree_index;
    int end = function_data->module_code_tree->FindEndOfBranch(start);

    Hasher hasher;
    hasher.AddValue(end - start);
    hasher.AddArray(function_data->module_code_tree->instructions + start,
                    end - start);

    // Also hash the environment.
    ValueId sub_id;
    for (const Environment* environment = function_data->environment;
         environment != NULL; environment = environment->next) {
      // We actually want to skip the very last element of the environment
      // because it's just an array of values corresponding to the code tree.
      int size = environment->next == NULL ? environment->size - 1 :
                                             environment->size;

      hasher.AddValue(size);
      for (int i = 0; i < size; i++) {
        environment->values[i].GetId(&sub_id);
        StringPiece bytes = sub_id.GetBytes();
        hasher.AddValue(bytes.size());
        hasher.AddBytes(bytes);
      }
    }
    hasher.AddValue(-1);

    output->SetBytesFromHasher('>', &hasher);

    // TODO(kenton):  TEMPORARY HACK DO NOT SUBMIT
    (*known_ids.pointer())[function_data] = *output;
    if (is_top) {
      is_running.set(false);
      known_ids.pointer()->clear();
    }
    // END HACK
  }

  int Marshal(Data data, distribute::Marshaller* marshaller,
              const ValueId* known_id = NULL) const {
    const FunctionData* function_data = data.As<FunctionData>();

    vector<Value> environment_values;

    if (function_data->environment->parent_function_data == NULL) {
      // This is an outer function.

      // We must marshal the entire environment.
      function_data->environment->ToVector(&environment_values, NULL);

      // Compute the ValueId if we don't already know it.
      ValueId computed_id;
      if (known_id == NULL) {
        GetId(data, &computed_id);
        known_id = &computed_id;
      }

      int result = marshaller->TryAvoidMarshalling(
          *known_id, Value(this, data),
          distribute::Marshaller::DONT_MARSHAL_UNLESS_REQUIRED);
      if (result != distribute::Marshaller::kInvalidIndex) {
        return result;
      }

      int start = function_data->code_tree_index;
      int end = function_data->module_code_tree->FindEndOfBranch(start);

      result = marshaller->AddFunction(
          *known_id, &environment_values[0], environment_values.size(),
          function_data->module_code_tree->instructions + start, end - start);
      marshaller->RecordForDupElimination(Value(this, data), result);
      return result;

    } else {
      const FunctionData* parent_function_data;
      function_data->environment->ToVector(
          &environment_values, &parent_function_data);

      // TODO(kenton):  Decide when to compute the function's ID and try to
      //   avoid sending multiple times.  We currently do this only for outer
      //   functions.
      int result = marshaller->TryAvoidMarshalling(
          ValueId(), Value(this, data),
          distribute::Marshaller::ELIMINATE_DUPS);
      if (result != distribute::Marshaller::kInvalidIndex) {
        return result;
      }

      int code_tree_offset = function_data->code_tree_index -
                             parent_function_data->code_tree_index;

      // We always marshal the function as a child of the overall module.
      // TODO(kenton):  In some cases it might make sense to marshal the function
      //   as a child of another inner function, which is itself a child of
      //   something else.  Hard to detect when this makes sense, though.
      result = marshaller->AddChildFunction(
          ValueId(), &environment_values[0], environment_values.size(),
          Value(&FunctionLogic::kSingleton, Data(parent_function_data)),
          code_tree_offset);
      marshaller->RecordForDupElimination(Value(this, data), result);
      return result;
    }
  }
};

const FunctionLogic FunctionLogic::kSingleton;

// ===================================================================
// Variable
//
// This represents a code tree node of type VARIABLE.

// Logic for a Variable.  The corresponding Data is just an integer.
class VariableLogic : public Logic {
 public:
  static const VariableLogic kSingleton;

  VariableLogic() {}

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    // The data is just an integer.
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("variable($value$)",
                   "value", SimpleItoa(data.AsInteger()));
  }

  void Run(Context* context) const {
    if (context->GetParameter().GetLogic() != &EnvironmentLogic::kSingleton) {
      context->Return(
        builtin::TypeErrorUtils::MakeError(context,
          "Parameter to code tree node must be environment."));
      return;
    }

    int index = context->GetData().AsInteger();

    const Environment* top_environment =
      context->GetParameter().GetData().As<Environment>();

    // Traverse the environment list to find the |index|th element.
    const Environment* environment = top_environment;
    while (environment != NULL) {
      if (index < environment->size) {
        // Found it.

        // We may be able to withdraw the environment if this is the last
        // instruction to be executed in this function.  Of course, we should
        // not withdraw the environment's associated value if we are about to
        // return that value.
        EVLAN_EXPECT_SOMETIMES(top_environment->TryWithdrawSelf(
            context->GetMemoryManager(), &environment->values[index]));

        context->Return(environment->values[index]);
        return;
      }
      index -= environment->size;
      environment = environment->next;
    }

    // We ran off the end of the list.
    //
    // Note:  Validation done elsewhere should prevent us from ever getting
    //   here.
    context->Return(
      builtin::TypeErrorUtils::MakeError(context,
        "Variable index past end of environment list."));
    return;
  }
};

const VariableLogic VariableLogic::kSingleton;

inline Value MakeVariable(int index) {
  return Value(&VariableLogic::kSingleton, Data(index));
}

// ===================================================================
// Application
//
// This represents a code tree node of type APPLICATION.
//
// There are actually three separate Logic and corresponding Data types
// here:  the Application type itself and two callbacks that it uses during
// its execution (since it must invoke two sub-trees).
//
// The function and parameter sub-expressions could be evaluated in any
// order.  In this implementation, we have chosen to evaluate the parameter
// first.

struct ApplicationData {
  Value function_code;
  Value parameter_code;

  int Accept(memory::MemoryVisitor* visitor) const {
    function_code.Accept(visitor);
    parameter_code.Accept(visitor);
    return sizeof(*this);
  }
};

class ApplicationLogic : public Logic {
 public:
  static const ApplicationLogic kSingleton;

  ApplicationLogic() {}

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<ApplicationData>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("application(");
    if (depth > 0) {
      data.As<ApplicationData>()->function_code.DebugPrint(depth - 1, printer);
      printer->Print(", ");
      data.As<ApplicationData>()->parameter_code.DebugPrint(depth - 1, printer);
    } else {
      printer->Print("..., ...");
    }
    printer->Print(")");
  }

  // Defined later, because it depends on ApplicationCallback1.
  void Run(Context* context) const;
};

const ApplicationLogic ApplicationLogic::kSingleton;

// -------------------------------------------------------------------
// First callback -- called after evaluation of the parameter expression.

struct ApplicationCallback1Data {
  // Environment in which function_code shall be evaluated.
  const Environment* environment;

  // The code tree for the function, which we must evaluate next.
  Value function_code;

  // The final callback of the application.
  Value callback;

  int Accept(memory::MemoryVisitor* visitor) const {
    visitor->VisitPointer(&environment);
    function_code.Accept(visitor);
    callback.Accept(visitor);
    return sizeof(*this);
  }
};

class ApplicationCallback1Logic : public Logic {
 public:
  static const ApplicationCallback1Logic kSingleton;

  ApplicationCallback1Logic() {}

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<ApplicationCallback1Data>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("applicationCallback1(");
    if (depth > 0) {
      data.As<ApplicationCallback1Data>()->function_code.DebugPrint(
        depth - 1, printer);
    } else {
      printer->Print("...");
    }
    printer->Print(")");
  }

  // Defined later, because it depends on ApplicationCallback2.
  void Run(Context* context) const;
};

const ApplicationCallback1Logic ApplicationCallback1Logic::kSingleton;

// -------------------------------------------------------------------
// Second callback -- called after evaluation of the function expression.

struct ApplicationCallback2Data {
  // Environment in which application was evaluated.  We only hold on to this
  // pointer so that we can withdraw it after withdrawing the
  // ApplicationCallback2Data.
  // TODO(kenton):  Another approach would be to shuffle the environment
  //   frame in ApplicationCallback1Logic so that it is allocated after
  //   the ApplicationCallback2Data.  Then, the environment would be
  //   withdrawn when evaluating the function code tree, before reaching
  //   ApplicationCallback2Logic.  The extra shuffling would add complexity,
  //   though.
  const Environment* environment;

  // The result of evaluating the parameter expression.  We must pass this
  // value to the function to get the final result of the application.
  Value parameter;

  // The final callback of the application.
  Value callback;

  int Accept(memory::MemoryVisitor* visitor) const {
    visitor->VisitPointer(&environment);
    parameter.Accept(visitor);
    callback.Accept(visitor);
    return sizeof(*this);
  }
};

class ApplicationCallback2Logic : public Logic {
 public:
  static const ApplicationCallback2Logic kSingleton;

  ApplicationCallback2Logic() {}

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<ApplicationCallback2Data>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("applicationCallback2(");
    if (depth > 0) {
      data.As<ApplicationCallback2Data>()->parameter.DebugPrint(
        depth - 1, printer);
    } else {
      printer->Print("...");
    }
    printer->Print(")");
  }

  // Defined later, for consistency.
  void Run(Context* context) const;
};

const ApplicationCallback2Logic ApplicationCallback2Logic::kSingleton;

// -------------------------------------------------------------------

void ApplicationLogic::Run(Context* context) const {
  if (context->GetParameter().GetLogic() != &EnvironmentLogic::kSingleton) {
    context->Return(
      builtin::TypeErrorUtils::MakeError(context,
        "Parameter to code tree node must be environment."));
    return;
  }

  // Save the environment, function_code, and callback off to the side for
  // later.
  ApplicationCallback1Data* callback1_data =
    context->Allocate<ApplicationCallback1Data>();

  callback1_data->environment =
    context->GetParameter().GetData().As<Environment>();
  callback1_data->function_code =
    context->GetData().As<ApplicationData>()->function_code;
  callback1_data->callback = context->GetCallback();

  // Compute the parameter.
  context->SetNext(
    context->GetData().As<ApplicationData>()->parameter_code,
    context->GetParameter(),
    Value(&ApplicationCallback1Logic::kSingleton, Data(callback1_data)));
}

bool IsAbstraction(Value code);

// See comments on definition.
void FillInFunctionData(Data abstraction_data,
                        const Environment* environment,
                        FunctionData* function_data);

// When ApplicationCallback1Logic::Run() is called, we want to withdraw the
// ApplicationCallback1Data.  If the function being called takes a single
// parameter (not a tuple), this is no problem -- unless the parameter was
// freshly-allocated on the heap, in which case withdrawing is simply
// impossible anyway.
//
// On the other hand, if the function takes a tuple, it is very likely that
// the tuple itself is freshly-allocated on the heap.  We would like to be
// able to withdraw in this case too,  because it is common.  In this case
// the heap probobly looks like:
//   (top)
//   ArrayData of parameter tuple
//   Parameter tuple elements
//   ApplicationCallback1Data
//   (...)
//
// In order to be able to withdraw the ApplicationCallback1Data, we need to
// first withdraw the parameter tuple.  But of course we aren't done with the
// parameter yet, so we'll need to store it off to the side temporarily.
//
// ApplicationCallback1MemoryRoot encapsulates this.  In its constructor, it
// tries to widthdraw both the parameter and the ApplicationCallback1Data,
// copying them off to the side.  If the parameter tuple was successfully
// withdrawn, then a new parameter tuple with the same contents is constructed
// on the heap after the callback data is withdrawn.
class ApplicationCallback1MemoryRoot : public memory::MemoryRoot {
 public:
  ApplicationCallback1MemoryRoot(memory::MemoryManager* memory_manager,
                                 const Value& parameter,
                                 const ApplicationCallback1Data* callback_data)
      : callback1_data_(*callback_data) {
    if (builtin::ArrayUtils::IsTuple(parameter) &&
        builtin::ArrayUtils::GetArraySize(parameter) <=
          arraysize(parameter_elements_) &&
        EVLAN_EXPECT_SOMETIMES(
          builtin::ArrayUtils::TryWithdraw(memory_manager, parameter))) {
      // Copy the tuple to our local space.
      size_ = builtin::ArrayUtils::GetArraySize(parameter);
      for (int i = 0; i < size_; i++) {
        parameter_elements_[i] = builtin::ArrayUtils::GetElement(parameter, i);
      }

      // Withdraw the callback data.
      EVLAN_EXPECT_SOMETIMES(memory_manager->TryWithdraw(callback_data));

      // Rebuilt the tuple.
      if (size_ == 0) {
        parameter_elements_[0] = builtin::ArrayUtils::EmptyTuple();
        size_ = -1;
      } else {
        Value* elements = memory_manager->AllocateArray<Value>(size_, this);
        memcpy(elements, parameter_elements_, sizeof(Value) * size_);
        parameter_elements_[0] = builtin::ArrayUtils::ArrayToTuple(
            builtin::ArrayUtils::AliasArray(
                memory_manager, this, size_, elements));
        size_ = -1;
      }
    } else {
      size_ = -1;
      parameter_elements_[0] = parameter;

      // The parameter may not be a tuple at all, and may point to an object
      // that was allocated some time ago.  In that case, we can still
      // withdraw the callback data.
      EVLAN_EXPECT_SOMETIMES(memory_manager->TryWithdraw(callback_data));
    }
  }

  inline const Value& GetParameter() {
    return parameter_elements_[0];
  }

  inline const Environment* GetEnvironment() {
    return callback1_data_.environment;
  }

  inline const Value& GetFunctionCode() {
    return callback1_data_.function_code;
  }

  inline const Value& GetFinalCallback() {
    return callback1_data_.callback;
  }

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    callback1_data_.Accept(visitor);
    if (size_ == -1) {
      parameter_elements_[0].Accept(visitor);
    } else {
      for (int i = 0; i < size_; i++) {
        parameter_elements_[i].Accept(visitor);
      }
    }
  }

 private:
  ApplicationCallback1Data callback1_data_;

  // If size_ >= 0, then the parameter was a tuple of this size and
  // parameter_elements_ are the elements of that tuple.  If size_ == -1, then
  // parameter_elements_[0] is the whole parameter.  size_ is only >= 0
  // briefly during the constructor while we are shuffling values.
  int size_;

  // We only apply this optimization for tuples up to the size of this array.
  // The size can be increased to cover bigger tuples.
  Value parameter_elements_[4];
};

struct FunctionDataAndEnvironment {
  FunctionData function_data;
  Environment environment;
};

void ApplicationCallback1Logic::Run(Context* context) const {
  // context->GetParameter() is the parameter to which we are applying the
  // function.  We still have to compute the function itself, so save the
  // parameter off to the side for now.

  ApplicationCallback1MemoryRoot memory_root(
      context->GetMemoryManager(),
      context->GetParameter(),
      context->GetData().As<ApplicationCallback1Data>());

  if (IsAbstraction(memory_root.GetFunctionCode())) {
    // The function code just constructs an abstraction.  Call it directly.
    FunctionDataAndEnvironment* new_function_data_and_environment =
        context->GetMemoryManager()->AllocateObjectWithArray<
            FunctionDataAndEnvironment, Value>(1, &memory_root);
    Environment* new_environment =
        &new_function_data_and_environment->environment;

    FillInFunctionData(memory_root.GetFunctionCode().GetData(),
                       memory_root.GetEnvironment(),
                       &new_function_data_and_environment->function_data);

    new_environment->size = 1;
    new_environment->values[0] = memory_root.GetParameter();
    new_environment->next = memory_root.GetEnvironment();
    new_environment->parent_function_data =
        &new_function_data_and_environment->function_data;
    new_environment->module_code_tree = new_environment->next->module_code_tree;

    // Invoke the body code with this environment.
    context->SetNext(new_function_data_and_environment->function_data.body_code,
        Value(&EnvironmentLogic::kSingleton, Data(new_environment)),
        memory_root.GetFinalCallback());
  } else {
    // The function code is *not* simply an abstraction.
    ApplicationCallback2Data* callback2_data = context->GetMemoryManager()
        ->Allocate<ApplicationCallback2Data>(&memory_root);

    callback2_data->environment = memory_root.GetEnvironment();
    callback2_data->parameter = memory_root.GetParameter();
    callback2_data->callback = memory_root.GetFinalCallback();

    // Compute the function.
    context->SetNext(memory_root.GetFunctionCode(),
                     Value(&EnvironmentLogic::kSingleton,
                           Data(callback2_data->environment)),
                     Value(&ApplicationCallback2Logic::kSingleton,
                           Data(callback2_data)));
  }
}

void ApplicationCallback2Logic::Run(Context* context) const {
  // Try to withdraw the callback data.
  const ApplicationCallback2Data* callback2_data =
    context->GetData().As<ApplicationCallback2Data>();
  if (EVLAN_EXPECT_SOMETIMES(
        context->GetMemoryManager()->TryWithdraw(callback2_data))) {
    // Cool, that worked.  Try to withdraw the environment frame too, which
    // may work if this is the last instruction of the function.
    EVLAN_EXPECT_SOMETIMES(callback2_data->environment->TryWithdrawSelf(
        context->GetMemoryManager()));
  }

  // We now have the function and the parameter, so we can go ahead and
  // call it, and have it call our final callback directly with its result.
  // (This achieves tail recursion.)
  context->SetNext(context->GetParameter(),
                   callback2_data->parameter,
                   callback2_data->callback);
}

// ===================================================================
// Abstraction
//
// This represents a code tree node of type ABSTRACTION.

struct AbstractionData {
  // Code tree representing the function body.
  Value body_code;

  // Index of the abstraction instruction in the module's code tree.
  int code_tree_index;

  // Pointer to the module's code tree.
  const CodeTree* module_code_tree;

  int Accept(memory::MemoryVisitor* visitor) const {
    body_code.Accept(visitor);
    visitor->VisitPointer(&module_code_tree);
    return sizeof(*this);
  }
};

class AbstractionLogic : public Logic {
 public:
  static const AbstractionLogic kSingleton;

  AbstractionLogic() {}

  // implements Logic ------------------------------------------------

  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    data->AcceptAs<AbstractionData>(visitor);
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("abstraction(");
    if (depth > 0) {
      data.As<AbstractionData>()->body_code.DebugPrint(depth - 1, printer);
    } else {
      printer->Print("...");
    }
    printer->Print(")");
  }

  void Run(Context* context) const {
    if (context->GetParameter().GetLogic() != &EnvironmentLogic::kSingleton) {
      context->Return(
        builtin::TypeErrorUtils::MakeError(context,
          "Parameter to code tree node must be environment."));
      return;
    }

    // Just bind the function body code to the current environment.
    FunctionData* function_data = context->Allocate<FunctionData>();
    FillInFunctionData(context->GetData(),
                       context->GetParameter().GetData().As<Environment>(),
                       function_data);

    context->Return(Value(&FunctionLogic::kSingleton, Data(function_data)));
  }
};

const AbstractionLogic AbstractionLogic::kSingleton;

inline bool IsAbstraction(Value code) {
  return code.GetLogic() == &AbstractionLogic::kSingleton;
}

// Bind the code for a function together with an environment in order to
// fill in a FunctionData.  That can then be combined with
// FunctionLogic::kSingleton to construct a callable Value representing
// the function.
void FillInFunctionData(Data abstraction_data,
                        const Environment* environment,
                        FunctionData* function_data) {
  const AbstractionData* real_abstraction_data =
      abstraction_data.As<AbstractionData>();

  function_data->environment = environment;
  function_data->body_code = real_abstraction_data->body_code;
  function_data->code_tree_index = real_abstraction_data->code_tree_index;
  function_data->module_code_tree = real_abstraction_data->module_code_tree;
}

// ===================================================================
// The InterpretModule() and InterpretCodeTree() functions

// A MemoryRoot implementation for short-term use during InterpretModule(),
// just to keep track of a couple things while allocating some other things.
struct InterpretModuleMemoryRoot : public memory::MemoryRoot {
  // The ObjectTracker tracking the Module object that is being interpreted.
  // This may be NULL if the Module is not tracked.
  memory::ObjectTracker* module_tracker;

  // Pointers to various local variables within InterpretModule().  See
  // the function itself for comments on what these are.
  const Value* callback;
  vector<Value>* constant_table_values;
  const map<string, Value>* imports;

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    if (module_tracker != NULL) {
      visitor->VisitTrackedObject(module_tracker);
    }
    callback->Accept(visitor);
    for (int i = 0; i < constant_table_values->size(); i++) {
      constant_table_values->at(i).Accept(visitor);
    }
    for (map<string, Value>::const_iterator iter = imports->begin();
         iter != imports->end(); ++iter) {
      iter->second.Accept(visitor);
    }
  }
};

// A MemoryRoot which tracks one additional Value on top of some other
// MemoryRoot.
struct ExtraValueMemoryRoot : public memory::MemoryRoot {
  Value value;
  memory::MemoryRoot* parent;

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    value.Accept(visitor);
    parent->Accept(visitor);
  }
};

class InterpretCodeTreeStateRoot;
void ContinueInterpretingCodeTree(Context* context,
                                  InterpretCodeTreeStateRoot* state);

// Class which encapsulates the process of interpreting the code tree.  This
// is a MemoryRoot to make it easy to allocate things during the process.
// This class also helps us keep track of the interpreter state while executing
// small pieces of code, e.g. to evaluate constant expressions.
class InterpretCodeTreeStateRoot : public memory::MemoryRoot {
 public:
  InterpretCodeTreeStateRoot(const Value* environment_values,
                             int environment_size,
                             const CodeTree* code_tree,
                             Value final_callback) {
    state_.environment_values = environment_values;
    state_.environment_size = environment_size;
    state_.code_tree = code_tree;
    state_.code_tree_depths = NULL;
    state_.current_index = code_tree->size - 1;
    state_.code_tree_values = NULL;
    state_.code_tree_node_stack = NULL;
    state_.final_callback = final_callback;
  }

  // Build state_.code_tree_depths.
  bool BuildCodeTreeDepths(memory::MemoryManager* memory_manager) {
    state_.code_tree_depths = memory_manager->AllocateArray<int>(
        state_.code_tree->size, this);

    stack<int> depth_stack;
    depth_stack.push(0);
    for (int i = 0; i < state_.code_tree->size; i++) {
      if (depth_stack.empty()) return false;

      state_.code_tree_depths[i] = depth_stack.top();
      switch (state_.code_tree->instructions[i]) {
        case Module::APPLICATION:
          depth_stack.push(depth_stack.top());
          break;
        case Module::ABSTRACTION:
          ++depth_stack.top();
          break;
        default:   // variable
          depth_stack.pop();
          break;
      }
    }

    return depth_stack.empty();
  }

  // Are we done interpreting the module?
  inline bool IsDone() {
    return state_.current_index < 0;
  }

  // Getters for state components.
  inline const CodeTree* GetCodeTree() { return state_.code_tree; }
  inline int GetCodeTreeSize() { return state_.code_tree->size; }
  inline int GetCurrentIndex() { return state_.current_index; }
  inline int GetCurrentInstruction() {
    return state_.code_tree->instructions[state_.current_index];
  }
  inline int GetCurrentAbstractionDepth() {
    return state_.code_tree_depths[state_.current_index];
  }

  inline int GetEnvironmentSize() { return state_.environment_size; }
  inline const Value* GetEnvironmentValues() {
    return state_.environment_values;
  }

  inline Value GetFinalCallback() { return state_.final_callback; }

  // Check if the node stack has at least the given number of values on it,
  // e.g. to verify that the current instruction is valid.
  bool NodeStackSizeAtLeast(int size) {
    const ValueList* stack = state_.code_tree_node_stack;
    while (size-- > 0) {
      if (stack == NULL) return false;
      stack = stack->next;
    }
    return true;
  }

  // Push a Value representing the current code tree node onto code_tree_values
  // and code_tree_node_list.  Advance to the next instruction.
  void PushNodeAndAdvance(memory::MemoryManager* memory_manager, Value value) {
    --state_.current_index;
    ExtraValueMemoryRoot memory_root;
    memory_root.value = value;
    memory_root.parent = this;
    ValueList* list = memory_manager->AllocateArray<ValueList>(2, &memory_root);
    list[0].value = memory_root.value;
    list[0].next = state_.code_tree_node_stack;
    state_.code_tree_node_stack = &list[0];
    list[1].value = memory_root.value;
    list[1].next = state_.code_tree_values;
    state_.code_tree_values = &list[1];
  }

  // Pop a child node off the value stack.
  Value PopNode() {
    Value result = state_.code_tree_node_stack->value;
    state_.code_tree_node_stack = state_.code_tree_node_stack->next;
    return result;
  }

  // Evaluates a constant sub-expression.  The given function is applied to
  // the given parameter and evaluated.  On (asynchronous) return, the result
  // is used to construct a constant node and passed to PushNodeAndAdvance().
  // Then, ContinueInterpretingCodeTree() is called to restart the interpreter
  // loop.
  void CallAndPushConstant(Context* context, Value function, Value parameter) {
    ExtraValueMemoryRoot root1, root2;
    root1.value = function;
    root1.parent = this;
    root2.value = parameter;
    root2.parent = &root1;

    State* state_copy = context->GetMemoryManager()->Allocate<State>(&root2);
    *state_copy = state_;
    context->SetNext(root1.value, root2.value,
                     Value(&ContinueCallback::kSingleton,
                           Data(state_copy)));
  }

  // Copy the completed code tree value list into the given array, which must
  // have size equal to GetCodeTreeSize().
  void CopyNodeTreeValuesTo(Value array[]) {
    while (state_.code_tree_values != NULL) {
      *array = state_.code_tree_values->value;
      state_.code_tree_values = state_.code_tree_values->next;
      ++array;
    }
  }

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    state_.Accept(visitor);
  }

 private:
  // Simple linked list of values.
  struct ValueList {
    Value value;
    const ValueList* next;

    int Accept(memory::MemoryVisitor* visitor) const {
      value.Accept(visitor);
      if (next != NULL) {
        visitor->VisitPointer(&next);
      }
      return sizeof(*this);
    }
  };

  // Encapsulate the state in a struct so that we can easily move the state
  // in and out of tracked memory.
  struct State {
    // Base environment for the module -- i.e. the constants and imports.
    const Value* environment_values;
    int environment_size;

    // The module's code tree.
    const CodeTree* code_tree;

    // For each instruction in the code tree, stores the abstraction depth of
    // that instruction -- i.e. the number of enclosing function scopes.
    int* code_tree_depths;

    // Tracks where we are in interpreting the code tree.  Starts at the end
    // and moves backwards because this gives us a postorder traversal, which
    // makes things easier.
    int current_index;

    // Linked list of values corresponding to nodes in the code tree, starting
    // at current_index + 1.
    const ValueList* code_tree_values;

    // Stack tracking interpretation of the code tree.  Since we're traversing
    // the tree in post-order, interpreting one node means popping all its
    // children from this stack, constructing the value representing that node,
    // then pushing that value back onto the stack.
    const ValueList* code_tree_node_stack;

    // Final callback to call when done interpreting the module.
    Value final_callback;

    int Accept(memory::MemoryVisitor* visitor) const {
      visitor->VisitArrayPointer(&environment_values, environment_size);
      visitor->VisitPointer(&code_tree);
      if (code_tree_values != NULL) {
        visitor->VisitPointer(&code_tree_values);
      }
      if (code_tree_depths != NULL) {
        visitor->VisitRawArrayPointer(&code_tree_depths, code_tree->size);
      }
      if (code_tree_node_stack != NULL) {
        visitor->VisitPointer(&code_tree_node_stack);
      }
      final_callback.Accept(visitor);
      return sizeof(*this);
    }
  };

  // Callback used by CallAndPushConstant() to re-start the interpreter loop.
  class ContinueCallback : public Logic {
   public:
    static const ContinueCallback kSingleton;

    ContinueCallback() {}

    // implements Logic ----------------------------------------------

    void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
      data->AcceptAs<State>(visitor);
    }

    void DebugPrint(Data data, int depth,
                    google::protobuf::io::Printer* printer) const {
      printer->Print("continueInterpreting");
    }

    void Run(Context* context) const {
      Value* constant_value = context->Allocate<Value>();
      *constant_value = context->GetParameter();

      // Recreate the interpreter core.
      InterpretCodeTreeStateRoot state(*context->GetData().As<State>());

      // Push a constant node containing the result of the evaluation.
      state.PushNodeAndAdvance(context->GetMemoryManager(),
          Value(&ConstantLogic::kSingleton, Data(constant_value)));

      // Restart the interpreter loop.
      ContinueInterpretingCodeTree(context, &state);
    }
  };

  State state_;

  InterpretCodeTreeStateRoot(const State& state) : state_(state) {}
};

const InterpretCodeTreeStateRoot::ContinueCallback
    InterpretCodeTreeStateRoot::ContinueCallback::kSingleton;

// A MemoryRoot implementation for short-term use during InterpretCodeTree(),
// just to keep track of a couple things while allocating some other things.
struct InterpretCodeTreeMemoryRoot : public memory::MemoryRoot {
  // The ObjectTracker tracking the code tree that is being interpreted.
  // This may be NULL if the code tree is not tracked.
  memory::ObjectTracker* code_tree_tracker;

  // Pointers to various local variables within InterpretCodeTree().  See
  // the function itself for comments on what these are.
  const Value* callback;
  CodeTree** code_tree_copy;
  Value* environment_values;
  int environment_size;

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    if (code_tree_tracker != NULL) {
      visitor->VisitTrackedObject(code_tree_tracker);
    }
    callback->Accept(visitor);
    if (code_tree_copy != NULL) {
      visitor->VisitPointer(code_tree_copy);
    }
    for (int i = 0; i < environment_size; i++) {
      environment_values[i].Accept(visitor);
    }
  }
};

}  // namespace

void InterpretModule(Context* context,
                     const Module& module,
                     memory::ObjectTracker* module_tracker,
                     const map<string, Value>& imports,
                     Value callback) {
  // Values representing the constants in the module's constant table.
  vector<Value> constant_table_values;

  // Set up the memory root.
  InterpretModuleMemoryRoot memory_root;
  memory_root.module_tracker = module_tracker;
  memory_root.callback = &callback;
  memory_root.constant_table_values = &constant_table_values;
  memory_root.imports = &imports;

  // -----------------------------------------------------------------
  // Translate the constant table

  for (int i = 0; i < module.imports_size(); i++) {
    const Value* import = FindOrNull(imports, module.imports(i));
    if (import == NULL) {
      constant_table_values.push_back(
        builtin::TypeErrorUtils::MakeError(
          context->GetMemoryManager(), &memory_root,
          "Unknown module: " + module.imports(i)));
    } else {
      constant_table_values.push_back(*import);
    }
  }

  for (int i = 0; i < module.int_constants_size(); i++) {
    constant_table_values.push_back(
      builtin::MakeInteger(module.int_constants(i)));
  }

  for (int i = 0; i < module.atom_constants_size(); i++) {
    constant_table_values.push_back(
      context->GetAtomRegistry()->GetAtom(
        context->GetMemoryManager(), module.atom_constants(i)));
  }

  for (int i = 0; i < module.data_constants_size(); i++) {
    constant_table_values.push_back(
      builtin::ByteUtils::CopyString(context->GetMemoryManager(),
                                     &memory_root, module.data_constants(i)));
  }

  // -----------------------------------------------------------------
  // Now we can call InterpretCodeTree() to do the rest.

  InterpretCodeTree(context,
                    module.code_tree().data(), module.code_tree_size(),
                    module_tracker,
                    vector_as_array(&constant_table_values),
                    constant_table_values.size(),
                    callback);
}

void InterpretCodeTree(Context* context,
                       const int32 code_tree[], int code_tree_size,
                       memory::ObjectTracker* code_tree_tracker,
                       Value environment_values[], int environment_size,
                       Value callback) {
  // Set up the memory root.
  InterpretCodeTreeMemoryRoot memory_root;
  memory_root.code_tree_tracker = code_tree_tracker;
  memory_root.callback = &callback;
  memory_root.code_tree_copy = NULL;
  memory_root.environment_values = environment_values;
  memory_root.environment_size = environment_size;

  // -----------------------------------------------------------------
  // Make a copy of the code tree as a CodeTree struct.

  CodeTree* code_tree_copy =
      context->GetMemoryManager()
             ->AllocateObjectWithArray<CodeTree, int32>(
               code_tree_size, &memory_root);
  memory_root.code_tree_copy = &code_tree_copy;
  code_tree_copy->size = code_tree_size;
  memcpy(code_tree_copy->instructions, code_tree,
         sizeof(code_tree[0]) * code_tree_size);

  // -----------------------------------------------------------------
  // Make a copy of the environment.

  Value* environment_copy = context->GetMemoryManager()->AllocateArray<Value>(
      environment_size, &memory_root);
  memcpy(environment_copy, environment_values,
         sizeof(Value) * environment_size);

  InterpretCodeTreeStateRoot new_root(
      environment_copy, environment_size, code_tree_copy, callback);

  if (!new_root.BuildCodeTreeDepths(context->GetMemoryManager())) {
    context->SetNext(
      new_root.GetFinalCallback(),
      builtin::TypeErrorUtils::MakeError(
        context->GetMemoryManager(), &new_root,
        "Module's code tree is invalid."));
    return;
  }

  ContinueInterpretingCodeTree(context, &new_root);
}

namespace {

// Do some work on interpreting a module.  This function is designed such that
// it can stop in the middle and then be called again to continue where it left
// off.  This allows other Evlan code to be evaluated in the middle of
// interpreting a module, by using ContinueInterpretingCodeTree() as the
// callback for that evaluation.
void ContinueInterpretingCodeTree(Context* context,
                                  InterpretCodeTreeStateRoot* state) {
  // We traverse the code tree in postorder.  For each node, we pop its
  // parameters from this stack then push the finished node onto this stack.

  // Traverse in reverse order so that the tree becomes a postorder tree rather
  // than preorder.  This makes the logic easier.
  bool valid = true;
  while (!state->IsDone()) {
    switch (state->GetCurrentInstruction()) {
      case Module::APPLICATION: {
        if (!state->NodeStackSizeAtLeast(2)) {
          valid = false;
          break;
        }
        ApplicationData* data =
          context->GetMemoryManager()
                 ->Allocate<ApplicationData>(state);
        data->function_code = state->PopNode();
        data->parameter_code = state->PopNode();

        if (IsConstant(data->function_code) &&
            IsConstant(data->parameter_code)) {
          // Both the function and parameter are constants.  Let's go ahead
          // and invoke the function now.
          state->CallAndPushConstant(context,
              *data->function_code.GetData().As<Value>(),
              *data->parameter_code.GetData().As<Value>());
          return;
        } else {
          state->PushNodeAndAdvance(
            context->GetMemoryManager(),
            Value(&ApplicationLogic::kSingleton, Data(data)));
        }
        break;
      }

      case Module::ABSTRACTION: {
        if (!state->NodeStackSizeAtLeast(1)) {
          valid = false;
          break;
        }
        AbstractionData* data =
          context->GetMemoryManager()
                 ->Allocate<AbstractionData>(state);
        data->body_code = state->PopNode();
        data->code_tree_index = state->GetCurrentIndex();
        data->module_code_tree = state->GetCodeTree();

        state->PushNodeAndAdvance(
            context->GetMemoryManager(),
            Value(&AbstractionLogic::kSingleton, Data(data)));
        break;
      }

      default: {
        // A variable.

        int index = state->GetCurrentInstruction() - Module::FIRST_VARIABLE;
        int depth = state->GetCurrentAbstractionDepth();
        if (index >= depth) {
          if (index - depth > state->GetEnvironmentSize()) {
            valid = false;
            break;
          }

          // The variable is in the constant table.  Create a constant node.
          Value* constant_value =
              context->GetMemoryManager()->Allocate<Value>(state);
          *constant_value = state->GetEnvironmentValues()[index - depth];
          state->PushNodeAndAdvance(context->GetMemoryManager(),
              Value(&ConstantLogic::kSingleton, Data(constant_value)));
        } else {
          // Variable is a function parameter.
          state->PushNodeAndAdvance(
              context->GetMemoryManager(),
              MakeVariable(index));
        }
        break;
      }
    }

    if (!valid) break;
  }

  if (!state->NodeStackSizeAtLeast(1) || state->NodeStackSizeAtLeast(2)) {
    valid = false;
  }

  if (!valid) {
    context->SetNext(
      state->GetFinalCallback(),
      builtin::TypeErrorUtils::MakeError(
        context->GetMemoryManager(), state,
        "Module's code tree is invalid."));
    return;
  }

  // Combine code_tree_values into a single value.
  Value* code_tree_values_array =
    context->GetMemoryManager()->AllocateArray<Value>(
      state->GetCodeTreeSize(), state);
  state->CopyNodeTreeValuesTo(code_tree_values_array);

  ExtraValueMemoryRoot new_memory_root;
  new_memory_root.value =
    builtin::ArrayUtils::AliasArray(context->GetMemoryManager(), state,
                                    state->GetCodeTreeSize(),
                                    code_tree_values_array);
  new_memory_root.parent = state;

  // -----------------------------------------------------------------
  // Set up the root environment.

  Environment* root_environment =
    context->GetMemoryManager()->AllocateObjectWithArray<Environment, Value>(
      state->GetEnvironmentSize() + 1, &new_memory_root);
  root_environment->size = state->GetEnvironmentSize() + 1;
  root_environment->next = NULL;
  root_environment->parent_function_data = NULL;
  root_environment->module_code_tree = state->GetCodeTree();

  memcpy(root_environment->values, state->GetEnvironmentValues(),
         sizeof(Value) * state->GetEnvironmentSize());

  // The last item on the stack is the code tree value array.
  root_environment->values[state->GetEnvironmentSize()] = new_memory_root.value;

  // -----------------------------------------------------------------

  // The stack contains one value:  the root of the code tree.  Apply it
  // to the root environment to evaluate the module.
  context->SetNext(state->PopNode(),
                   Value(&EnvironmentLogic::kSingleton,
                         Data(root_environment)),
                   state->GetFinalCallback());
}

// -------------------------------------------------------------------

// A MemoryRoot that only tracks the callback value.
struct CallbackMemoryRoot : public memory::MemoryRoot {
  Value* callback;

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    callback->Accept(visitor);
  }
};

// A MemoryRoot implementation for use during GetSubFunction().
struct GetSubFunctionMemoryRoot : public memory::MemoryRoot {
  // Pointers to local variables in GetSubFunction().
  Value* body_code;
  const FunctionData** function_data;
  const Environment** parent_environment;
  const Value** sub_environment_values;
  int sub_environment_size;
  bool sub_environment_is_tracked;
  Value* callback;

  // implements MemoryRoot -------------------------------------------

  void Accept(memory::MemoryVisitor* visitor) const {
    body_code->Accept(visitor);
    visitor->VisitPointer(function_data);
    visitor->VisitPointer(parent_environment);
    if (sub_environment_is_tracked) {
      visitor->VisitArrayPointer(sub_environment_values, sub_environment_size);
    } else {
      for (int i = 0; i < sub_environment_size; i++) {
        (*sub_environment_values)[i].Accept(visitor);
      }
    }
    callback->Accept(visitor);
  }
};

}  // namespace

void GetSubFunction(Context* context,
                    Value parent_function,
                    int code_tree_offset,
                    const Value sub_environment_values[],
                    int sub_environment_size,
                    bool sub_environment_is_tracked,
                    Value callback) {
  if (builtin::TypeErrorUtils::IsError(parent_function)) {
    context->SetNext(callback, parent_function);
    return;
  }

  // Used only when calling MakeError(), in which case we can discard everything
  // else.
  CallbackMemoryRoot callback_memory_root;
  callback_memory_root.callback = &callback;

  // Extract the FunctionLogic from parent_function.
  if (parent_function.GetLogic() != &FunctionLogic::kSingleton) {
    context->SetNext(
      callback,
      builtin::TypeErrorUtils::MakeError(
        context->GetMemoryManager(), &callback_memory_root,
        "GetSubFunction() called on non-function."));
    return;
  }

  const FunctionData* function_data =
      parent_function.GetData().As<FunctionData>();
  const Environment* parent_environment = function_data->environment;

  // Find the index of the root of the sub-function's code tree.
  int code_tree_index = function_data->code_tree_index + code_tree_offset;

  // Follow environment to end to get the code tree array.
  Value code_tree_value_array;

  {
    const Environment* environment = parent_environment;
    while (environment->next != NULL) {
      environment = environment->next;
    }
    code_tree_value_array = environment->values[environment->size - 1];
  }

  // Make sure it's actually an array.
  if (!builtin::ArrayUtils::IsArray(code_tree_value_array)) {
    context->SetNext(
      callback,
      builtin::TypeErrorUtils::MakeError(
        context->GetMemoryManager(), &callback_memory_root,
        "GetSubFunction() failed because last element in environment was "
        "not an array."));
    return;
  }

  // Make sure that the code tree index is in bounds for said array.
  int code_tree_size = builtin::ArrayUtils::GetArraySize(code_tree_value_array);
  if (code_tree_index < 0 || code_tree_index >= code_tree_size) {
    context->SetNext(
      callback,
      builtin::TypeErrorUtils::MakeError(
        context->GetMemoryManager(), &callback_memory_root,
        "code_tree_index out-of-bounds when calling GetSubFunction()."));
    return;
  }

  // Get the Value representing the sub-function's code tree.
  Value body_code = builtin::ArrayUtils::GetElement(
      code_tree_value_array, code_tree_index);

  // Set up a MemoryRoot with all the pointers we've gathered.
  GetSubFunctionMemoryRoot memory_root;
  memory_root.body_code = &body_code;
  memory_root.function_data = &function_data;
  memory_root.parent_environment = &parent_environment;
  memory_root.sub_environment_values = &sub_environment_values;
  memory_root.sub_environment_size = sub_environment_size;
  memory_root.sub_environment_is_tracked = sub_environment_is_tracked;
  memory_root.callback = &callback;

  // Allocate the sub-function environment.
  Environment* sub_environment =
    context->GetMemoryManager()->AllocateObjectWithArray<Environment, Value>(
      sub_environment_size, &memory_root);

  // Fill it in.
  sub_environment->size = sub_environment_size;
  sub_environment->next = parent_environment;
  sub_environment->parent_function_data = function_data;
  sub_environment->module_code_tree = parent_environment->module_code_tree;
  for (int i = 0; i < sub_environment_size; i++) {
    sub_environment->values[i] = sub_environment_values[i];
  }

  // Call the body with the new environment.  It will return the new function.
  context->SetNext(body_code,
                   Value(&EnvironmentLogic::kSingleton,
                         Data(sub_environment)),
                   callback);
}

}  // namespace interpreter
}  // namespace vm
}  // namespace evlan
