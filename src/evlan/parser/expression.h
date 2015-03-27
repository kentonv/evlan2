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
// The Evlan parser parses code and produces a tree of Expression objects,
// which then know how to compile themselves into a single Module.  This file
// defines the Expression interface and all implementations.  parser.cc does
// the actual parsing.

#ifndef EVLAN_PARSER_EXPRESSION_H__
#define EVLAN_PARSER_EXPRESSION_H__

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <evlan/common.h>

namespace evlan { class Module; }

namespace evlan {
namespace parser {

// This class keeps track of constants that need to go in the module's constant
// table.
class InputTable {
 public:
  InputTable();
  ~InputTable();

  // During the Expression::VisitDependencies() pass, these methods are called
  // to register values in the table.
  void AddImport(const string& name);
  void AddInteger(int value);
  void AddAtom(const string& name);
  void AddData(const string& value);

  // Once all values have been added, Finalize() writes them to the actual
  // Module object and keeps track of their final indexes.
  int Finalize(Module* module);

  // During Expression::BuildCodeTree(), these methods are called to look up
  // the index of each value.  The values must have been previously registered
  // with one of the Add*() methods.
  int LookupImport(const string& name) const;
  int LookupInteger(int value) const;
  int LookupAtom(const string& name) const;
  int LookupData(const string& value) const;

 private:
  map<string, int> imports_;
  map<int, int> integer_literals_;
  map<string, int> atoms_;
  map<string, int> data_blobs_;
};

// Abstract interface for a parsed expression which can compile itself into
// a code tree.
class Expression {
 public:
  virtual ~Expression();

  // Set own parent pointer to the expression given, and recursively set the
  // parent pointer on all children.
  virtual void SetParent(Expression* parent) = 0;

  // Recursively search for all variables and call ChildNeedsVariable() for each
  // one.  Also add any entries to the input table as needed, and possibly make
  // note of children's dependencies.
  virtual void VisitDependencies(InputTable* input_table) = 0;

  // Called by a child during VisitDependencies() to indicate that it needs
  // the given variable name to be defined.  Returns true if found, false
  // if not.  escape_depth is the number of variables of the same name to skip
  // while searching.  Callee may register stuff in input_table.
  virtual bool ChildNeedsVariable(const string& name, int escape_depth,
                                  InputTable* input_table) = 0;

  // Add code for this expression to the given module's code_tree.  May
  // call AddVariableReference() in process and actually use the result.
  // |level| is the abstraction level, which is the current function nesting
  // depth.  In other words, |level| is the number of outer functions whose
  // parameters are visible to the current expression.
  virtual void BuildCodeTree(int level, const InputTable& input_table,
                             Module* module) = 0;

  // Called by a child druing BuildCodeTree() to add code to the code tree
  // which evaluates to the named variable.  The variable must have been
  // visited using ChildNeedsVariable() during the VisitDependencies() pass.
  // |level| is the level passed to BuildCodeTree() in the context where the
  // variable is being referenced.  This allows the callee to compute the
  // index of a particular variable in that context.
  virtual void AddVariableReference(const string& name, int escape_depth,
                                    int level, const InputTable& input_table,
                                    Module* module) = 0;
};

// ===================================================================

// This class is used as the root of the expression tree.
class ExpressionRoot : public Expression {
 public:
  ExpressionRoot(Expression* child);
  ~ExpressionRoot();

  // Compiles the expression as a single module and writes it to the given
  // Module object.
  void Compile(Module* module);

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  scoped_ptr<Expression> child_;
};

// A variable identifier.
class Variable : public Expression {
 public:
  // |escape_depth| is the number of backslashes that preceded the variable
  // name.
  Variable(const string& name, int escape_depth);
  ~Variable();

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  Expression* parent_;

  string name_;
  int escape_depth_;
};

// A function definition.
class Abstraction : public Expression {
 public:
  // Single-argument function.
  Abstraction(const string& variable_name, Expression* body);

  // Multi-argument function.
  Abstraction(const vector<string>& variable_names, Expression* body);

  ~Abstraction();

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  Expression* parent_;

  vector<string> variable_names_;
  scoped_ptr<Expression> body_;
  int level_;
};

// A function call.
class Application : public Expression {
 public:
  Application(Expression* function, Expression* parameter);
  ~Application();

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  Expression* parent_;

  scoped_ptr<Expression> function_;
  scoped_ptr<Expression> parameter_;
};

// An integer literal.
class IntegerLiteral : public Expression {
 public:
  IntegerLiteral(int value);
  ~IntegerLiteral();

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  Expression* parent_;

  int value_;
};

// At atom literal.
class AtomLiteral : public Expression {
 public:
  AtomLiteral(const string& name);
  ~AtomLiteral();

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  Expression* parent_;

  string name_;
};

// A string literal.
class StringLiteral : public Expression {
 public:
  StringLiteral(const string& value);
  ~StringLiteral();

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  Expression* parent_;

  string value_;
};

// For now, we declare that the DataLiteral is a StringLiteral,
// because they use the same storage type (i.e. raw bytes).
typedef StringLiteral DataLiteral;

// An import.
class Import : public Expression {
 public:
  Import(const string& name);
  ~Import();

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  Expression* parent_;

