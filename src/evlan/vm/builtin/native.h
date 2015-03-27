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

#ifndef EVLAN_VM_NATIVE_H__
#define EVLAN_VM_NATIVE_H__

#include <unordered_map>
#include <vector>
#include <string>

#include "evlan/common.h"
#include "evlan/stringpiece.h"
#include "evlan/strutil.h"
#include "evlan/vm/memory/memory_manager.h"
#include "evlan/vm/runtime.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/double.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/boolean.h"
#include "evlan/vm/builtin/array.h"
#include "evlan/vm/builtin/protobuf.h"
#include "evlan/vm/builtin/type_error.h"
#include <google/protobuf/io/printer.h>

namespace evlan {
  namespace vm {
    namespace distribute {
      class ValueRegistrar;
    }
  }
}

namespace evlan {
namespace vm {
namespace builtin {

// Logic for a value which has no Data.
class StatelessLogic : public Logic {
 public:
  virtual ~StatelessLogic();

  // Register any stateless values which are nested within this logic.
  virtual void RegisterNestedValues(distribute::ValueRegistrar* registrar,
                                    memory::MemoryManager* memory_manager,
                                    memory::MemoryRoot* memory_root) const;
};

// NativeModule assists in making C++ functions callable from Evlan.  It is
// a Logic for an object which contains a set of functions which are
// implemented in pure C++.  Example usage:
//
//   int Square(int i) { return i * i; }
//   ...
//   NativeModule module("moduleName");
//   module.AddFunction("square", &Square);
//   Value moduleValue(&module, Data());
//   // ... make moduleValue available from Evlan code ...
//
// This effectively exports the function Square() so that is may be called from
// Evlan.  Template magic automatically converts the input to a C++ int and
// the returned value back to an Evlan integer.  If the input is of the wrong
// type, a type error will be returned instead.
//
// See the unit test, or any of the builtin type implementatinos such as
// integer.cc, for examples.
class NativeModule : public Logic {
 public:
  NativeModule(const StringPiece& name);
  virtual ~NativeModule();

  void Register(distribute::ValueRegistrar* registrar,
                memory::MemoryManager* memory_manager,
                memory::MemoryRoot* memory_root) const;

  // Add a member to the module implemented by the given Logic.  The Logic
  // must ignore its associated Data.  The NativeModule does not take ownership
  // of the Logic; it must remain valid until the NativeModule is destroyed.
  void AddLogic(const StringPiece& name, const Logic* logic);

  // Like AddLogic() but takes ownership of the Logic.
  void AddAndAdoptLogic(const StringPiece& name, Logic* logic);

  // Add the given function as a member of the module.  The function may take
  // up to three parameters.  Each parameter may be one of the following types:
  // int, bool, const string&, StringPiece, Value.  The function must return
  // a value, which may be any of the same set of types (except const string&,
  // but it may return a plain string).
  template <typename Result, typename Arg0>
  inline void AddFunction(const StringPiece& name, Result (*function)(Arg0));

  template <typename Result, typename Arg0, typename Arg1>
  inline void AddFunction(const StringPiece& name,
                          Result (*function)(Arg0, Arg1));

  template <typename Result, typename Arg0, typename Arg1, typename Arg2>
  inline void AddFunction(const StringPiece& name,
                          Result (*function)(Arg0, Arg1, Arg2));

  // Like the above, but the function additionally takes a pointer to the
  // Context as its first parameter.  The function must return void; it should
  // indicate its Evlan return value by calling Context::Return().  You may
  // allocate memory through the Context, but be aware that if the function
  // parameters contain pointers to tracked memory (e.g. StringPieces and
  // Values), these pointers will be invalid after any memory allocation.
  template <typename Arg0>
  inline void AddFunction(const StringPiece& name,
                          void (*function)(Context*, Arg0));

  template <typename Arg0, typename Arg1>
  inline void AddFunction(const StringPiece& name,
                          void (*function)(Context*, Arg0, Arg1));

  template <typename Arg0, typename Arg1, typename Arg2>
  inline void AddFunction(const StringPiece& name,
                          void (*function)(Context*, Arg0, Arg1, Arg2));

  // Add a plain old Value as a member of the module.  The Value's logic and
  // data must not be tracked by any MemoryManager.
  void AddValue(const StringPiece& name, const Value& value);

  // implements Logic ------------------------------------------------

