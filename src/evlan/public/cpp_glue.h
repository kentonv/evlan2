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
// This file allows you to make C++ classes callable from within Evlan.  Using
// it involves two steps.  First, you must use the EVLAN_EXPORT_* macros to
// declare your class's interface to Evlan.  Example:
//
//   class Foo {
//    public:
//     int SomeMethod() const;
//     bool AnotherMethod(string a, const Bar& b) const;
//
//     const string& some_field() const;
//   };
//
//   EVLAN_EXPORT_TYPE(Foo) {
//     EVLAN_EXPORT_METHOD(SomeMethod);
//     EVLAN_EXPORT_METHOD(AnotherMethod);
//     EVLAN_EXPORT_FIELD(some_field);
//   }
//
// Notes:
// * All exported methods must be const, since Evlan is purely-functional.
// * You do not have to export all of the class's methods; only the ones you
//   want to be able to call.
// * The EVLAN_EXPORT block should appear in a header file, and must be in
//   the same namespace as the type itself.  You should probably either put
//   it in the same header where the type is declared or (if you want to avoid
//   making all users of the type depend on Evlan) put it in a header in the
//   same directory with the _evlan.h suffix.  It's important that every type
//   have only one EVLAN_EXPORT block as having multiple would be a violation
//   of the C++ One Definition Rule (but may not be caught by the compiler if
//   the two definitions are never #included by the same .cc file).
// * The EVLAN_EXPORT block cannot be nested within a class.  If you wish to
//   export a private nested type, you will have to declare it a friend of the
//   EVLAN_EXPORT macros like so:
//     class Foo {
//       ...
//      private:
//       class Bar { ... };
//       EVLAN_EXPORT_FRIEND(Bar);
//     };
//     EVLAN_EXPORT_TYPE(Foo::Bar);
//
// Second, use EvlanEvaluator::NewCppObject() to wrap an instance of the
// object:
//
//   EvlanEvaluator evaluator;
//   Foo* foo = new Foo;
//   EvlanValue* value = evaluator.NewCppObject(foo);
//
// Voila.  You now have an EvlanValue which you can pass into other Evlan code.
// Note that the names of the exported methods will be coerced to Evlan style,
// meaning the first letter is lower-cased.  For example, here's a function
// which takes a Foo and calls its SomeMethod() method:
//
//   f = foo => foo.someMethod()
//
// The EVLAN_EXPORT_FIELD is a special case.  The method must be a zero-argument
// accessor method.  In Evlan code, you will not have to pass an argument
// list at all.  Example:
//
//   isBar = foo => foo.someField == "bar"
//
// Type Conventions
// ================
//
// Exported methods must follow special rules regarding input and output types.
// In particular:
// * All C++ integral and floating-point types may be used, and will be mapped
//   to and from the corresponding Evlan types (Boolean, Integer, and Double).
// * string, StringPiece, const references thereof, and const char* all map
//   to Strings in Evlan.  Ownership is never transferred.  When converting
//   these types to Evlan values, the contents are copied, so the data need
//   not be long-lived.
// * You may use const pointers or const references to any class that has an
//   EVLAN_EXPORT_TYPE block as well as any subclass of google::protobuf::Message.  In
//   the case of function parameters, ownership of the object is always
//   retained by the caller.  In the case of a return type, if the type is a
//   pointer, ownership is passed to the caller.  Otherwise, no ownership is
//   exchanged, but the object must remain valid for the life of the
//   EvlanEvaluator.
// * You may use EvlanValue*, which can represent any arbitrary value.  When
//   used as a parameter, ownership is retained by the caller.  When used as
//   a return type, ownership is passed to the caller.
// * AtomId may be used as a parameter type, but the value is only guaranteed
//   to be valid until the function returns.  AtomId *cannot* be used as a
//   return type.  If you need a long-lasting AtomId or need to return an atom,
//   you can always use EvlanValue* instead.
// * STL vectors of any supported type will be converted to/from Evlan arrays.
//   As with objects, if you return a pointer to a vector, the caller will
//   take ownership, whereas if you return a const reference, the callee
//   keeps ownership.  When used as a parameter type, const references and
//   pointers are both allowed and ownership is never transferred.
//
// Additionally, if the first parameter of a method is of type EvlanEvaluator*,
// the EvlanEvaluator calling the method will be passed in.  In this case,
// Evlan code calling the method does not need to pass a value for this
// parameter.  In other words, the method:
//   int Foo(EvlanEvaluator* evaluator, int i, int j)
// could be called from Evlan like so:
//   obj.foo(1, 2)
//
// When Evlan code attempts to call a C++ method, if the parameters are not of
// the correct types, the call returns a type error.  The C++ method will not
// be called at all in this case.
//
// Methods are currently limited to six parameters, though this could trivially
// be increased if needed.

