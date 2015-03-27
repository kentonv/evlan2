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

#include "evlan/testing/test_util.h"
#include "evlan/parser/parser.h"
#include "evlan/vm/builtin/atom.h"
#include "evlan/vm/builtin/boolean.h"
#include "evlan/vm/builtin/builtin.h"
#include "evlan/vm/builtin/bytes.h"
#include "evlan/vm/builtin/double.h"
#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/support.h"
#include "evlan/vm/builtin/type_error.h"
#include "evlan/vm/interpreter/interpreter.h"
#include "evlan/vm/memory/allocator.h"
#include "evlan/vm/memory/braindead_memory_manager.h"
#include "evlan/vm/memory/debug_memory_manager.h"
#include <google/protobuf/io/printer.h>
#include "evlan/strutil.h"
#include <google/protobuf/testing/file.h>
#include <google/protobuf/testing/googletest.h>

namespace evlan {

using google::protobuf::File;
using namespace std::placeholders;

// A TrackedObject which stores a value.  We use AddExternalReference() on the
// object's tracker to prevent the MemoryManager from reclaiming this value.
// We use KeepaliveValues to retain values passed to AddImport().
class EvlanTest::KeepaliveValue : public vm::memory::TrackedObject {
 public:
  KeepaliveValue(vm::Value value);
  ~KeepaliveValue();

  vm::Value value_;

  // implements TrackedObject --------------------------------------
  void NotReachable();
  void Accept(vm::memory::MemoryVisitor* visitor) const;
};

EvlanTest::KeepaliveValue::KeepaliveValue(vm::Value value) : value_(value) {}
EvlanTest::KeepaliveValue::~KeepaliveValue() {}

void EvlanTest::KeepaliveValue::NotReachable() {
  delete this;
}
void EvlanTest::KeepaliveValue::Accept(
    vm::memory::MemoryVisitor* visitor) const {
  value_.Accept(visitor);
}

// ===================================================================

// Abstract interface for an object that checks if the result of evaluating
// some code meets expectations.
class EvlanTest::ResultChecker {
 public:
  virtual ~ResultChecker() {}

  virtual void Check(const vm::Value& result) = 0;
};

// ===================================================================

class EvlanTest::ContextFactoryImpl : public vm::ContextFactory {
 public:
  ContextFactoryImpl(EvlanTest* test)
      : test_(test), refcount_(0), context_exists_(false) {}

  virtual ~ContextFactoryImpl() {
    EXPECT_EQ(refcount_, 0);
  }

  void AddOriginalReference() {
    ++refcount_;
  }

  // implements ContextFactory ---------------------------------------

  virtual void AddReference() {
    EXPECT_TRUE(context_exists_ || refcount_ > 0)
        << "AddReference() called on dead factory.";
    ++refcount_;
  }

  virtual void RemoveReference() {
    ASSERT_GT(refcount_, 0);
    --refcount_;
  }

  virtual vm::Context::Environment NewContext() {
    single_context_lock_.Lock();
    context_exists_ = true;
    vm::Context::Environment result;
    result.memory_manager = test_->memory_manager_.get();
    result.atom_registry = test_->atom_registry_.get();
    result.support_code_manager = test_->support_code_manager_.get();
    return result;
  }

  virtual void ReleaseContext(const vm::Context::Environment& environment) {
    EXPECT_TRUE(context_exists_);
    context_exists_ = false;
    single_context_lock_.Unlock();
  }

 private:
  EvlanTest* const test_;
  int refcount_;

  bool context_exists_;  // Effectively counts as one extra reference when true.