  void Run(Context* context) const;
  void Accept(const Data* data,
              memory::MemoryVisitor* visitor) const;
  void GetId(Data data, ValueId* output) const;
  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const;

 private:
  // Template class which translates a Value into a C++ type for use as a
  // function parameter.  Also does type-checking.
  template <typename Type>
  class ParameterTranslator;

  // Translates a returned C++ value into an Evlan value (making the given
  // Context return that value).
  static inline void TranslateResult(Context* context, const Value& result);
  static inline void TranslateResult(Context* context, int result);
  static inline void TranslateResult(Context* context, double result);
  static inline void TranslateResult(Context* context, bool result);
  static inline void TranslateResult(Context* context,
                                     const StringPiece& result);

  // Get the name of a type, for use in debug messages.
  template<typename Type> static StringPiece TypeName();

  // Common base class for NativeLogic template classes.
  class NativeLogic;

  // Logics which wrap simple C++ functions.
  template <typename Result, typename Arg0>
  class NativeLogic1;

  template <typename Result, typename Arg0, typename Arg1>
  class NativeLogic2;

  template <typename Result, typename Arg0, typename Arg1, typename Arg3>
  class NativeLogic3;

  // Logics which wrap C++ functions that take a Context pointer as the
  // first parameter.
  template <typename Arg0>
  class NativeLogicContext1;

  template <typename Arg0, typename Arg1>
  class NativeLogicContext2;

  template <typename Arg0, typename Arg1, typename Arg3>
  class NativeLogicContext3;

  // -----------------------------------------------------------------

  typedef unordered_map<string, Value> ValueMap;

  string name_;
  ValueMap value_map_;
  vector<const Logic*> logics_to_delete_;

  // -----------------------------------------------------------------

  void RegisterNestedValues(distribute::ValueRegistrar* registrar,
                            memory::MemoryManager* memory_manager,
                            memory::MemoryRoot* memory_root) const;
};

// ===================================================================
// implementation details

// TODO(kenton):  Move some template specializations to the .cc file?

template <>
class NativeModule::ParameterTranslator<Value> {
 public:
  inline bool Translate(const Value& value) {
    value_ = &value;
    return true;
  }

  inline const Value& Get() { return *value_; }

 private:
  const Value* value_;
};

template <>
class NativeModule::ParameterTranslator<int> {
 public:
  inline bool Translate(const Value& value) {
    if (!IsInteger(value)) return false;
    value_ = GetIntegerValue(value);
    return true;
  }

  inline int Get() { return value_; }

 private:
  int value_;
};

template <>
class NativeModule::ParameterTranslator<double> {
 public:
  inline bool Translate(const Value& value) {
    if (IsInteger(value)) {
      value_ = GetIntegerValue(value);
    } else if (IsDouble(value)) {
      value_ = GetDoubleValue(value);
    } else {
      return false;
    }

    return true;
  }

  inline double Get() { return value_; }

 private:
  double value_;
};

template <>
class NativeModule::ParameterTranslator<bool> {
 public:
  inline bool Translate(const Value& value) {
    if (!BooleanUtils::IsBoolean(value)) return false;
    value_ = BooleanUtils::GetBooleanValue(value);
    return true;
  }

  inline bool Get() { return value_; }

 private:
  bool value_;
};

template <>
class NativeModule::ParameterTranslator<const string&> {
 public:
  inline bool Translate(const Value& value) {
    if (!ByteUtils::IsByteArray(value)) return false;
    ByteUtils::ToString(value).CopyToString(&value_);
    return true;
  }

  inline const string& Get() { return value_; }

 private:
  string value_;
};

template <>
class NativeModule::ParameterTranslator<StringPiece> {
 public:
  inline bool Translate(const Value& value) {
    if (!ByteUtils::IsByteArray(value)) return false;
    value_ = ByteUtils::ToString(value);
    return true;
  }

  inline StringPiece Get() { return value_; }

 private:
  StringPiece value_;
};

template <>
class NativeModule::ParameterTranslator<const google::protobuf::Message*> {
 public:
  inline bool Translate(const Value& value) {
    if (!ProtobufUtils::IsMessage(value)) return false;
    value_ = ProtobufUtils::GetMessage(value);
    return true;
  }

  inline const google::protobuf::Message* Get() { return value_; }

 private:
  const google::protobuf::Message* value_;
};

inline void NativeModule::TranslateResult(Context* context,
                                          const Value& result) {
  context->Return(result);
}

inline void NativeModule::TranslateResult(Context* context, int result) {
  context->Return(MakeInteger(result));
}

inline void NativeModule::TranslateResult(Context* context, double result) {
  context->Return(MakeDouble(result));
}

inline void NativeModule::TranslateResult(Context* context, bool result) {
  context->Return(BooleanUtils::MakeBoolean(result));
}

inline void NativeModule::TranslateResult(Context* context,
                                          const StringPiece& result) {
  context->Return(ByteUtils::CopyString(context->GetMemoryManager(),
                                        context, result));
}

template<>
inline StringPiece NativeModule::TypeName<Value>() {
  return "Value";
}

template<>
inline StringPiece NativeModule::TypeName<int>() {
  return "Integer";
}

template<>
inline StringPiece NativeModule::TypeName<double>() {
  return "Double";
}

template<>
inline StringPiece NativeModule::TypeName<bool>() {
  return "Boolean";
}

template<>
inline StringPiece NativeModule::TypeName<StringPiece>() {
  return "String";
}

template<>
inline StringPiece NativeModule::TypeName<string>() {
  return "String";
}

template<>
inline StringPiece NativeModule::TypeName<const string&>() {
  return "String";
}

template<>
inline StringPiece NativeModule::TypeName<const google::protobuf::Message*>() {
  return "ProtocolMessage";
}

class NativeModule::NativeLogic : public Logic {
 public:
  NativeLogic(const NativeModule* module, StringPiece name);
  virtual ~NativeLogic();

  // implements Logic ------------------------------------------------

  // Accept() never has anything to mark for NativeLogic implementations.
  void Accept(const Data* data, memory::MemoryVisitor* visitor) const;

  void GetId(Data data, ValueId* output) const;

 protected:
  template<typename Type>
  bool TranslateParameter(Context* context,
                          int index, const Value& value,
                          ParameterTranslator<Type>* arg) const {
    if (TypeErrorUtils::IsError(value)) {
      context->Return(value);
      return false;
    } else if (!arg->Translate(value)) {
      context->Return(TypeErrorUtils::MakeError(
        context->GetMemoryManager(), context,
        strings::Substitute(
          "Wrong type for parameter $0 to native function \"$1\".  "
          "Expected $2, got \"$3\".",
          index, name_, TypeName<Type>().as_string(), value.DebugString())));
      return false;
    } else {
      return true;
    }
  }

  inline bool CheckArity(Context* context, int arity) const {
    if (TypeErrorUtils::IsError(context->GetParameter())) {
      context->Return(context->GetParameter());
      return false;
    } else if (arity != 1 &&
               (!ArrayUtils::IsTuple(context->GetParameter()) ||
                ArrayUtils::GetArraySize(context->GetParameter()) != arity)) {
      context->Return(TypeErrorUtils::MakeError(
        context->GetMemoryManager(), context,
        strings::Substitute(
          "Wrong number of arguments to native function \"$0\".",
          name_)));
      return false;
    } else {
      return true;
    }
  }

 protected:
  const NativeModule* const module_;
  const string name_;
};

template <typename Result, typename Arg0>
class NativeModule::NativeLogic1 : public NativeModule::NativeLogic {
 public:
  typedef Result Function(Arg0);

  NativeLogic1(const NativeModule* module, const StringPiece& name,
               Function* function)
    : NativeLogic(module, name),
      function_(function) {}

  // implements NativeLogic ------------------------------------------

  void Run(Context* context) const {
    if (!CheckArity(context, 1)) return;
    ParameterTranslator<Arg0> arg0;
    if (!TranslateParameter(context, 0, context->GetParameter(), &arg0)) return;
    TranslateResult(context, function_(arg0.Get()));
  }

  void DebugPrint(Data data, int depth, google::protobuf::io::Printer* printer) const {
    map<string, string> vars;
    vars["name"] = name_;
    vars["arg0"] = TypeName<Arg0>().as_string();
    vars["result"] = TypeName<Result>().as_string();
    printer->Print(vars, "nativeFunction($name$($arg0$): $result$)");
  }

 private:
  Function* function_;
};

template <typename Result, typename Arg0, typename Arg1>
class NativeModule::NativeLogic2 : public NativeModule::NativeLogic {
 public:
  typedef Result Function(Arg0, Arg1);

  NativeLogic2(const NativeModule* module, const StringPiece& name,
               Function* function)
    : NativeLogic(module, name),
      function_(function) {}

  // implements NativeLogic ------------------------------------------