#ifndef EVLAN_PUBLIC_CPP_GLUE_H__
#define EVLAN_PUBLIC_CPP_GLUE_H__

#include <typeinfo>
#include <unordered_map>
#include <type_traits>
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/public/evaluator.h"

namespace evlan {

// ===================================================================
// THIS FILE CONTAINS ONLY IMPLEMENTATION DETAILS
//
// Users should not have to read this.  Just read the comments at the top
// of this file.

// CppGlue manages glue needed to make a C++ class callable from Evlan.  You
// can register a set of types with it, then convert objects of those types
// to EvlanValues and back.  See the comments at the top of this file.
class CppGlue {
 public:
  // Must be public for scoped_ptr.
  ~CppGlue();

 private:
  // This interface is only to be used by EvlanEvaluator.
  friend class EvlanEvaluator;
  friend class EvlanValue;

  explicit CppGlue(EvlanEvaluator* evaluator);

  // Create a CppGlue for a child (forked) EvlanEvaluator.  When a type is
  // registered in the child, first it searches the parent to see if the type
  // appears there and uses that version if so.  Otherwile, the type's glue
  // is created and owned by the child.  This prevents CppGlue in two forked
  // evaluators from interfering, and avoids allocating new atoms in an
  // evaluator after it has been forked.
  CppGlue(CppGlue* parent, EvlanEvaluator* evaluator);

  // Register a type which you want to be callable from Evlan.  You must call
  // this before calling WrapObject().
  template <typename Class>
  void Register();

  // Convert a C++ object to an EvlanValue, so that it can be called from
  // Evlan code.  This takes ownership of the object and will delete it when
  // it is no longer reachable.
  template <typename Class>
  EvlanValue* WrapObject(EvlanEvaluator* evaluator, const Class* object);

  // Like WrapObject(), but does not take ownership of the object.  The object
  // must remain valid for the life of the EvlanEvaluator.
  template <typename Class>
  EvlanValue* WrapObjectReference(EvlanEvaluator* evaluator,
                                  const Class& object);

  // Converts an EvlanValue back to a C++ object.  Returns NULL if the
  // EvlanValue does not represent an object of the expected type.  The
  // object remains property of the EvlanEvaluator.
  template <typename Class>
  const Class* UnwrapObject(EvlanValue* value);

  // =================================================================
  // Implementation details follow.

  // Contains information needed to call a class.  The CppGlue constructs
  // exactly one ClassGlue per exported type (not per object).
  //
  // This is public only so that it can be used by macros; users should treat
  // the ClassGlue type as a private implementation detail.
 public:
  class ClassGlue;
 private:

  // Helper that removes all modifiers from a type, like pointers, references,
  // const, volatile, etc.
  template <typename T> struct remove_modifiers { typedef T type; };
  template <typename T> struct remove_modifiers<T*>
    : public remove_modifiers<T> {};
  template <typename T> struct remove_modifiers<T&>
    : public remove_modifiers<T> {};
  template <typename T> struct remove_modifiers<const T>
    : public remove_modifiers<T> {};
  template <typename T> struct remove_modifiers<volatile T>
    : public remove_modifiers<T> {};
  template <typename T> struct remove_modifiers<vector<T> >
    : public remove_modifiers<T> {};

  // Contains information needed to call a method.  The CppGlue constructs
  // exactly one MethodGlue per exported method.
  class MethodGlue;

