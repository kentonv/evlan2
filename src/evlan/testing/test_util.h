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

#ifndef EVLAN_TESTING_TEST_UTIL_H__
#define EVLAN_TESTING_TEST_UTIL_H__

#include <map>
#include <string>
#include <functional>

#include "evlan/common.h"
#include "evlan/vm/runtime.h"
#include "evlan/proto/module.pb.h"
#include <gtest/gtest.h>

namespace evlan {
  namespace vm {
    namespace memory {
      class Allocator;
    }
  }
}

namespace evlan {

// Base class for tests that execute Evlan code.
class EvlanTest : public testing::Test {
 protected:
  EvlanTest();
  virtual ~EvlanTest();

  // Get the test's MemoryManager.
  inline vm::memory::MemoryManager* GetMemoryManager();

  // Get a dummy MemoryRoot.  The MemoryRoot doesn't actually contain anything;
  // this is just a convenience method so that you don't have to create your
  // own MemoryRoot.
  vm::memory::MemoryRoot* GetMemoryRoot();

  // Get a ContextFactory that can be used by this test.  This factory cannot
  // be used to construct more than one context at a time.  Note that the
  // factory's reference count is incremented before GetContextFactory()
  // returns, so the caller must call RemoveReference() at some point.
  vm::ContextFactory* GetContextFactory();

  // -----------------------------------------------------------------
  // Setup:  These are typically called during the test's SetUp() method or
  // early in a test.

  // By default, a DebugMemoryManager is used for memory management.  This
  // may be too slow for evaluating complex code, in which case you may call
  // UseFastMemoryManager() to use faster memory management instead.
  void UseFastMemoryManager();

  // Allow code set with SetCode() to import the given value.
  void AddImport(const StringPiece& name, const vm::Value& value);

  // If support code is needed (see evlan/vm/builtin/support.h), then one of
  // these should be called.  LoadStandardSupportCode() uses
  // evlan/api/builtinSupport.evlan as the support code.  Note that because
  // the standard support code is pretty large, you should probably always call
  // UseFastMemoryManager() before loading it.
  void SetSupportCode(const StringPiece& code);
  void LoadStandardSupportCode();

  // -----------------------------------------------------------------
  // Expectations

  // Evaluates the given code and expects the result to be the given value.
  // TODO(kenton):  Maybe these should be designed to work with
  //   EXPECT_PRED_FORMAT?
  void ExpectEvaluatesToInteger(int         value, StringPiece code);
  void ExpectEvaluatesToDouble (double      value, StringPiece code);
  void ExpectEvaluatesToBoolean(bool        value, StringPiece code);
  void ExpectEvaluatesToAtom   (StringPiece value, StringPiece code);
  void ExpectEvaluatesToString (StringPiece value, StringPiece code);
  void ExpectEvaluatesToError  (StringPiece value, StringPiece code);

  // Expect that the resulting value has the given debug string.  Generally,
  // this should only be used to test a PrintDebugString() implementation --
  // you should never write a test which makes assumptions about the debug
  // strings printed by code other than the code being tested.  The debug
  // string will be printed to the given depth.
  void ExpectEvaluatesToDebugString(StringPiece text, StringPiece code);
  void ExpectEvaluatesToDebugString(int depth, StringPiece text,
                                    StringPiece code);

  // -----------------------------------------------------------------
  // Other utilities

  // Runs the given callback and returns the result.  The parameters to the
  // callback are a Context and a continuation Value.  The callback is expected
  // to call Context::SetNext(), and looping the context is expected to
  // eventually cause the continuation to be called with the result, which is
  // then returned from RunNative().
  vm::Value RunNative(std::function<void(vm::Context*, vm::Value)> callback);

  // Evaluates the given module, returning the Value passed to the final
  // callback.
  vm::Value ExecuteModule(const Module& module);

  // Returns a callback that, when called, causes WaitForCompletion() to return.
  // Must be called before calling WaitForCompletion().  If the callback is
  // called before WaitForCompletion() is called, then WaitForCompletion()
  // should return immediately.  The default implementation does not support
  // asynchronous completion, so WaitForCompletion() always returns immediately,
  // but tests which run code that may complete asynchronously should override
  // these.
  virtual Closure* GetCompletionCallback();

  // Waits for the last callback returned by GetCompletionCallback() to be
  // called, then returns.
  virtual void WaitForCompletion();

 private:
  class KeepaliveValue;
  class ResultChecker;
  class ContextFactoryImpl;

  scoped_ptr<vm::memory::Allocator> memory_allocator_;
  scoped_ptr<vm::memory::MemoryManager> memory_manager_;
  scoped_ptr<vm::builtin::AtomRegistry> atom_registry_;
  scoped_ptr<vm::builtin::SupportCodeManager> support_code_manager_;
  scoped_ptr<ContextFactoryImpl> context_factory_;
  map<string, KeepaliveValue*> imports_;

  bool support_code_set_;

  // Evaluates the given code and uses result_checker to verify the final
  // result.
  void Run(const StringPiece& code, ResultChecker* result_checker);
};

// ===================================================================
// implementation details

inline vm::memory::MemoryManager* EvlanTest::GetMemoryManager() {
  return memory_manager_.get();
}

}  // namespace evlan

#endif  // EVLAN_TEST_UTIL_H__