  // We lock this mutex when we create our one context, then unlock it when
  // done.  This at least allows asynchronous code to be tested, even if only
  // one context can actually be running at a time.
  Mutex single_context_lock_;
};

// ===================================================================

EvlanTest::EvlanTest()
  : memory_manager_(new vm::memory::DebugMemoryManager),
    atom_registry_(new vm::builtin::AtomRegistry),
    support_code_manager_(new vm::builtin::SupportCodeManager),
    context_factory_(new ContextFactoryImpl(this)),
    support_code_set_(false) {
}

EvlanTest::~EvlanTest() {}

void EvlanTest::UseFastMemoryManager() {
  memory_allocator_.reset(new vm::memory::MallocAllocator);
  memory_manager_.reset(
    new vm::memory::BraindeadMemoryManager(memory_allocator_.get()));
}

vm::memory::MemoryRoot* EvlanTest::GetMemoryRoot() {
  // At present, we don't actually need this, since we use
  // AddExternalReference() to keep all our TrackedObjects alive.

  class DummyMemoryRoot : public vm::memory::MemoryRoot {
   public:
    DummyMemoryRoot() {}
    ~DummyMemoryRoot() {}

    // implements MemoryRoot -----------------------------------------
    void Accept(vm::memory::MemoryVisitor* visitor) const {
      // Nothing.
    }
  };

  static DummyMemoryRoot dummy;
  return &dummy;
}

vm::ContextFactory* EvlanTest::GetContextFactory() {
  context_factory_.get()->AddOriginalReference();
  return context_factory_.get();
}

void EvlanTest::AddImport(const StringPiece& name, const vm::Value& value) {
  if (imports_.count(name.as_string()) > 0) {
    ADD_FAILURE() << "Duplicate import definition: " << name;
  } else {
    KeepaliveValue* tracked_object = new KeepaliveValue(value);
    memory_manager_->AddTrackedObject(tracked_object)->AddExternalReference();
    imports_[name.as_string()] = tracked_object;
  }
}

void EvlanTest::SetSupportCode(const StringPiece& code) {
  ASSERT_FALSE(support_code_set_) << "SetSupportCode() was already called.";
  support_code_set_ = true;

  Module support_module;
  parser::Parse(code.as_string(), &support_module);

  vm::Value support_code = ExecuteModule(support_module);

  vm::Context context(context_factory_.get());
  support_code_manager_->Init(&context, support_code);
}

void EvlanTest::LoadStandardSupportCode() {
  string code;
  File::ReadFileToStringOrDie(
      google::protobuf::TestSourceDir() + "/evlan/api/builtinSupport.evlan", &code);
  SetSupportCode(code);
}

vm::Value EvlanTest::RunNative(std::function<void(vm::Context*, vm::Value)> callback) {
  // Callback which records the value passed to it by constructing a
  // KeepaliveValue around it and storing a pointer to the given location.
  class RecordValueCallbackLogic : public vm::Logic {
   public:
    // When the callback is called, a KeepaliveValue will be created and *value
    // will be set to point at it.  Also, the KeepaliveValue will be registered
    // with the MemoryManager and a pointer to the tracker will be stored in
    // *tracker.
    RecordValueCallbackLogic(KeepaliveValue** value,
                             vm::memory::ObjectTracker** tracker,
                             Closure* done)
      : value_(value), tracker_(tracker), done_(done) {}
    virtual ~RecordValueCallbackLogic() {}

    void Accept(const vm::Data* data,
                vm::memory::MemoryVisitor* visitor) const {
      // No data.
    }

    void DebugPrint(vm::Data data, int depth,
                    google::protobuf::io::Printer* printer) const {
      printer->Print("testDoneCallback");
    }

    void Run(vm::Context* context) const {
      *value_ = new KeepaliveValue(context->GetParameter());
      *tracker_ = context->GetMemoryManager()->AddTrackedObject(*value_);
      (*tracker_)->AddExternalReference();
      done_->Run();
    }

   private:
    KeepaliveValue** value_;
    vm::memory::ObjectTracker** tracker_;
    Closure* done_;
  };

  KeepaliveValue* value = NULL;
  vm::memory::ObjectTracker* tracker = NULL;
  RecordValueCallbackLogic callback_logic(&value, &tracker,
                                          GetCompletionCallback());
  vm::Value final_callback(&callback_logic, vm::Data());

  {
    vm::Context context(context_factory_.get());
    callback(&context, final_callback);
    context.Run();
  }

  WaitForCompletion();

  if (value == NULL) {
    ADD_FAILURE() << "Final callback was not called.";
    return vm::Value();
  } else {
    tracker->RemoveExternalReference();
    return value->value_;
  }
}

void EvlanTest::Run(const StringPiece& code, ResultChecker* result_checker) {
  Module module;
  parser::Parse(code.as_string(), &module);
  vm::Value result = ExecuteModule(module);
  result_checker->Check(result);
}

static void RunModule(const Module* module,
                      const map<string, vm::Value>* imports,
                      vm::Context* context, vm::Value callback) {
  vm::interpreter::InterpretModule(context, *module, NULL, *imports, callback);
}

template<typename T>
static inline const T* constify(T* t) { return t; }

vm::Value EvlanTest::ExecuteModule(const Module& module) {
  // This import is always required.
  if (imports_.count("builtin") == 0) {
    AddImport("builtin", vm::builtin::GetBuiltinLibrary());
  }

  map<string, vm::Value> imports;
  for (map<string, KeepaliveValue*>::iterator iter = imports_.begin();
       iter != imports_.end(); ++iter) {
    imports[iter->first] = iter->second->value_;
  }

  return RunNative(std::bind(
      &RunModule, constify(&module), constify(&imports), _1, _2));
}

Closure* EvlanTest::GetCompletionCallback() {
  return NewCallback(&DoNothing);
}

void EvlanTest::WaitForCompletion() {
  // Do nothing.
}

// -------------------------------------------------------------------

void EvlanTest::ExpectEvaluatesToInteger(int value, StringPiece code) {
  class IntegerResultChecker : public ResultChecker {
   public:
    IntegerResultChecker(int expected_value)
      : expected_value_(expected_value) {}
    virtual ~IntegerResultChecker() {}

    virtual void Check(const vm::Value& result) {
      if (!vm::builtin::IsInteger(result) ||
          vm::builtin::GetIntegerValue(result) != expected_value_) {
        ADD_FAILURE()
          << "Final result didn't match.\n"
          << "  Actual: " << result.DebugString() << "\n"
          << "Expected: " << expected_value_;
      }
    }

   private:
    int expected_value_;
  };

  IntegerResultChecker checker(value);

  Run(code, &checker);
}

void EvlanTest::ExpectEvaluatesToDouble(double value, StringPiece code) {
  class DoubleResultChecker : public ResultChecker {
   public:
    DoubleResultChecker(double expected_value)
      : expected_value_(expected_value) {}
    virtual ~DoubleResultChecker() {}

    virtual void Check(const vm::Value& result) {
      if (!vm::builtin::IsDouble(result) ||
          vm::builtin::GetDoubleValue(result) != expected_value_) {
        ADD_FAILURE()
          << "Final result didn't match.\n"
          << "  Actual: " << result.DebugString() << "\n"
          << "Expected: " << expected_value_;
      }
    }

   private:
    double expected_value_;
  };

  DoubleResultChecker checker(value);

  Run(code, &checker);
}

void EvlanTest::ExpectEvaluatesToBoolean(bool value, StringPiece code) {
  class BooleanResultChecker : public ResultChecker {
   public:
    BooleanResultChecker(bool expected_value)
      : expected_value_(expected_value) {}
    virtual ~BooleanResultChecker() {}

    virtual void Check(const vm::Value& result) {
      if (!vm::builtin::BooleanUtils::IsBoolean(result) ||
          vm::builtin::BooleanUtils::GetBooleanValue(result) !=
            expected_value_) {
        ADD_FAILURE()
          << "Final result didn't match.\n"
          << "  Actual: " << result.DebugString() << "\n"
          << "Expected: " << expected_value_;
      }
    }

   private:
    bool expected_value_;
  };

  BooleanResultChecker checker(value);

  Run(code, &checker);
}

void EvlanTest::ExpectEvaluatesToAtom(StringPiece value, StringPiece code) {
  class AtomResultChecker : public ResultChecker {
   public:
    AtomResultChecker(const StringPiece& expected_value)
      : expected_value_(expected_value.as_string()) {}
    virtual ~AtomResultChecker() {}

    virtual void Check(const vm::Value& result) {
      if (!vm::builtin::IsAtom(result) ||
          vm::builtin::GetAtomName(result.GetData()) != expected_value_) {
        ADD_FAILURE()
          << "Final result didn't match.\n"
          << "  Actual: " << result.DebugString() << "\n"
          << "Expected: @" << expected_value_;
      }
    }

   private:
    string expected_value_;
  };

  AtomResultChecker checker(value);

  Run(code, &checker);
}

void EvlanTest::ExpectEvaluatesToString(StringPiece value, StringPiece code) {
  class StringResultChecker : public ResultChecker {
   public:
    StringResultChecker(const StringPiece& expected_value)
      : expected_value_(expected_value.as_string()) {}
    virtual ~StringResultChecker() {}

    virtual void Check(const vm::Value& result) {
      if (!vm::builtin::ByteUtils::IsByteArray(result) ||
          vm::builtin::ByteUtils::ToString(result) != expected_value_) {
        ADD_FAILURE()
          << "Final result didn't match.\n"
          << "  Actual: " << result.DebugString() << "\n"
          << "Expected: \"" << strings::CEscape(expected_value_) << "\"";
      }
    }

   private:
     string expected_value_;
  };

  StringResultChecker checker(value);

  Run(code, &checker);
}

void EvlanTest::ExpectEvaluatesToError(StringPiece value, StringPiece code) {
  class ErrorResultChecker : public ResultChecker {
   public:
    ErrorResultChecker(const StringPiece& expected_description)
      : expected_description_(expected_description.as_string()) {}
    virtual ~ErrorResultChecker() {}

    virtual void Check(const vm::Value& result) {
      if (!vm::builtin::TypeErrorUtils::IsError(result)) {
        ADD_FAILURE()
          << "Final result didn't match.\n"
          << "  Actual: " << result.DebugString() << "\n"
          << "Expected: typeError(\"" << strings::CEscape(expected_description_)
          << "\")";
      } else {
        StringPiece actual_description =
          vm::builtin::TypeErrorUtils::GetDescription(result);
        if(actual_description != expected_description_) {
          ADD_FAILURE()
            << "Error description didn't match.\n"
            << "  Actual: " << actual_description << "\n"
            << "Expected: " << expected_description_;
        }
      }
    }

   private:
    string expected_description_;
  };

  ErrorResultChecker checker(value);

  Run(code, &checker);
}

void EvlanTest::ExpectEvaluatesToDebugString(
    StringPiece text, StringPiece code) {
  ExpectEvaluatesToDebugString(4, text, code);
}

void EvlanTest::ExpectEvaluatesToDebugString(int depth, StringPiece text,
                                             StringPiece code) {
  class DebugStringResultChecker : public ResultChecker {
   public:
    DebugStringResultChecker(int depth, const StringPiece& expected_text)
      : depth_(depth), expected_text_(expected_text.as_string()) {}
    virtual ~DebugStringResultChecker() {}

    virtual void Check(const vm::Value& result) {
      string actual_text = result.DebugString(depth_);
      if (actual_text != expected_text_) {
        ADD_FAILURE()
          << "Debug string didn't match.\n"
          << "  Actual: " << actual_text << "\n"
          << "Expected: " << expected_text_;
      }
    }

   private:
    int depth_;
    string expected_text_;
  };

  DebugStringResultChecker checker(depth, text);

  Run(code, &checker);
}

}  // namespace evlan
