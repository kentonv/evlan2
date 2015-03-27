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

#include "evlan/parser/expression.h"

#include <algorithm>

#include "evlan/proto/module.pb.h"
#include "evlan/strutil.h"
#include "evlan/map-util.h"
#include "evlan/stl-util.h"

namespace evlan {
namespace parser {

Expression::~Expression() {}

InputTable::InputTable() {
}

InputTable::~InputTable() {
}

void InputTable::AddImport(const string& name) {
  imports_[name] = 0;
}

void InputTable::AddInteger(int value) {
  integer_literals_[value] = 0;
}

void InputTable::AddAtom(const string& name) {
  atoms_[name] = 0;
}

void InputTable::AddData(const string& value) {
  data_blobs_[value] = 0;
}

int InputTable::Finalize(Module* module) {
  int counter = 0;

  for (map<string, int>::iterator iter = imports_.begin();
       iter != imports_.end(); ++iter) {
    module->add_imports(iter->first);
    iter->second = counter++;
  }

  for (map<int, int>::iterator iter = integer_literals_.begin();
       iter != integer_literals_.end(); ++iter) {
    module->add_int_constants(iter->first);
    iter->second = counter++;
  }

  for (map<string, int>::iterator iter = atoms_.begin();
       iter != atoms_.end(); ++iter) {
    module->add_atom_constants(iter->first);
    iter->second = counter++;
  }

  for (map<string, int>::iterator iter = data_blobs_.begin();
       iter != data_blobs_.end(); ++iter) {
    module->add_data_constants(iter->first);
    iter->second = counter++;
  }

  return counter;
}

int InputTable::LookupImport(const string& name) const {
  map<string, int>::const_iterator iter = imports_.find(name);
  GOOGLE_CHECK(iter != imports_.end());
  return iter->second;
}

int InputTable::LookupInteger(int value) const {
  map<int, int>::const_iterator iter = integer_literals_.find(value);
  GOOGLE_CHECK(iter != integer_literals_.end());
  return iter->second;
}

int InputTable::LookupAtom(const string& name) const {
  map<string, int>::const_iterator iter = atoms_.find(name);
  GOOGLE_CHECK(iter != atoms_.end());
  return iter->second;
}

int InputTable::LookupData(const string& value) const {
  map<string, int>::const_iterator iter = data_blobs_.find(value);
  GOOGLE_CHECK(iter != data_blobs_.end());
  return iter->second;
}

// ===================================================================

ExpressionRoot::ExpressionRoot(Expression* child)
  : child_(child) {
}

ExpressionRoot::~ExpressionRoot() {
}

void ExpressionRoot::Compile(Module* module) {
  child_->SetParent(this);

  InputTable input_table;
  child_->VisitDependencies(&input_table);
  input_table.Finalize(module);
  child_->BuildCodeTree(0, input_table, module);
}

void ExpressionRoot::SetParent(Expression* parent) {
  GOOGLE_LOG(FATAL) << "Not supposed to call this.";
}

void ExpressionRoot::VisitDependencies(InputTable* input_table) {
  GOOGLE_LOG(FATAL) << "Not supposed to call this.";
}

bool ExpressionRoot::ChildNeedsVariable(const string& name, int escape_depth,
                                        InputTable* input_table) {
  GOOGLE_LOG(ERROR) << "Variable " << name << " not defined.";
  return false;
}

void ExpressionRoot::BuildCodeTree(int level, const InputTable& input_table,
                                   Module* module) {
  GOOGLE_LOG(FATAL) << "Not supposed to call this.";
}

void ExpressionRoot::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  GOOGLE_LOG(ERROR) << "Variable " << name << " not defined.";