  // This header declares a ton of specializations of MethodGlue and
  // corresponding overloads of NewMethodGlue() which construct them.
  // The header is generated by the script generate_method_glue.py.
  #include "evlan/public/method_glue_decls.h"

  // Information about an exported class.  There is exactly one TypeInfoImpl
  // instance per exported class program-wide (not per CppGlue).
  class TypeInfo;
  template <typename Class> class TypeInfoImpl;

  // A bound object callable from Evlan.  Implements CustomValue.
  class CustomObject;

  // A bound method callable from Evlan.  Implements CustomValue.
  class CustomMethod;

  // -----------------------------------------------------------------
  // Member variables.

  const CppGlue* parent_;
  EvlanEvaluator* evaluator_;

  typedef unordered_map<const TypeInfo*, ClassGlue*> TypeMap;
  TypeMap type_map_;

  // -----------------------------------------------------------------
  // Random helpers.

  // Converts a C++ method name to an Evlan method name (converting it to
  // camel case with a lower-case first letter).
  static string CppNameToEvlanName(const StringPiece& name);

  // Constructs a type error indicating that the wrong number of arguments
  // were passed to a function.
  static EvlanValue* ArityError(
      EvlanEvaluator* evaluator, const TypeInfo* type,
      const StringPiece& method_name, EvlanValue* parameter, int arity);

  // Constructs a type error indicating that a parameter to a method was
  // of the wrong type.
  static EvlanValue* ParameterTypeError(
      EvlanEvaluator* evaluator, const TypeInfo* type,
      const StringPiece& method_name, EvlanValue* parameter,
      const string& type_name, int index);

  // Gets the human-readable name of a C++ type using RTTI.  Intended for
  // error messages.
  template <typename Type>
  static string GetTypeName() {
    return GetTypeName(typeid(Type));
  }

  // Helper for above.
  static string GetTypeName(const std::type_info& type);

  // -----------------------------------------------------------------
  // Helpers for Register().

  // Like Register<Type>(), but the type can be any type which is allowed
  // as a parameter or return type for an exported method.  For all built-in
  // types and protocol messages, this will end up doing nothing, but for
  // other types (and pointers and references thereof) it will register
  // the type as with Register<Type>() (but with the poniter/reference
  // stripped off).
  template <typename Type>
  void RegisterArbitraryType();

  // Helper for RegisterArbitraryType().  At this point, Type will be stripped
  // of modifiers (pointer, reference, const, volatile).  The parameter
  // indicates if it is a primitive C++ type.
  template <typename Type>
  void RegisterCoreType(std::true_type is_primitive);
  template <typename Type>
  void RegisterCoreType(std::false_type is_primitive);

  // Helper for RegisterCoreType(false_type).  The parameter is a dummy pointer
  // to Type, which we use to detect if it is a protocol message.
  template <typename Type>
  void RegisterDetectMessage(google::protobuf::Message*);
  template <typename Type>
  void RegisterDetectMessage(void*);

  // Helper for RegisterDetectMessage(void*) and Register().
  void RegisterClass(const TypeInfo* type_info);

  // -----------------------------------------------------------------
  // Converting C++ -> Evlan