  void Run(Context* context) const {
    if (!CheckArity(context, 2)) return;

    const Value* params = ArrayUtils::AsCArray(context->GetParameter());
    ParameterTranslator<Arg0> arg0;
    ParameterTranslator<Arg1> arg1;

    if (!TranslateParameter(context, 0, params[0], &arg0)) return;
    if (!TranslateParameter(context, 1, params[1], &arg1)) return;

    EVLAN_EXPECT_SOMETIMES(ArrayUtils::TryWithdraw(
        context->GetMemoryManager(), context->GetParameter()));

    TranslateResult(context, function_(arg0.Get(), arg1.Get()));
  }

  void DebugPrint(Data data, int depth, google::protobuf::io::Printer* printer) const {
    map<string, string> vars;
    vars["name"] = name_;
    vars["arg0"] = TypeName<Arg0>().as_string();
    vars["arg1"] = TypeName<Arg1>().as_string();
    vars["result"] = TypeName<Result>().as_string();
    printer->Print(vars, "nativeFunction($name$($arg0$, $arg1$): $result$)");
  }

 private:
  Function* function_;
};

template <typename Result, typename Arg0, typename Arg1, typename Arg2>
class NativeModule::NativeLogic3 : public NativeModule::NativeLogic {
 public:
  typedef Result Function(Arg0, Arg1, Arg2);

  NativeLogic3(const NativeModule* module, const StringPiece& name,
               Function* function)
    : NativeLogic(module, name),
      function_(function) {}

  // implements NativeLogic ------------------------------------------

  void Run(Context* context) const {
    if (!CheckArity(context, 3)) return;

    const Value* params = ArrayUtils::AsCArray(context->GetParameter());
    ParameterTranslator<Arg0> arg0;
    ParameterTranslator<Arg1> arg1;
    ParameterTranslator<Arg2> arg2;

    if (!TranslateParameter(context, 0, params[0], &arg0)) return;
    if (!TranslateParameter(context, 1, params[1], &arg1)) return;
    if (!TranslateParameter(context, 2, params[2], &arg2)) return;

    EVLAN_EXPECT_SOMETIMES(ArrayUtils::TryWithdraw(
        context->GetMemoryManager(), context->GetParameter()));

    TranslateResult(context, function_(arg0.Get(), arg1.Get(), arg2.Get()));
  }

  void DebugPrint(Data data, int depth, google::protobuf::io::Printer* printer) const {
    map<string, string> vars;
    vars["name"] = name_;
    vars["arg0"] = TypeName<Arg0>().as_string();
    vars["arg1"] = TypeName<Arg1>().as_string();
    vars["arg2"] = TypeName<Arg2>().as_string();
    vars["result"] = TypeName<Result>().as_string();
    printer->Print(vars,
      "nativeFunction($name$($arg0$, $arg1$, $arg2$): $result$)");
  }

 private:
  Function* function_;
};

template <typename Arg0>
class NativeModule::NativeLogicContext1 : public NativeModule::NativeLogic {
 public:
  typedef void Function(Context*, Arg0);

  NativeLogicContext1(const NativeModule* module, const StringPiece& name,
                      Function* function)
    : NativeLogic(module, name),
      function_(function) {}

  // implements NativeLogic ------------------------------------------

  void Run(Context* context) const {
    if (!CheckArity(context, 1)) return;
    ParameterTranslator<Arg0> arg0;
    if (!TranslateParameter(context, 0, context->GetParameter(), &arg0)) return;
    function_(context, arg0.Get());
  }

  void DebugPrint(Data data, int depth, google::protobuf::io::Printer* printer) const {
    map<string, string> vars;
    vars["name"] = name_;
    vars["arg0"] = TypeName<Arg0>().as_string();
    printer->Print(vars, "nativeFunction($name$($arg0$): Value)");
  }

 private:
  Function* function_;
};

template <typename Arg0, typename Arg1>
class NativeModule::NativeLogicContext2 : public NativeModule::NativeLogic {
 public:
  typedef void Function(Context*, Arg0, Arg1);

  NativeLogicContext2(const NativeModule* module, const StringPiece& name,
                      Function* function)
    : NativeLogic(module, name),
      function_(function) {}

  // implements NativeLogic ------------------------------------------