  // Add identity function as dummy value.
  module->add_code_tree(Module::ABSTRACTION);
  module->add_code_tree(Module::FIRST_VARIABLE);
}

// ===================================================================

Variable::Variable(const string& name, int escape_depth)
  : name_(name), escape_depth_(escape_depth) {
}

Variable::~Variable() {
}

void Variable::SetParent(Expression* parent) {
  parent_ = parent;
}

void Variable::VisitDependencies(InputTable* input_table) {
  parent_->ChildNeedsVariable(name_, escape_depth_, input_table);
}

bool Variable::ChildNeedsVariable(const string& name, int escape_depth,
                                  InputTable* input_table) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
  return false;
}

void Variable::BuildCodeTree(int level, const InputTable& input_table,
                             Module* module) {
  parent_->AddVariableReference(name_, escape_depth_,
                                level, input_table, module);
}

void Variable::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
}

// ===================================================================

Abstraction::Abstraction(const string& variable_name, Expression* body)
  : parent_(NULL), body_(body), level_(-1) {
  variable_names_.push_back(variable_name);
}

Abstraction::Abstraction(const vector<string>& variable_names, Expression* body)
  : parent_(NULL), variable_names_(variable_names), body_(body), level_(-1) {
}

Abstraction::~Abstraction() {
}

void Abstraction::SetParent(Expression* parent) {
  parent_ = parent;
  body_->SetParent(this);
}

void Abstraction::VisitDependencies(InputTable* input_table) {
  body_->VisitDependencies(input_table);
}

bool Abstraction::ChildNeedsVariable(const string& name, int escape_depth,
                                     InputTable* input_table) {
  // Check if the variable name is one of the parameters to this function.
  vector<string>::iterator iter =
    std::find(variable_names_.begin(), variable_names_.end(), name);
  if (iter == variable_names_.end()) {
    // Nope -- not found.  Defer to parent.
    return parent_->ChildNeedsVariable(name, escape_depth, input_table);
  }
  if (escape_depth > 0) {
    // The variable name matched, but it is escaped.  So, decrement the
    // escape depth and defer to the parent.
    return parent_->ChildNeedsVariable(name, escape_depth - 1, input_table);
  }

  // The variable is one of the parameters to this function.
  if (variable_names_.size() != 1) {
    // The function has multiple parameters.  So, the variable will be a member
    // of a tuple.  This means we will have to use the index of the tuple
    // element, so we need to add that number to the input table.
    int index = iter - variable_names_.begin();
    input_table->AddInteger(index);
  }

  return true;
}

void Abstraction::BuildCodeTree(int level, const InputTable& input_table,
                                Module* module) {
  level_ = level;
  module->add_code_tree(Module::ABSTRACTION);

  // Increment level to account for this function's parameter and build the
  // body.
  body_->BuildCodeTree(level + 1, input_table, module);
}

void Abstraction::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  // Check if the variable name is one of the parameters to this function.
  vector<string>::iterator iter =
    std::find(variable_names_.begin(), variable_names_.end(), name);
  if (iter == variable_names_.end()) {
    // Nope -- not found.  Defer to parent.
    parent_->AddVariableReference(name, escape_depth, level,
                                  input_table, module);
    return;
  }
  if (escape_depth > 0) {
    // The variable name matched, but it is escaped.  So, decrement the
    // escape depth and defer to the parent.
    parent_->AddVariableReference(name, escape_depth - 1, level,
                                  input_table, module);
    return;
  }

  // The variable is one of the parameters to this function.
  int variable_level = level - level_ - 1;

  if (variable_names_.size() == 1) {
    // This function only takes one parameter, so it is not embedded in a
    // tuple.  So, just refer to the parameter directly.
    module->add_code_tree(Module::FIRST_VARIABLE + variable_level);
  } else {
    // This function takes multiple parameters, so the parameter value is
    // actually a tuple.  To fetch the element of this tuple that we're
    // interested in, we must apply it to the index, which is an integer
    // constant.
    module->add_code_tree(Module::APPLICATION);
    module->add_code_tree(Module::FIRST_VARIABLE + variable_level);
    int index = iter - variable_names_.begin();
    int int_constant_index = input_table.LookupInteger(index);
    module->add_code_tree(Module::FIRST_VARIABLE + level + int_constant_index);
  }
}