  // ToEvlanValue() is used to convert return values from exported methods
  // into EvlanValues.  It is overloaded for all possible return types.
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator, int value) const;
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator, double value) const;
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator, bool value) const;
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator,
                           const StringPiece& value) const;
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator,
                           const string& value) const;
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator,
                           const char* value) const;

  // Arrays
  template <typename Class>
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator,
                           vector<Class>* values) const;
  template <typename Class>
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator,
                           const vector<Class>& values) const;

  // Convert arbitrary objects to EvlanValues.  The object may either
  // be a proto2 message or a class registered via Register().  If passed
  // a pointer, then the EvlanEvaluator takes ownership of the object and
  // will delete it when it is no longer reachable.  If passed a const
  // reference, no ownership is exchanged, but the object must outlive the
  // EvlanEvaluator.
  template <typename Class>
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator,
                           Class* object) const;
  template <typename Class>
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator,
                           const Class* object) const;

  template <typename Class>
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator,
                           const Class& object) const;

  // "Convert" an EvlanValue to an EvlanValue.  This actually just returns
  // |value| verbatim.  It's here so that custom methods can return EvlanValues.
  EvlanValue* ToEvlanValue(EvlanEvaluator* evaluator, EvlanValue* value) const;

  // Helper for the pointer version of ToEvlanValue().
  template <typename Class>
  EvlanValue* AdoptObject(EvlanEvaluator* evaluator,
                          const google::protobuf::Message* object) const;
  template <typename Class>
  EvlanValue* AdoptObject(EvlanEvaluator* evaluator,
                          const void* object) const;

  // Helper for the const reference version of ToEvlanValue().
  template <typename Class>
  EvlanValue* ReferenceObject(EvlanEvaluator* evaluator,
                              const google::protobuf::Message* object) const;
  template <typename Class>
  EvlanValue* ReferenceObject(EvlanEvaluator* evaluator,
                              const void* object) const;

  // Helper for AdoptObject() and ReferenceObject() when the object is not
  // a protocol message.
  EvlanValue* WrapObject(EvlanEvaluator* evaluator,
                         const TypeInfo* type,
                         const void* object,
                         bool adopt) const;

  // -----------------------------------------------------------------
  // Converting Evlan -> C++

  template <typename Type>
  class ParameterTranslator;
  template <typename Type, bool is_protobuf>
  class ObjectTranslator;
};

// Implementation note:  We template on the "Self" type because in order to
// take the address of a method, we need to provide the method's qualified
// name (e.g. ClassName::MethodName).  With the "Self" type, we can simply
// write "Self::MethodName".  Another option would have been to make the
// EVLAN_EXPORT_METHOD macro take the type name as a parameter in addition to
// the method name, but that would have been far less cool.  See
// CppGlue::TypeInfo::NewClassGlue() to see how this is called.
#define EVLAN_EXPORT_TYPE(NAME)                                                \
  template <typename Self>                                                     \
  inline void evlan_cpp_glue_ExportMethods(                                    \
      NAME*,                                                                   \
      Self*,                                                                   \
      ::evlan::CppGlue* cpp_glue,                                              \
      ::evlan::EvlanEvaluator* evaluator,                                      \
      ::evlan::CppGlue::ClassGlue* class_glue)

