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

#include "evlan/vm/builtin/integer.h"
#include "evlan/vm/builtin/native.h"
#include "evlan/vm/builtin/support.h"
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/common.h"
#include <google/protobuf/io/printer.h>
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace builtin {

class IntegerLogic : public Logic {
 public:
  IntegerLogic() {}
  ~IntegerLogic() {}

  static const IntegerLogic kIntegerLogic;

  // implements Logic ------------------------------------------------
  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    // Data is just an integer, so nothing to do here.
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    printer->Print("$value$", "value", SimpleItoa(data.AsInteger()));
  }

  void Run(Context* context) const {
    // The methods of the Integer type are written in Evlan and loaded by
    // the SupportCodeManager.
    context->GetSupportCodeManager()
           ->GetSupportCode(SupportCodeManager::INTEGER)
           ->Run(context);
  }

  void GetId(Data data, ValueId* output) const {
    output->SetBytesToRawValue('#', data.AsInteger());
  }

  int Marshal(Data data, distribute::Marshaller* marshaller,
              const ValueId* known_id = NULL) const {
    return marshaller->AddInteger(data.AsInteger());
  }
};

const IntegerLogic IntegerLogic::kIntegerLogic;

const Logic* GetIntegerLogic() {
  return &IntegerLogic::kIntegerLogic;
}

// ===================================================================

namespace {

int Negate(int i) {
  return -i;
}

int Add     (int i, int j) { return i + j; }
int Subtract(int i, int j) { return i - j; }
int Multiply(int i, int j) { return i * j; }

void Divide(Context* context, int i, int j) {
  if (j == 0) {
    context->Return(TypeErrorUtils::MakeError(context,
      "Integer divided by zero."));
  } else {
    context->Return(MakeInteger(i / j));
  }
}

bool Equals          (int i, int j) { return i == j; }
bool NotEquals       (int i, int j) { return i != j; }
bool IsLessThan      (int i, int j) { return i <  j; }
bool IsGreaterThan   (int i, int j) { return i >  j; }
bool IsLessOrEqual   (int i, int j) { return i <= j; }
bool IsGreaterOrEqual(int i, int j) { return i >= j; }

const Logic* NewIntegerBuiltins() {
  NativeModule* result = new NativeModule("nativeInteger");
  result->AddFunction("negate", &Negate);

  result->AddFunction("add"     , &Add     );
  result->AddFunction("subtract", &Subtract);
  result->AddFunction("multiply", &Multiply);
  result->AddFunction("divide"  , &Divide  );

  result->AddFunction("equal"     , &Equals          );
  result->AddFunction("notEqual"  , &NotEquals       );
  result->AddFunction("less"      , &IsLessThan      );
  result->AddFunction("greater"   , &IsGreaterThan   );
  result->AddFunction("notGreater", &IsLessOrEqual   );
  result->AddFunction("notLess"   , &IsGreaterOrEqual);

  return result;
}

}  // namespace

const Logic* GetIntegerBuiltins() {
  static const Logic* const kSingleton = NewIntegerBuiltins();
  return kSingleton;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan

