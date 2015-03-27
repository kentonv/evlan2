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

#ifndef EVLAN_STRUTIL_H_
#define EVLAN_STRUTIL_H_

#include <google/protobuf/stubs/strutil.h>
#include <google/protobuf/stubs/substitute.h>

namespace evlan {

using google::protobuf::StringReplace;
using google::protobuf::SimpleItoa;
using google::protobuf::SimpleDtoa;
using google::protobuf::LowerString;
using google::protobuf::SplitStringUsing;
using google::protobuf::HasPrefixString;
using google::protobuf::HasSuffixString;
using google::protobuf::StripPrefixString;
using google::protobuf::StripSuffixString;

namespace strings {

using google::protobuf::UnescapeCEscapeString;
using google::protobuf::CEscape;
using google::protobuf::strings::Substitute;
using google::protobuf::strings::SubstituteAndAppend;

}

}  // namespace evlan

#endif  // EVLAN_STRUTIL_H_
