#! /usr/bin/env python

# Copyright (c) 2006-2012 Google, Inc. and contributors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Author: Kenton Varda
#
# This script generates two highy-repetitive C++ header files:
# * method_glue_decls.h:
#     Declares classes MethodGlueN and MethodGlueWithEvaluatorN, and
#     corresponding NewMethodGlue() factory functions, for all N in the range
#     [0, MAX_PARAMS].
# * method_glue_defs.h:
#     Defines the classes and functions declared in method_glue.h.
#
# These two headers are #included only from cpp_glue.h.  They should really
# be considered a part of cpp_glue.h.  The only reason why they're separate
# is that writing these by hand would have been too tedious.

import string
import sys

# Maximum number of parameters that we support.
MAX_PARAMS = 6

# ====================================================================
# Templates which will be expanded below

PRELUDE = string.Template(
"""// THIS FILE IS GENERATED BY generate_method_glue.py
// DO NOT EDIT!

// This file should ONLY be #included from cpp_glue.h, and it should only
// #include this file once, so we use a non-traditional include guard.
#if !defined(EVLAN_PUBLIC_CPP_GLUE_H__) || defined(${guard_name})
#error "You cannot include this file directly."
#endif
#define ${guard_name}
""")

DECLARATION = string.Template("""
template <typename Class, typename Result${typenames}>
class ${classname};

template <typename Class, typename Result$typenames>
static MethodGlue* NewMethodGlue(
    CppGlue* cpp_glue,
    EvlanEvaluator* evaluator,
    const StringPiece& name,
    Result (Class::*method)(${types}) const);
""")

DEFINITION = string.Template("""
template <typename Class, typename Result${typenames}>
class CppGlue::${classname} : public CppGlue::MethodGlue {
 public:
  typedef Result (Class::*MethodPtr)(${types}) const;

  ${classname}(
      const CppGlue* cpp_glue,
      EvlanEvaluator* evaluator,
      const StringPiece& name,
      MethodPtr method)
    : MethodGlue(cpp_glue, evaluator, name),
      method_(method) {}

  virtual ~${classname}() {}

  virtual EvlanValue* Call(EvlanEvaluator* evaluator,
                           const void* object,
                           EvlanValue* parameter) {
    scoped_ptr<EvlanValue> parameters[${count}];
    if (!parameter->GetTupleContents(${count}, parameters)) {
      return ArityError(evaluator, TypeInfoImpl<Class>::GetSingleton(),
                        original_name_, parameter, ${count});
    }
${translators}
    return cpp_glue_->ToEvlanValue(evaluator,
      (reinterpret_cast<const Class*>(object)->*method_)(${values}));
  }

 private:
  MethodPtr method_;
};

template <typename Class, typename Result$typenames>
CppGlue::MethodGlue* CppGlue::NewMethodGlue(
    CppGlue* cpp_glue,
    EvlanEvaluator* evaluator,
    const StringPiece& name,
    Result (Class::*method)(${types}) const) {
  cpp_glue->RegisterArbitraryType<Result>();
${register_types}
  return new ${classname}<Class, Result${types_leading_comma}>(
    cpp_glue, evaluator, name, method);
}
""")

TRANSLATOR = string.Template("""
    ParameterTranslator<Arg${i}> arg${i};
    if (!arg${i}.Translate(parameters[${i}].get())) {
      return ParameterTypeError(evaluator, TypeInfoImpl<Class>::GetSingleton(),
                                original_name_, parameters[${i}].get(),
                                GetTypeName<Arg${i}>(), ${i});
    }
""")

# ====================================================================
# Code to expand templates

if len(sys.argv) != 3:
  sys.stderr.write(
    "Wrong number of arguments.\n"
    "USAGE: %s DECLS_FILE DEFS_FILE\n" % sys.argv[0]);
  sys.exit(1)

header = open(sys.argv[1], "w")
inl = open(sys.argv[2], "w")

header.write(
  PRELUDE.substitute(guard_name = "EVLAN_PUBLIC_METHOD_GLUE_DECLS_H__"))
inl.write(
  PRELUDE.substitute(guard_name = "EVLAN_PUBLIC_METHOD_GLUE_DEFS_H__"))

for count in range(0, MAX_PARAMS + 1):
  vars = {}
  vars["count"] = count
  vars["classname"] = "MethodGlue%d" % count
  vars["typenames"] = "".join(
    [ ", typename Arg%d" % i for i in range(0, count) ])
  vars["types"] = ", ".join(
    [ "Arg%d" % i for i in range(0, count) ])
  vars["translators"] = "".join(
    [ TRANSLATOR.substitute(i = i) for i in range(0, count) ])
  vars["values"] = ", ".join(
    [ "arg%d.Get()" % i for i in range(0, count) ])
  vars["register_types"] = "".join(
    [ "  cpp_glue->RegisterArbitraryType<Arg%d>();\n" % i
      for i in range(0, count) ])

  if count == 0:
    vars["types_leading_comma"] = ""
  else:
    vars["types_leading_comma"] = ", " + vars["types"]

  header.write(DECLARATION.substitute(vars))
  inl.write(DEFINITION.substitute(vars))

  # Write equivalent MethodGlueWithEvaluator.
  vars["classname"] = "MethodGlueWithEvaluator%d" % count
  if count > 0:
    vars["types"] = "EvlanEvaluator*, " + vars["types"]
    vars["values"] = "evaluator, " + vars["values"]
  else:
    vars["types"] = "EvlanEvaluator*"
    vars["values"] = "evaluator"

  header.write(DECLARATION.substitute(vars))
  inl.write(DEFINITION.substitute(vars))