// ===================================================================

Application::Application(Expression* function, Expression* parameter)
  : parent_(NULL), function_(function), parameter_(parameter) {
}

Application::~Application() {
}

void Application::SetParent(Expression* parent) {
  parent_ = parent;
  function_->SetParent(this);
  parameter_->SetParent(this);
}

void Application::VisitDependencies(InputTable* input_table) {
  function_->VisitDependencies(input_table);
  parameter_->VisitDependencies(input_table);
}

bool Application::ChildNeedsVariable(const string& name, int escape_depth,
                                     InputTable* input_table) {
  return parent_->ChildNeedsVariable(name, escape_depth, input_table);
}

void Application::BuildCodeTree(int level, const InputTable& input_table,
                                Module* module) {
  module->add_code_tree(Module::APPLICATION);
  function_->BuildCodeTree(level, input_table, module);
  parameter_->BuildCodeTree(level, input_table, module);
}

void Application::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  parent_->AddVariableReference(name, escape_depth, level, input_table, module);
}

// ===================================================================

IntegerLiteral::IntegerLiteral(int value)
  : parent_(NULL), value_(value) {
}

IntegerLiteral::~IntegerLiteral() {
}

void IntegerLiteral::SetParent(Expression* parent) {
  parent_ = parent;
}

void IntegerLiteral::VisitDependencies(InputTable* input_table) {
  input_table->AddInteger(value_);
}

bool IntegerLiteral::ChildNeedsVariable(const string& name, int escape_depth,
                                        InputTable* input_table) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
  return false;
}

void IntegerLiteral::BuildCodeTree(int level, const InputTable& input_table,
                                   Module* module) {
  int index = input_table.LookupInteger(value_);
  module->add_code_tree(Module::FIRST_VARIABLE + level + index);
}

void IntegerLiteral::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
}

// ===================================================================

AtomLiteral::AtomLiteral(const string& name)
  : parent_(NULL), name_(name) {
}

AtomLiteral::~AtomLiteral() {
}

void AtomLiteral::SetParent(Expression* parent) {
  parent_ = parent;
}

void AtomLiteral::VisitDependencies(InputTable* input_table) {
  input_table->AddAtom(name_);
}

bool AtomLiteral::ChildNeedsVariable(const string& name, int escape_depth,
                                     InputTable* input_table) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
  return false;
}

void AtomLiteral::BuildCodeTree(int level, const InputTable& input_table,
                                Module* module) {
  int index = input_table.LookupAtom(name_);
  module->add_code_tree(Module::FIRST_VARIABLE + level + index);
}

void AtomLiteral::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
}

// ===================================================================

StringLiteral::StringLiteral(const string& value)
  : parent_(NULL), value_(value) {
}

StringLiteral::~StringLiteral() {
}

void StringLiteral::SetParent(Expression* parent) {
  parent_ = parent;
}

void StringLiteral::VisitDependencies(InputTable* input_table) {
  input_table->AddData(value_);
}

bool StringLiteral::ChildNeedsVariable(const string& name, int escape_depth,
                                       InputTable* input_table) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
  return false;
}

void StringLiteral::BuildCodeTree(int level, const InputTable& input_table,
                                  Module* module) {
  int index = input_table.LookupData(value_);
  module->add_code_tree(Module::FIRST_VARIABLE + level + index);
}

void StringLiteral::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
}

// ===================================================================

Import::Import(const string& name)
  : parent_(NULL), name_(name) {
}

Import::~Import() {
}

void Import::SetParent(Expression* parent) {
  parent_ = parent;
}

void Import::VisitDependencies(InputTable* input_table) {
  input_table->AddImport(name_);
}

bool Import::ChildNeedsVariable(const string& name, int escape_depth,
                                InputTable* input_table) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
  return false;
}