#define EVLAN_EXPORT_METHOD(NAME)                                              \
  class_glue->AddMethod(cpp_glue, evaluator, #NAME, &Self::NAME)

#define EVLAN_EXPORT_FIELD(NAME)                                               \
  class_glue->AddField(cpp_glue, evaluator, #NAME, &Self::NAME)

#define EVLAN_EXPORT_FRIEND(NAME)                                              \
  template <typename Self>                                                     \
  friend inline void evlan_cpp_glue_ExportMethods(                             \
      NAME*,                                                                   \
      Self*,                                                                   \
      ::evlan::CppGlue* cpp_glue,                                              \
      ::evlan::EvlanEvaluator* evaluator,                                      \
      ::evlan::CppGlue::ClassGlue* class_glue);

// ===================================================================

class CppGlue::MethodGlue {
 public:
  MethodGlue(const CppGlue* cpp_glue,
             EvlanEvaluator* evaluator,
             const StringPiece& name)
    : cpp_glue_(cpp_glue),
      original_name_(name) {
    scoped_ptr<EvlanValue> name_atom(
      evaluator->NewAtom(CppNameToEvlanName(name)));
    GOOGLE_CHECK(name_atom->GetPermanentAtomValue(&name_atom_id_));
  }

  virtual ~MethodGlue() {}

  virtual EvlanValue* Call(EvlanEvaluator* evaluator,
                           const void* object,
                           EvlanValue* parameter) = 0;

  // Get the atom representing the Evlan-style method name.
  AtomId GetNameAtomId() { return name_atom_id_; }

  // Get the C++ name (prior to conversion to Evlan style).
  const StringPiece& GetOriginalName() { return original_name_; }

 protected:
  const CppGlue* cpp_glue_;
  StringPiece original_name_;
  AtomId name_atom_id_;
};

// -------------------------------------------------------------------

class CppGlue::ClassGlue {
 public:
  ClassGlue(const TypeInfo* type_info);
  ~ClassGlue();

  template <typename Method>
  inline void AddMethod(CppGlue* cpp_glue,
                        EvlanEvaluator* evaluator,
                        const StringPiece& name,
                        Method method) {
    AddMethod(NewMethodGlue(cpp_glue, evaluator, name, method));
  }

  template <typename Class, typename Type>
  inline void AddField(CppGlue* cpp_glue,
                       EvlanEvaluator* evaluator,
                       const StringPiece& name,
                       Type (Class::*accessor)() const) {
    AddField(NewMethodGlue(cpp_glue, evaluator, name, accessor));
  }

 private:
  friend class CppGlue::CustomObject;
  friend class CppGlue::CustomMethod;

  void AddMethod(MethodGlue* glue);
  void AddField(MethodGlue* glue);

  const TypeInfo* type_info_;

  // The bool is true for fields, false for methods.
  typedef map<AtomId, pair<bool, MethodGlue*> > MethodMap;
  MethodMap methods_;
};

// -------------------------------------------------------------------

class CppGlue::TypeInfo {
 public:
  virtual ~TypeInfo();

  // Get the human-readable C++ class name, for debugging / error messages.
  virtual string GetName() const = 0;

  // Construct a new ClassGlue for the type.
  virtual ClassGlue* NewClassGlue(CppGlue* cpp_glue,
                                  EvlanEvaluator* evaluator) const = 0;

  // Delete an object of this type.
  virtual void Delete(const void* object) const = 0;
};

template <typename Class>
class CppGlue::TypeInfoImpl : public CppGlue::TypeInfo {
 public:
  TypeInfoImpl() {}
  virtual ~TypeInfoImpl() {}

  static inline const TypeInfoImpl* GetSingleton() { return &kSingleton; }

  virtual string GetName() const {
    return GetTypeName<Class>();
  }

  virtual ClassGlue* NewClassGlue(CppGlue* cpp_glue,
                                  EvlanEvaluator* evaluator) const {
    Class* dummy = NULL;
    ClassGlue* result = new ClassGlue(this);
    // evlan_cpp_glue_ExportMethods is declared by the EVLAN_EXPORT_TYPE macro.
    // The first "dummy" argument here is to trigger "Koenig lookup":  C++ will
    // search the namespace where Class is defined for possible overloads of
    // this function.  The second "dummy" argument is passed only to initialize
    // the "Self" type, which is needed by the macro magic.
    evlan_cpp_glue_ExportMethods(dummy, dummy, cpp_glue, evaluator, result);
    return result;
  }

  virtual void Delete(const void* object) const {
    delete reinterpret_cast<const Class*>(object);
  }

 private:
  static TypeInfoImpl kSingleton;
};

template <typename Class>
CppGlue::TypeInfoImpl<Class> CppGlue::TypeInfoImpl<Class>::kSingleton;

// -------------------------------------------------------------------

class CppGlue::CustomObject : public CustomValue {
 public:
  CustomObject(ClassGlue* class_glue, const void* object, bool adopt);
  virtual ~CustomObject();

  template <typename Type>
  const Type* GetObjectAs() {
    if (TypeInfoImpl<Type>::GetSingleton() == class_glue_->type_info_) {
      return reinterpret_cast<const Type*>(object_);
    } else {
      return NULL;
    }
  }

  // implements CustomValue ------------------------------------------
  virtual EvlanValue* Call(EvlanEvaluator* evaluator,
                           EvlanValue* self,
                           EvlanValue* parameter);
  virtual string DebugString(int depth);

 private:
  friend class CustomMethod;

  ClassGlue* class_glue_;
  const void* object_;
  bool owned_;
};

class CppGlue::CustomMethod : public CustomValue {
 public:
  CustomMethod(EvlanValue* object_value, CustomObject* custom_object,
               MethodGlue* method_glue);
  virtual ~CustomMethod();

  // implements CustomValue ------------------------------------------
  virtual EvlanValue* Call(EvlanEvaluator* evaluator,
                           EvlanValue* self,
                           EvlanValue* parameter);
  virtual string DebugString(int depth);

 private:
  scoped_ptr<EvlanValue> object_value_;
  CustomObject* custom_object_;
  MethodGlue* method_glue_;
};

// -------------------------------------------------------------------
// Register()

template <typename Class>
inline void CppGlue::Register() {
  RegisterClass(TypeInfoImpl<Class>::GetSingleton());
}

template <typename Type>
inline void CppGlue::RegisterArbitraryType() {
  typedef typename remove_modifiers<Type>::type CoreType;
  typedef std::integral_constant<bool,
      std::is_integral<CoreType>::value ||
      std::is_floating_point<CoreType>::value> is_primitive;
  RegisterCoreType<CoreType>(is_primitive());
}

template <typename Type>
inline void CppGlue::RegisterCoreType(std::true_type is_primitive) {
  // This is a primitive type.  No registration necessary.
}

template <typename Type>
inline void CppGlue::RegisterCoreType(std::false_type is_primitive) {
  // This is not a primitive type.
  // We need to detect if this is a protocol message type or a normal class.
  Type* dummy = NULL;
  RegisterDetectMessage<Type>(dummy);
}

template <>
inline void CppGlue::RegisterCoreType<string>(std::false_type is_primitive) {}
template <>
inline void CppGlue::RegisterCoreType<StringPiece>(
    std::false_type is_primitive) {}
template <>
inline void CppGlue::RegisterCoreType<EvlanValue>(
    std::false_type is_primitive) {}
template <>
inline void CppGlue::RegisterCoreType<AtomId>(
    std::false_type is_primitive) {}

template <typename Type>
inline void CppGlue::RegisterDetectMessage(google::protobuf::Message*) {
  // Protocol message type, so do nothing.
}

template <typename Type>
inline void CppGlue::RegisterDetectMessage(void*) {
  // Not a protocol message.
  RegisterClass(TypeInfoImpl<Type>::GetSingleton());
}

// -------------------------------------------------------------------
// Converting C++ -> Evlan

template <typename Class>
inline EvlanValue* CppGlue::WrapObject(EvlanEvaluator* evaluator,
                                       const Class* object) {
  return WrapObject(evaluator, TypeInfoImpl<Class>::GetSingleton(),
                    reinterpret_cast<const void*>(object), true);
}

template <typename Class>
inline EvlanValue* CppGlue::WrapObjectReference(EvlanEvaluator* evaluator,
                                                const Class& object) {
  return WrapObject(evaluator, TypeInfoImpl<Class>::GetSingleton(),
                    reinterpret_cast<const void*>(&object), false);
}

inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         int value) const {
  return evaluator->NewInteger(value);
}

inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         double value) const {
  return evaluator->NewDouble(value);
}

inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         bool value) const {
  return evaluator->NewBoolean(value);
}

inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         const StringPiece& value) const {
  return evaluator->NewString(value);
}

inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         const string& value) const {
  return evaluator->NewString(value);
}

inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         const char* value) const {
  return evaluator->NewString(value);
}

template <typename Class>
EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                  vector<Class>* values) const {
  scoped_ptr<vector<Class> > delete_me(values);
  return ToEvlanValue(evaluator, *values);
}

template <typename Class>
EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                  const vector<Class>& values) const {
  vector<EvlanValue*> evlan_values(values.size());
  for (int i = 0; i < values.size(); i++) {
    evlan_values[i] = ToEvlanValue(evaluator, values[i]);
  }
  EvlanValue* result = evaluator->NewArray(evlan_values);
  for (int i = 0; i < values.size(); i++) {
    delete evlan_values[i];
  }
  return result;
}

template <typename Class>
inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         Class* object) const {
  return AdoptObject<Class>(evaluator, object);
}

template <typename Class>
inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         const Class* object) const {
  return AdoptObject<Class>(evaluator, object);
}

template <typename Class>
inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         const Class& object) const {
  return ReferenceObject<Class>(evaluator, &object);
}

inline EvlanValue* CppGlue::ToEvlanValue(EvlanEvaluator* evaluator,
                                         EvlanValue* value) const {
  return value;
}