  string name_;
};

// A where block.
class WhereBlock : public Expression {
 public:
  // |base| is the expression that comes before the "where" keyword, which
  // evaluates to the final result after variable substitutions have taken
  // place.
  WhereBlock(Expression* base);
  ~WhereBlock();

  // Adds one variable binding to the where block.  For example, "x = 1" would
  // be added by:
  //   AddBinding("x", new IntegerLiteral(1));
  bool AddBinding(const string& name, Expression* value);

  // implements Expression -------------------------------------------
  void SetParent(Expression* parent);
  void VisitDependencies(InputTable* input_table);
  bool ChildNeedsVariable(const string& name, int escape_depth,
                          InputTable* input_table);
  void BuildCodeTree(int level, const InputTable& input_table,
                     Module* module);
  void AddVariableReference(const string& name, int escape_depth,
                            int level, const InputTable& input_table,
                            Module* module);

 private:
  // Warning:  The implementation is complicated, especially because we have
  //   to detect and deal with recursion.

  Expression* parent_;

  struct BindingInfo {
    scoped_ptr<Expression> expression;

    // This binding's index in ordered_bindings_, or -1 if it is not listed.
    // We end up generating code which computes the bindings in this order.
    int index;

    // What other bindings does this binding refer to?
    set<BindingInfo*> dependencies;

    // What other bindings refer to this binding?
    set<BindingInfo*> dependents;

    // Of the bindings listed in |dependencies|, these ones have indexes which
    // are greater than or equal to this binding's index.  This occurs when
    // bindings refer to each other recursively.  In this case, we will have to
    // transform the code to get rid of this recusion.  For example, say we
    // have code like:
    //   g(123) where
    //     f = x => g(x - 1) + 1
    //     g = x => if x > 0 then f(x) else 0
    // This will be transformed to:
    //   g(g)(123) where
    //     f = g => x => g(g)(x - 1) + 1
    //     g = g => x => if x > 0 then f(g)(x) else 0
    // As you can see, this code will produce the same result but the bindings
    // no longer refer to each other.
    //
    // |recursive_vars| is a vector rather than a set because the order matters
    // since recursive values become parameters to this binding.
    vector<BindingInfo*> recursive_vars;

    inline BindingInfo(Expression* expression)
      : expression(expression), index(-1) {}

    // Looks for the binding inside |recursive_vars| and returns the index
    // if found.  Otherwise returns -1.
    inline int FindRecursiveVar(BindingInfo* var) {
      for (int i = 0; i < recursive_vars.size(); i++) {
        if (recursive_vars[i] == var) return i;
      }
      return -1;
    }
  };

  // The BindingInfo representing the base expression (the part that comes
  // before the "where" keyword).
  BindingInfo base_;

  // BindingInfos for each variable binding, mapped by name.
  map<string, BindingInfo*> bindings_;

  // We will generated code that computes the BindingInfos in the order in
  // which they appear in this vector.
  vector<BindingInfo*> ordered_bindings_;

  // During VisitDependencies(), we recursively call VisitDependencies() on each
  // binding's expression.  currently_visiting_ is set to the binding currently
  // being visited so that if it calls ChildNeedsVariable() we can update the
  // binding's dependencies as appropriate.  Similarly, during BuildCodeTree(),
  // currently_visiting_ is set to the child being built so that
  // AddVariableReference() can do the right thing.
  BindingInfo* currently_visiting_;

  // Populates the ordered_bindings_ vector by first calling Order() on all of
  // the given binding's dependencies, then adding the binding itself.  This
  // way, each binding comes before all the bindings that depend on it.  If
  // cycles are present, the order will be approximate, and we will have to
  // rewrite some of the bindings for recursion later.
  void Order(BindingInfo* binding);

  // Adds |dependency| to the dependency list of all bindings which depend on
  // |binding|.
  void AddDependencyToDependents(BindingInfo* binding, BindingInfo* dependency);

  // Add code to module.code_tree which evaluates to the given binding.  If
  // the binding has recursive_vars, it is up to the caller to arrange for
  // the binding to be applied to the other bindings listed there.
  void AddBindingReference(BindingInfo* binding, int level,
                           const InputTable& input_table,
                           Module* module);
};

// ===================================================================

// Construct an expression representing a tuple containing the given values.
Expression* MakeTuple(const vector<Expression*>& elements);

// Construct an expression representing an array containing the given values.
Expression* MakeArray(const vector<Expression*>& elements);

// Construct an if-then-else expression with the given condition and branches.
Expression* MakeIf(Expression* condition,
                   Expression* true_clause,
                   Expression* false_clause);

// Construct an object with the given members.  Each pair is a member name and
// an expression that evaluates to the member's value.
Expression* MakeObject(const vector<pair<string, Expression*> >& members);

// Construct a boolean literal expression.
Expression* MakeBooleanLiteral(bool value);

// Construct a double literal expression.
Expression* MakeDoubleLiteral(double value);

// Construct a subscript expression (e.g. of an array).
Expression* MakeSubscript(Expression* collection, Expression* subscript);

}  // namespace parser
}  // namespace evlan

#endif  // EVLAN_PARSER_EXPRESSION_H__
