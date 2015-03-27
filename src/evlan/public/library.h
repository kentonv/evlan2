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
// This file defines a mechanism for defining Evlan libraries in C++.  Each
// library exposes a singleton implementation of the evlan::Library interface,
// which allows it to be loaded at runtime and to declare dependencies on
// other libraries.  The evlanc.ekam-rule build rule automatically generates
// such singleton implementations, but you can also implement libraries in
// C++ in order to provide access to native code.
//
// Using the evlanc.ekam-rule build rule
// =====================================
//
// Any file foo.evlan will be parsed and three files will be generated:
//   foo.evmod - A serialized evlan::Module (protobuf).
//   foo.{h,cc} - Defines a singleton evlan::Library representing the module.
//     The raw module will be embedded directly into the code, so that it does
//     not need to be loaded from disk and runtime.
//
// Defining Evlan libraries in C++
// ===============================
//
// You can write an Evlan library in C++, e.g. to allow Evlan code to use
// functionality implemented in C++.  To do this, you must follow certain
// rules in order to allow your library to be imported from Evlan.  In
// particular:
// * You must define a header file whose name is the desired module name
//   suffixed with ".h".
// * Your header file must define a function which returns a singleton
//   evlan::Library*.  The function name should be constructed by taking the
//   library's full name (including containing directory), converting each
//   component to capitalized-camel-case (e.g. "foo_bar" becomes "FooBar"),
//   and converting all slashes to underscores.
//
// For example, if the module is "my_project/some_dir/my_library", then
// the header should be "my_project/some_dir/my_library.h", and it should
// define a function:
//   Library* MyProject_SomeDir_MyLibrary();
// Notice that each component of the path is converted to camel-case, and then
// the slashes are replaced with underscores.  This function should not be
// inside any namespace.  It should always return exactly the same pointer.
//
// For an example, see evlan/api/unittest.h.

#ifndef EVLAN_PUBLIC_LIBRARY_H__
#define EVLAN_PUBLIC_LIBRARY_H__

#include <unordered_map>
#include <map>
#include <string>
#include <vector>

#include "evlan/common.h"
#include "evlan/public/import_resolver.h"
#include "evlan/stringpiece.h"

namespace proto2 {
  namespace io {
    class ZeroCopyInputStream;
  }
}

namespace evlan {

class EvlanValue;
class EvlanEvaluator;
class ImportResolver;

// Defined in this file.
class Library;
class LibraryLoader;
class Directory;
class DiskDirectory;

// All Evlan libraries implement this interface.
class Library {
 public:
  virtual ~Library();

  // The library label, i.e. the name under which the library may be imported.
  virtual StringPiece GetLabel() = 0;

  // Fill in *output with pointers to all libraries this library depends on.
  // These should point at singletons, such that if two independent libraries A
  // and B depend on a common library C, then their GetDependencies() methods
  // will both use the same pointer to refer to C.
  virtual void GetDependencies(vector<Library*>* output) = 0;

  // Use the given evaluator to construct an EvlanValue for this library.
  // |dependencies| contains EvlanValues representing all of this library's
  // dependencies, in the same order as they are returned by GetDependencies().
  // The evaluator and EvlanValues remain property of the caller.
  //
  // Load() should not do any I/O, including reading from disk.  All code should
  // be compiled directly into the binary.  Input files should be passed to
  // the library via the Evlan API that it exposes.
  virtual EvlanValue* Load(EvlanEvaluator* evaluator,
                           const vector<EvlanValue*>& dependencies) = 0;
};

// Encapsulates the process of loading a set of libraries and their
// dependencies into the Evlan VM.  LibraryLoader will make sure that no
// single library is loaded twice, even when multiple libraries depend on it.
class LibraryLoader {
 public:
  // Constructs a LibraryLoader that will load libraries using the given
  // evaluator.  The evaluator remains property of the caller.  It must remain
  // valid for the life of the LibraryLoader.
  LibraryLoader(EvlanEvaluator* evaluator);

  ~LibraryLoader();

  // Load the given library and return an EvlanValue representing it.  The
  // returned value becomes property of the caler.
  EvlanValue* Load(Library* library);

 private:
  EvlanEvaluator* evaluator_;
  typedef unordered_map<Library*, EvlanValue*> LibraryMap;
  LibraryMap loaded_libraries_;

  // Returns NULL if a cyclic dependency is detected.
  EvlanValue* LoadInternal(Library* library);

  DISALLOW_COPY_AND_ASSIGN(LibraryLoader);
};

}  // namespace evlan

#endif  // EVLAN_PUBLIC_LIBRARY_H__