template <typename Class>
inline EvlanValue* CppGlue::AdoptObject(
    EvlanEvaluator* evaluator,
    const google::protobuf::Message* object) const {
  return evaluator->NewProtobuf(object);
}

template <typename Class>
inline EvlanValue* CppGlue::AdoptObject(
    EvlanEvaluator* evaluator,
    const void* object) const {
  return WrapObject(evaluator, TypeInfoImpl<Class>::GetSingleton(),
                    reinterpret_cast<const void*>(object), true);
}

template <typename Class>
inline EvlanValue* CppGlue::ReferenceObject(
    EvlanEvaluator* evaluator,
    const google::protobuf::Message* object) const {
  return evaluator->NewProtobufReference(*object);
}

template <typename Class>
inline EvlanValue* CppGlue::ReferenceObject(
    EvlanEvaluator* evaluator,
    const void* object) const {
  return WrapObject(evaluator, TypeInfoImpl<Class>::GetSingleton(),
                    reinterpret_cast<const void*>(object), false);
}

// -------------------------------------------------------------------
// Converting Evlan -> C++

template <>
class CppGlue::ParameterTranslator<bool> {
 public:
  bool Translate(EvlanValue* value) {
    return value->GetBooleanValue(&value_);
  }

  inline bool Get() { return value_; }

 private:
  bool value_;
};

template <>
class CppGlue::ParameterTranslator<int> {
 public:
  bool Translate(EvlanValue* value) {
    return value->GetIntegerValue(&value_);
  }

  inline int Get() { return value_; }

 private:
  int value_;
};

template <>
class CppGlue::ParameterTranslator<double> {
 public:
  bool Translate(EvlanValue* value) {
    return value->GetDoubleValue(&value_);
  }

  inline double Get() { return value_; }

 private:
  double value_;
};

// TODO(kenton):  Other primitive types.

template <>
class CppGlue::ParameterTranslator<StringPiece> {
 public:
  bool Translate(EvlanValue* value) {
    return value->GetStringValue(&value_);
  }

  inline const StringPiece& Get() { return value_; }

 private:
  StringPiece value_;
};

template <>
class CppGlue::ParameterTranslator<string> {
 public:
  bool Translate(EvlanValue* value) {
    return value->GetStringValue(&value_);
  }

  inline string Get() { return value_.as_string(); }

 private:
  StringPiece value_;
};

template <>
class CppGlue::ParameterTranslator<const StringPiece&>
  : public CppGlue::ParameterTranslator<StringPiece> {};

template <>
class CppGlue::ParameterTranslator<const string&> {
 public:
  bool Translate(EvlanValue* value) {
    StringPiece temp;
    if (!value->GetStringValue(&temp)) return false;
    temp.CopyToString(&value_);
    return true;
  }

  inline const string& Get() { return value_; }

 private:
  string value_;
};

template <>
class CppGlue::ParameterTranslator<const char*> {
 public:
  bool Translate(EvlanValue* value) {
    // We have to make a copy because we need to null-terminate it.
    StringPiece temp;
    if (!value->GetStringValue(&temp)) return false;
    temp.CopyToString(&value_);
    return true;
  }

  inline const char* Get() { return value_.c_str(); }

 private:
  string value_;
};

template <>
class CppGlue::ParameterTranslator<AtomId> {
 public:
  bool Translate(EvlanValue* value) {
    return value->GetAtomValue(&value_);
  }

  inline AtomId Get() { return value_; }

 private:
  AtomId value_;
};

template <>
class CppGlue::ParameterTranslator<const AtomId&>
  : public ParameterTranslator<AtomId> {};

template <>
class CppGlue::ParameterTranslator<EvlanValue*> {
 public:
  bool Translate(EvlanValue* value) {
    value_ = value;
    return true;
  }