void Import::BuildCodeTree(int level, const InputTable& input_table,
                           Module* module) {
  int index = input_table.LookupImport(name_);
  module->add_code_tree(Module::FIRST_VARIABLE + level + index);
}

void Import::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  GOOGLE_LOG(FATAL) << "Should never be called; expression has no children.";
}

// ===================================================================

WhereBlock::WhereBlock(Expression* base)
  : parent_(NULL), base_(base), currently_visiting_(NULL) {
}

WhereBlock::~WhereBlock() {
  STLDeleteValues(&bindings_);
}

bool WhereBlock::AddBinding(const string& name, Expression* value) {
  BindingInfo** target = &bindings_[name];
  if (*target == NULL) {
    *target = new BindingInfo(value);
    (*target)->index = ordered_bindings_.size();
    ordered_bindings_.push_back(*target);
    return true;
  } else {
    delete value;
    return false;
  }
}

void WhereBlock::SetParent(Expression* parent) {
  parent_ = parent;

  base_.expression->SetParent(this);

  for (map<string, BindingInfo*>::iterator binding = bindings_.begin();
       binding != bindings_.end(); ++binding) {
    binding->second->expression->SetParent(this);
  }
}

void WhereBlock::VisitDependencies(InputTable* input_table) {
  base_.index = ordered_bindings_.size();
  ordered_bindings_.push_back(&base_);

  currently_visiting_ = &base_;
  base_.expression->VisitDependencies(input_table);

  for (map<string, BindingInfo*>::iterator binding = bindings_.begin();
       binding != bindings_.end(); ++binding) {
    currently_visiting_ = binding->second;
    binding->second->expression->VisitDependencies(input_table);
  }

  currently_visiting_ = NULL;

  // Re-order bindings more efficiently.
  for (int i = 0; i < ordered_bindings_.size(); i++) {
    ordered_bindings_[i]->index = -1;
  }
  ordered_bindings_.clear();
  Order(&base_);

  // Build BindingInfo::dependents for all bindings.  Also populate
  // recursive_vars.
  for (int i = 0; i < ordered_bindings_.size(); i++) {
    BindingInfo* binding = ordered_bindings_[i];
    for (set<BindingInfo*>::iterator iter = binding->dependencies.begin();
         iter != binding->dependencies.end(); ++iter) {
      BindingInfo* dependency = *iter;
      dependency->dependents.insert(binding);

      if (binding->index <= dependency->index) {
        binding->recursive_vars.push_back(dependency);
      }
    }
  }

  // If a binding has recursive vars, then all of its dependents must depend
  // on that recursive var as well, so that it can be passed to the binding.
  for (int i = 0; i < ordered_bindings_.size(); i++) {
    BindingInfo* binding = ordered_bindings_[i];
    for (int i = 0; i < binding->recursive_vars.size(); i++) {
      AddDependencyToDependents(binding, binding->recursive_vars[i]);
    }
  }

  // If there were any recursive functions with multiple recursive vars, we
  // need to use tuples.
  for (int i = 0; i < ordered_bindings_.size(); i++) {
    if (ordered_bindings_[i]->recursive_vars.size() > 1) {
      input_table->AddImport("builtin");
      input_table->AddAtom("tuple");
      input_table->AddAtom("builder");

      // Integers used when reading the tuple.
      for (int j = 0; j < ordered_bindings_[i]->recursive_vars.size(); j++) {
        input_table->AddInteger(j);
      }
      // Integer used when building the tuple.
      input_table->AddInteger(ordered_bindings_[i]->recursive_vars.size());
      break;
    }
  }
}

bool WhereBlock::ChildNeedsVariable(const string& name, int escape_depth,
                                    InputTable* input_table) {
  map<string, BindingInfo*>::iterator iter = bindings_.find(name);

  if (iter != bindings_.end()) {
    if (escape_depth > 0) {
      return parent_->ChildNeedsVariable(name, escape_depth - 1, input_table);
    } else {
      currently_visiting_->dependencies.insert(iter->second);
      return true;
    }
  } else {
    return parent_->ChildNeedsVariable(name, escape_depth, input_table);
  }
}