  void Run(Context* context) const {
    if (!CheckArity(context, 2)) return;

    const Value* params = ArrayUtils::AsCArray(context->GetParameter());
    ParameterTranslator<Arg0> arg0;
    ParameterTranslator<Arg1> arg1;

    if (!TranslateParameter(context, 0, params[0], &arg0)) return;
    if (!TranslateParameter(context, 1, params[1], &arg1)) return;

    // TODO(kenton):  Can't withdraw the parameter tuple because the callee
    //   may call context->Allocate(), which will treat the parameter as
    //   part of the memory root.  Maybe we should disallow this?

    function_(context, arg0.Get(), arg1.Get());
  }

  void DebugPrint(Data data, int depth, google::protobuf::io::Printer* printer) const {
    map<string, string> vars;
    vars["name"] = name_;
    vars["arg0"] = TypeName<Arg0>().as_string();
    vars["arg1"] = TypeName<Arg1>().as_string();
    printer->Print(vars, "nativeFunction($name$($arg0$, $arg1$): Value)");
  }

 private:
  Function* function_;
};

template <typename Arg0, typename Arg1, typename Arg2>
class NativeModule::NativeLogicContext3 : public NativeModule::NativeLogic {
 public:
  typedef void Function(Context*, Arg0, Arg1, Arg2);

  NativeLogicContext3(const NativeModule* module, const StringPiece& name,
                      Function* function)
    : NativeLogic(module, name),
      function_(function) {}

  // implements NativeLogic ------------------------------------------

  void Run(Context* context) const {
    if (!CheckArity(context, 3)) return;

    const Value* params = ArrayUtils::AsCArray(context->GetParameter());
    ParameterTranslator<Arg0> arg0;
    ParameterTranslator<Arg1> arg1;
    ParameterTranslator<Arg2> arg2;

    // TODO(kenton):  Can't withdraw the parameter tuple because the callee
    //   may call context->Allocate(), which will treat the parameter as
    //   part of the memory root.  Maybe we should disallow this?

    if (!TranslateParameter(context, 0, params[0], &arg0)) return;
    if (!TranslateParameter(context, 1, params[1], &arg1)) return;
    if (!TranslateParameter(context, 2, params[2], &arg2)) return;

    function_(context, arg0.Get(), arg1.Get(), arg2.Get());
  }

  void DebugPrint(Data data, int depth, google::protobuf::io::Printer* printer) const {
    map<string, string> vars;
    vars["name"] = name_;
    vars["arg0"] = TypeName<Arg0>().as_string();
    vars["arg1"] = TypeName<Arg1>().as_string();
    vars["arg2"] = TypeName<Arg2>().as_string();
    printer->Print(vars,
      "nativeFunction($name$($arg0$, $arg1$, $arg2$): Value)");
  }

 private:
  Function* function_;
};

// -------------------------------------------------------------------

template <typename Result, typename Arg0>
inline void NativeModule::AddFunction(const StringPiece& name,
                                      Result (*function)(Arg0)) {
  AddAndAdoptLogic(name, new NativeLogic1<Result, Arg0>(this, name, function));
}

template <typename Result, typename Arg0, typename Arg1>
inline void NativeModule::AddFunction(const StringPiece& name,
                                      Result (*function)(Arg0, Arg1)) {
  AddAndAdoptLogic(name,
    new NativeLogic2<Result, Arg0, Arg1>(this, name, function));
}

template <typename Result, typename Arg0, typename Arg1, typename Arg2>
inline void NativeModule::AddFunction(const StringPiece& name,
                                      Result (*function)(Arg0, Arg1, Arg2)) {
  AddAndAdoptLogic(name,
    new NativeLogic3<Result, Arg0, Arg1, Arg2>(this, name, function));
}

template <typename Arg0>
inline void NativeModule::AddFunction(
    const StringPiece& name, void (*function)(Context*, Arg0)) {
  AddAndAdoptLogic(name,
    new NativeLogicContext1<Arg0>(this, name, function));
}

template <typename Arg0, typename Arg1>
inline void NativeModule::AddFunction(
    const StringPiece& name, void (*function)(Context*, Arg0, Arg1)) {
  AddAndAdoptLogic(name,
    new NativeLogicContext2<Arg0, Arg1>(this, name, function));
}

template <typename Arg0, typename Arg1, typename Arg2>
inline void NativeModule::AddFunction(
    const StringPiece& name, void (*function)(Context*, Arg0, Arg1, Arg2)) {
  AddAndAdoptLogic(name,
    new NativeLogicContext3<Arg0, Arg1, Arg2>(this, name, function));
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

#endif  // EVLAN_VM_NATIVE_H__
