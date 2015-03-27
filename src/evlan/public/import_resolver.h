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

#ifndef EVLAN_PUBLIC_IMPORT_RESOLVER_H__
#define EVLAN_PUBLIC_IMPORT_RESOLVER_H__

#include <map>
#include "evlan/common.h"
#include "evlan/common.h"
#include "evlan/stringpiece.h"

class Mutex;

namespace google {
  namespace protobuf {
    class DescriptorPool;
    class FileDescriptor;
    class MessageFactory;
  }
}

namespace evlan {

// Defined in evaluator.h.
class EvlanValue;
class EvlanEvaluator;

// Abstract interface for an object which resolves import statements.
class ImportResolver {
 public:
  virtual ~ImportResolver();

  // Returns an EvlanValue representing the given import, or NULL if the import
  // was not found.  The caller takes ownership of the returned value.  The
  // value must be usable by the given EvlanEvaluator.
  virtual EvlanValue* Import(EvlanEvaluator* evaluator,
                             const StringPiece& name) = 0;
};

// An ImportResolver that always returns NULL.
class NullImportResolver : public ImportResolver {
 public:
  NullImportResolver();
  virtual ~NullImportResolver();

  // implements ImportResolver ---------------------------------------
  virtual EvlanValue* Import(EvlanEvaluator* evaluator,
                             const StringPiece& name);

 private:
  DISALLOW_COPY_AND_ASSIGN(NullImportResolver);
};

// A simple implementation of ImportResolver which allows you to map names
// directly to values.  You may also map prefixes to be handled by other
// resolvers.
class MappedImportResolver : public ImportResolver {
 public:
  MappedImportResolver();
  virtual ~MappedImportResolver();

  // Maps the given import name to the given value.  The caller retains
  // ownership of the value, which need only remain valid until Map() returns.
  void Map(const StringPiece& name, EvlanValue* value);

  // Maps all imports which start with the given prefix.  Any import beginning
  // with this prefix will be forwarded to |sub_resolver|, after first removing
  // the prefix.  The caller retains ownership of |sub_resolver|, but it must
  // outlive the MappedImportResolver.  If an import matches multiple prefixes,
  // the longest one (i.e. the most-specific one) will be used.
  void MapPrefix(const StringPiece& prefix,
                 ImportResolver* sub_resolver);

  // implements ImportResolver ---------------------------------------
  virtual EvlanValue* Import(EvlanEvaluator* evaluator,
                             const StringPiece& name);

 private:
  struct ValueOrResolver {
    bool is_resolver;
    union {
      EvlanValue* value;
      ImportResolver* resolver;
    };

    inline ValueOrResolver() : is_resolver(false), value(NULL) {}
    inline explicit ValueOrResolver(EvlanValue* value)
      : is_resolver(false) { this->value = value; }
    inline explicit ValueOrResolver(ImportResolver* resolver)
      : is_resolver(true) { this->resolver = resolver; }
  };

  typedef map<string, ValueOrResolver> ImportMap;
  ImportMap import_map_;

  DISALLOW_COPY_AND_ASSIGN(MappedImportResolver);
};

// An ImportResolver which looks up .proto files in a DescriptorPool and
// returns their Evlan representations.
//
// Hint:  To construct a ProtoImportResolver that contains all
//   .proto files which have been compiled into the binary, use:
//     ProtoImportResolver resolver(
//       google::protobuf::DescriptorPool::generated_pool(),
//       google::protobuf::MessageFactory::generated_factory(),
//       NULL);

class ProtoImportResolver : public ImportResolver {
 public:
  // Create a new ProtoImportResolver around an arbitrary DescriptorPool and
  // MessageFactory.  The factory must be able to construct Message objects
  // for every Descriptor in the pool.  |factory_mutex| will be locked while
  // calling factory->GetPrototype(); it may be NULL if the factory does not
  // require synchronization.  The caller retains ownership of all pointers;
  // they must outlive the ProtoImportResolver.
  ProtoImportResolver(const google::protobuf::DescriptorPool* pool,
                      google::protobuf::MessageFactory* factory,
                      Mutex* factory_mutex);

  virtual ~ProtoImportResolver();

  // implements ImportResolver ---------------------------------------
  virtual EvlanValue* Import(EvlanEvaluator* evaluator,
                             const StringPiece& name);

 private:
  const google::protobuf::DescriptorPool* pool_;
  google::protobuf::MessageFactory* factory_;
  Mutex* factory_mutex_;

  DISALLOW_COPY_AND_ASSIGN(ProtoImportResolver);
};

}  // namespace evlan

#endif  // EVLAN_PUBLIC_IMPORT_RESOLVER_H__