void WhereBlock::Order(BindingInfo* binding) {
  GOOGLE_CHECK_EQ(binding->index, -1);

  binding->index = -2;  // So that we know we're currently ordering this one.

  for (set<BindingInfo*>::iterator iter = binding->dependencies.begin();
       iter != binding->dependencies.end(); ++iter) {
    BindingInfo* sibling = *iter;

    if (sibling->index == -1) {
      Order(sibling);
    }
  }

  binding->index = ordered_bindings_.size();
  ordered_bindings_.push_back(binding);
}

void WhereBlock::AddDependencyToDependents(BindingInfo* binding,
                                           BindingInfo* dependency) {
  for (set<BindingInfo*>::iterator iter = binding->dependents.begin();
       iter != binding->dependents.end(); ++iter) {
    BindingInfo* dependent = *iter;

    if (InsertIfNotPresent(&dependent->dependencies, dependency)) {
      if (dependent->index <= dependency->index) {
        dependent->recursive_vars.push_back(dependency);
        AddDependencyToDependents(dependent, dependency);
      }
    }
  }
}

// -------------------------------------------------------------------

void WhereBlock::BuildCodeTree(int level, const InputTable& input_table,
                               Module* module) {
  // We translate a where block into a series of abstractions and applications.
  // For example, this:
  //   h(z) where
  //     x = 1
  //     y = f(x)
  //     z = g(x, y)
  // becomes:
  //   (x =>
  //     (y =>
  //       (z => h(z))
  //       (g(x, y))
  //     )(f(x))
  //   )(1)

  for (int i = 0; i < ordered_bindings_.size() - 1; i++) {
    ordered_bindings_[i]->index += level;
    module->add_code_tree(Module::APPLICATION);
    module->add_code_tree(Module::ABSTRACTION);
  }
  ordered_bindings_.back()->index += level;

  for (int i = ordered_bindings_.size() - 1; i >= 0; i--) {
    if (!ordered_bindings_[i]->recursive_vars.empty()) {
      module->add_code_tree(Module::ABSTRACTION);
    }

    currently_visiting_ = ordered_bindings_[i];

    int sub_level = level + i;
    if (!ordered_bindings_[i]->recursive_vars.empty()) {
      ++sub_level;
    }

    ordered_bindings_[i]->expression->BuildCodeTree(
      sub_level, input_table, module);
  }

  currently_visiting_ = NULL;
}

