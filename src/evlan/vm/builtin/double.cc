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

// Author: Joseph Schorr
//
// Based significantly on the integer.cc file (Thanks Kenton!)

#include "evlan/vm/builtin/double.h"

#include <limits>
#include <math.h>

#include "evlan/vm/builtin/native.h"
#include "evlan/vm/builtin/support.h"
#include "evlan/vm/distribute/marshaller.h"
#include "evlan/common.h"
#include <google/protobuf/io/printer.h>
#include "evlan/strutil.h"

namespace evlan {
namespace vm {
namespace builtin {

class DoubleLogic : public Logic {
 public:
  DoubleLogic() {}
  ~DoubleLogic() {}

  static const DoubleLogic kDoubleLogic;

  // implements Logic ------------------------------------------------
  void Accept(const Data* data, memory::MemoryVisitor* visitor) const {
    // Nothing to do here.
  }

  void DebugPrint(Data data, int depth,
                  google::protobuf::io::Printer* printer) const {
    double value = data.AsDouble();
    string string_value = SimpleDtoa(value);

    if (string_value.find('.') == string::npos &&
        string_value.find('e') == string::npos &&
        string_value.find("inf") == string::npos &&
        string_value.find("nan") == string::npos) {
      printer->Print("$value$.0", "value", string_value);
    } else {
      printer->Print("$value$", "value", string_value);
    }
  }

  void Run(Context* context) const {
    // The methods of the Double type are written in Evlan and loaded by
    // the SupportCodeManager.
    context->GetSupportCodeManager()
           ->GetSupportCode(SupportCodeManager::DOUBLE)
           ->Run(context);
  }

  void GetId(Data data, ValueId* output) const {
    output->SetBytesToRawValue('%', data.AsDouble());
  }

  int Marshal(Data data, distribute::Marshaller* marshaller,
              const ValueId* known_id = NULL) const {
    return marshaller->AddDouble(data.AsDouble());
  }
};

const DoubleLogic DoubleLogic::kDoubleLogic;

const Logic* GetDoubleLogic() {
  return &DoubleLogic::kDoubleLogic;
}

// ===================================================================

namespace {

Value kInfinity(GetDoubleLogic(), Data(
                    std::numeric_limits<double>::infinity()));

double Negate(double x) {
  return -x;
}

double Add     (double x, double y) { return x + y; }
double Subtract(double x, double y) { return x - y; }
double Multiply(double x, double y) { return x * y; }

int Floor(double x) {
  return static_cast<int>(x);
}

int Round(double x) {
  return static_cast<int>(x + 0.5);
}

double Divide(double x, double y) {
  // Divide by zero handled by IEE-754.
  return x / y;
}

double Decode(const string& data) {
  if (data.size() != 8) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  uint64 bit_value = 0;

  for (int i = sizeof(uint64) - 1; i >= 0; --i) {
    bit_value |= (unsigned char)data[i];

    if (i > 0) {
      bit_value <<= 8;
    }
  }

  return bit_cast<double>(bit_value);
}

bool Equals          (double x, double y) { return x == y; }
bool NotEquals       (double x, double y) { return x != y; }
bool IsLessThan      (double x, double y) { return x <  y; }
bool IsGreaterThan   (double x, double y) { return x >  y; }
bool IsLessOrEqual   (double x, double y) { return x <= y; }
bool IsGreaterOrEqual(double x, double y) { return x >= y; }

const Logic* NewDoubleBuiltins() {
  NativeModule* result = new NativeModule("nativeDouble");

  result->AddValue("infinity", kInfinity);

  result->AddFunction("decode"    , &Decode    );
  result->AddFunction("negate"    , &Negate    );
  result->AddFunction("floor"     , &Floor     );
  result->AddFunction("round"     , &Round     );

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

const Logic* GetDoubleBuiltins() {
  static const Logic* const kSingleton = NewDoubleBuiltins();
  return kSingleton;
}

}  // namespace builtin
}  // namespace vm
}  // namespace evlan