  inline EvlanValue* Get() { return value_; }

 private:
  EvlanValue* value_;
};

template <typename Type>
class CppGlue::ObjectTranslator<Type, false> {
 public:
  bool Translate(EvlanValue* value) {
    return value->GetCppObjectValue(&object_);
  }

  inline const Type* Get() { return object_; }

 private:
  const Type* object_;
};

template <typename Type>
class CppGlue::ObjectTranslator<Type, true> {
 public:
  bool Translate(EvlanValue* value) {
    const google::protobuf::Message* generic_message;
    if (!value->GetProtobufValue(&generic_message)) return false;

    message_ = dynamic_cast<const Type*>(generic_message);
    if (message_ == NULL) return false;

    return true;
  }

  inline const Type* Get() { return message_; }

 private:
  const Type* message_;
};

template <>
class CppGlue::ObjectTranslator<google::protobuf::Message, true> {
 public:
  bool Translate(EvlanValue* value) {
    return value->GetProtobufValue(&message_);
  }

  inline const google::protobuf::Message* Get() { return message_; }

 private:
  const google::protobuf::Message* message_;
};

template <typename Type>
class CppGlue::ObjectTranslator<vector<Type>, false> {
 public:
  ~ObjectTranslator() {
    for (int i = 0; i < evlan_values_.size(); i++) {
      delete evlan_values_[i];
    }
  }

  inline bool Translate(EvlanValue* value) {
    if (!value->GetArrayElements(&evlan_values_)) return false;
    element_translators_.reset(
      new ParameterTranslator<Type>[evlan_values_.size()]);
    final_values_.resize(evlan_values_.size());
    for (int i = 0; i < evlan_values_.size(); i++) {
      if (!element_translators_[i].Translate(evlan_values_[i])) return false;
      final_values_[i] = element_translators_[i].Get();
    }
    return true;
  }

  inline const vector<Type>* Get() { return &final_values_; }

 private:
  vector<EvlanValue*> evlan_values_;
  scoped_array<ParameterTranslator<Type> > element_translators_;
  vector<Type> final_values_;
};

template <typename Type>
class CppGlue::ParameterTranslator<const Type*>
  : public ObjectTranslator<Type,
      std::is_convertible<Type*, google::protobuf::Message*>::value> {};

template <typename Type>
class CppGlue::ParameterTranslator<const Type&> {
 public:
  inline bool Translate(EvlanValue* value) {
    return pointer_translator_.Translate(value);
  }

  inline const Type& Get() { return *pointer_translator_.Get(); }

 private:
  ParameterTranslator<const Type*> pointer_translator_;
};

template <typename Class>
const Class* CppGlue::UnwrapObject(EvlanValue* value) {
  ParameterTranslator<const Class*> translator;
  if (!translator.Translate(value)) return NULL;
  return translator.Get();
}

#include "evlan/public/method_glue_defs.h"
#include "evlan/stringpiece.h"

// ===================================================================

template<typename Class>
bool EvlanValue::GetCppObjectValue(const Class** value) {
  CustomValue* custom_value;
  if (!GetCustomValue(&custom_value)) return false;

  CppGlue::CustomObject* custom_object =
    dynamic_cast<CppGlue::CustomObject*>(custom_value);
  if (custom_object == NULL) return false;

  *value = custom_object->GetObjectAs<Class>();
  if (*value == NULL) return false;

  return true;
}

template<typename Class>
EvlanValue* EvlanEvaluator::NewCppObject(const Class* object) {
  cpp_glue_->Register<Class>();  // Make sure the type is registered.
  return cpp_glue_->WrapObject(this, object);
}

template<typename Class>
EvlanValue* EvlanEvaluator::NewCppObjectReference(const Class& object) {
  cpp_glue_->Register<Class>();  // Make sure the type is registered.
  return cpp_glue_->WrapObjectReference(this, object);
}

template<typename Class>
void EvlanEvaluator::PreRegisterCppObjectType() {
  cpp_glue_->Register<Class>();
}

}  // namespace evlan

#endif  // EVLAN_PUBLIC_CPP_GLUE_H__