void WhereBlock::AddVariableReference(
    const string& name, int escape_depth, int level,
    const InputTable& input_table, Module* module) {
  map<string, BindingInfo*>::iterator iter = bindings_.find(name);

  if (iter == bindings_.end()) {
    parent_->AddVariableReference(name, escape_depth, level,
                                  input_table, module);
    return;
  }
  if (escape_depth > 0) {
    parent_->AddVariableReference(name, escape_depth - 1, level,
                                  input_table, module);
    return;
  }

  BindingInfo* binding = iter->second;

  // Two things we need to do:
  // (1) If the referenced binding is a recursive_var relative to the current
  //     binding, we need to refer to the recursive parameter rather than the
  //     binding itself.  E.g., when we have:
  //       f = g(f)
  //     this becomes:
  //       f = f => g(f(f))
  //     Note that the f in g(f(f)) refers to a function parameter, not the
  //     binding from the where block.
  // (2) If the referred binding itself has recursive vars, then we need to
  //     pass those vars here.  E.g. in the above example, f became f(f).
  //
  // Note that (1) always implies (2), but not vice versa.
  //
  // Part (1) is handled by AddBindingReference().  The rest of the code in
  // this function handles (2).

  if (!binding->recursive_vars.empty()) {
    module->add_code_tree(Module::APPLICATION);
  }

  AddBindingReference(binding, level, input_table, module);

  if (!binding->recursive_vars.empty()) {
    int size = binding->recursive_vars.size();

    if (size == 1) {
      // No need to build a tuple.
      AddBindingReference(binding->recursive_vars[0], level,
                          input_table, module);
    } else {
      // The tuple builder needs to be applied to each variable.
      for (int i = 0; i < size; i++) {
        module->add_code_tree(Module::APPLICATION);
      }

      // Make the tuple builder itself.  This chunk represents the code:
      //   import "builtin".tuple.builder(size)
      int builtin_import = input_table.LookupImport("builtin");
      int tuple_atom = input_table.LookupImport("tuple");
      int builder_atom = input_table.LookupAtom("builder");
      int int_literal = input_table.LookupInteger(size);

      module->add_code_tree(Module::APPLICATION);
      module->add_code_tree(Module::APPLICATION);
      module->add_code_tree(Module::APPLICATION);
      module->add_code_tree(Module::FIRST_VARIABLE + level + builtin_import);
      module->add_code_tree(Module::FIRST_VARIABLE + level + tuple_atom);
      module->add_code_tree(Module::FIRST_VARIABLE + level + builder_atom);
      module->add_code_tree(Module::FIRST_VARIABLE + level + int_literal);

      // Complete the applications started above.
      for (int i = 0; i < size; i++) {
        AddBindingReference(binding->recursive_vars[i], level,
                            input_table, module);
      }
    }
  }
}

void WhereBlock::AddBindingReference(BindingInfo* binding, int level,
                                     const InputTable& input_table,
                                     Module* module) {
  if (currently_visiting_->index <= binding->index) {
    // This binding is a recursive variable.  We need to reference it as a
    // function parameter.  (See comment in AddVariableReference() -- this
    // deals with part 1.)

    int recursive_var_index = currently_visiting_->FindRecursiveVar(binding);
    GOOGLE_CHECK_NE(recursive_var_index, -1);
    int variable_level = level - currently_visiting_->index - 1;

    if (currently_visiting_->recursive_vars.size() == 1) {
      // No tuple involved.  Just reference the variable.
      module->add_code_tree(Module::FIRST_VARIABLE + variable_level);
    } else {
      // The var is inside a tuple.  The following code applies the tuple to
      // the index.
      int int_literal = input_table.LookupInteger(recursive_var_index);

      module->add_code_tree(Module::APPLICATION);
      module->add_code_tree(Module::FIRST_VARIABLE + variable_level);
      module->add_code_tree(Module::FIRST_VARIABLE + level + int_literal);
    }

  } else {
    // Just a regular binding reference.
    int variable_level = level - binding->index - 1;
    module->add_code_tree(Module::FIRST_VARIABLE + variable_level);
  }
}

// ===================================================================

Expression* MakeTuple(const vector<Expression*>& elements) {
  // Example:
  //   f(@a, @b, @c)
  // becomes:
  //   f(import "builtin".array.tupleBuilder(3)(@a)(@b)(@c))
  Expression* result = new Import("builtin");
  result = new Application(result, new AtomLiteral("array"));
  result = new Application(result, new AtomLiteral("tupleBuilder"));
  result = new Application(result, new IntegerLiteral(elements.size()));

  for (int i = 0; i < elements.size(); i++) {
    result = new Application(result, elements[i]);
  }

  return result;
}

Expression* MakeArray(const vector<Expression*>& elements) {
  // Example:
  //   {@a, @b, @c}
  // becomes:
  //   import "builtin".array.builder(3)(@a)(@b)(@c)
  Expression* result = new Import("builtin");
  result = new Application(result, new AtomLiteral("array"));
  result = new Application(result, new AtomLiteral("builder"));
  result = new Application(result, new IntegerLiteral(elements.size()));

  for (int i = 0; i < elements.size(); i++) {
    result = new Application(result, elements[i]);
  }

  return result;
}

