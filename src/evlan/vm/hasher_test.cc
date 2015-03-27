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

// Copyright 2010 Google Inc. All Rights Reserved.
// Author: Kenton Varda

#include "evlan/vm/hasher.h"

#include "evlan/stringpiece.h"
#include <gtest/gtest.h>

namespace evlan {
namespace vm {
namespace {

string ToString(Hasher* hasher) {
  char buffer[Hasher::kHashSize];
  hasher->Finish(buffer);
  return string(buffer, sizeof(buffer));
}

TEST(HasherTest, Unique) {
  Hasher hasher1, hasher2, hasher3;

  hasher1.AddBytes("foo");
  hasher2.AddBytes("foo");
  hasher3.AddBytes("bar");

  string hash1 = ToString(&hasher1);

  // Can't finish twice.
  EXPECT_DEBUG_DEATH(ToString(&hasher1), "finished_");

  // Don't use EXPECT_EQ because we don't want to write raw binary data to the
  // terminal in case of mismatch.
  EXPECT_TRUE(hash1 == ToString(&hasher2));
  EXPECT_TRUE(hash1 != ToString(&hasher3));
}

TEST(HasherTest, AddValue) {
  Hasher hasher1, hasher2, hasher3;

  hasher1.AddValue(1);
  hasher2.AddValue(1);
  hasher3.AddValue(2);

  string hash1 = ToString(&hasher1);

  EXPECT_TRUE(hash1 == ToString(&hasher2));
  EXPECT_TRUE(hash1 != ToString(&hasher3));
}

TEST(HasherTest, AddArray) {
  int array1[] = {1, 2, 3};
  int array2[] = {4, 5, 6};

  Hasher hasher1, hasher2, hasher3;

  hasher1.AddArray(array1, 3);
  hasher2.AddArray(array1, 3);
  hasher3.AddArray(array2, 3);

  string hash1 = ToString(&hasher1);

  EXPECT_TRUE(hash1 == ToString(&hasher2));
  EXPECT_TRUE(hash1 != ToString(&hasher3));
}

TEST(HasherTest, Concatenate) {
  Hasher hasher1, hasher2;

  hasher1.AddBytes("foo");
  hasher1.AddBytes("barbaz");
  hasher2.AddBytes("foobar");
  hasher2.AddBytes("baz");

  EXPECT_TRUE(ToString(&hasher1) == ToString(&hasher2));
}

TEST(HasherTest, AddStringNoConcatenate) {
  Hasher hasher1, hasher2;

  hasher1.AddString("foo");
  hasher1.AddString("barbaz");
  hasher2.AddString("foobar");
  hasher2.AddString("baz");

  EXPECT_TRUE(ToString(&hasher1) != ToString(&hasher2));
}

}  // namespace
}  // namespace vm
}  // namespace evlan