Expression* MakeIf(Expression* condition,
                   Expression* true_clause,
                   Expression* false_clause) {
  // Example:
  //   if a then b else c
  // becomes:
  //   a.$if(() => b, () => c)()

  vector<Expression*> clauses;
  clauses.push_back(new Abstraction(vector<string>(), true_clause));
  clauses.push_back(new Abstraction(vector<string>(), false_clause));

  return new Application(
    new Application(
      new Application(condition, new AtomLiteral("if")),
      MakeTuple(clauses)),
    MakeTuple(vector<Expression*>()));
}

Expression* MakeObject(const vector<pair<string, Expression*> >& members) {
  // Example:
  //   object of
  //     x = 1
  //     y = 2
  //     z = 3
  // becomes:
  //   (members => name =>
  //      members(import "builtin".atom.table({@x, @y, @z})(name))
  //   ){x, y, z} where
  //      x = 1
  //      y = 2
  //      z = 3
  // or, perhaps more legibly:
  //   (members => name =>
  //      members(index) where
  //        index = nameTable(name)
  //        nameTable = makeTable({@x, @y, @z})
  //        makeTable = import "builtin".atom.table
  //   )(x, y, z) where
  //      x = 1
  //      y = 2
  //      z = 3
  // We rely on later optimization to ensure that the atom table is only built
  // once, at module load time.

  vector<Expression*> member_atoms;
  for (int i = 0; i < members.size(); i++) {
    member_atoms.push_back(new AtomLiteral(members[i].first));
  }

  Expression* result = new Import("builtin");
  result = new Application(result, new AtomLiteral("atom"));
  result = new Application(result, new AtomLiteral("table"));  // = makeTable
  result = new Application(result, MakeArray(member_atoms));   // = nameTable
  result = new Application(result, new Variable("name", 0));   // = index
  result = new Application(new Variable("members", 0), result);
  result = new Abstraction("name", result);
  result = new Abstraction("members", result);

  vector<Expression*> member_variables;
  for (int i = 0; i < members.size(); i++) {
    member_variables.push_back(new Variable(members[i].first, 0));
  }

  WhereBlock* member_expression = new WhereBlock(MakeTuple(member_variables));

  for (int i = 0; i < members.size(); i++) {
    if (!member_expression->AddBinding(members[i].first, members[i].second)) {
      GOOGLE_LOG(ERROR) << members[i].first << " is already defined in this object.";
    }
  }

  result = new Application(result, member_expression);

  return result;
}

Expression* MakeBooleanLiteral(bool value) {
  // Example:
  //   true
  // becomes:
  //   import "builtin".boolean.$true

  return new Application(
    new Application(
      new Import("builtin"),
      new AtomLiteral("boolean")),
    new AtomLiteral(value ? "true" : "false"));
}

Expression* MakeDoubleLiteral(double value) {
  // Example:
  //   2.5
  // becomes:
  //  import "builtin".double.decode({byte data for 2.5})

  // Build the byte string value of the double
  // value given to us.
  string byte_value(sizeof(uint64), '\0');
  uint64 bit_value = bit_cast<uint64>(value);

  for (int i = 0; i < sizeof(uint64); ++i) {
    char masked_bits = bit_value & 0xFF;
    byte_value[i] = masked_bits;
    bit_value >>= 8;
  }

  return
      new Application(
          new Application(
              new Application(
                  new Import("builtin"),
                  new AtomLiteral("double")),
              new AtomLiteral("decode")),
          new DataLiteral(byte_value));
}

Expression* MakeSubscript(Expression* collection, Expression* subscript) {
  // Example:
  //   foo[123]
  // becomes:
  //   foo.element(123)

  return new Application(
    new Application(collection, new AtomLiteral("element")),
    subscript);
}

}  // namespace parser
}  // namespace evlan
