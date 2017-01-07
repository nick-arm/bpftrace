#include <iostream>

#include "semantic_analyser.h"
#include "ast.h"
#include "parser.tab.hh"

namespace ebpf {
namespace bpftrace {
namespace ast {

void SemanticAnalyser::visit(Integer &integer)
{
  type_ = Type::integer;
}

void SemanticAnalyser::visit(Builtin &builtin)
{
  if (builtin.ident == "nsecs" ||
      builtin.ident == "pid" ||
      builtin.ident == "tid") {
    type_ = Type::integer;
  }
  else {
    type_ = Type::none;
    err_ << "Unknown builtin: '" << builtin.ident << "'" << std::endl;
  }
}

void SemanticAnalyser::visit(Call &call)
{
  int nargs = 0;
  if (call.vargs) {
    nargs = call.vargs->size();
    for (Expression *expr : *call.vargs) {
      expr->accept(*this);
    }
  }

  if (call.func == "quantize") {
    type_ = Type::quantize;
    if (nargs != 1) {
      err_ << "quantize() should take 1 argument (";
      err_ << nargs << " provided)" << std::endl;
    }
  }
  else if (call.func == "count") {
    type_ = Type::count;
    if (nargs != 0) {
      err_ << "count() should take 0 arguments (";
      err_ << nargs << " provided)" << std::endl;
    }
  }
  else {
    type_ = Type::none;
    err_ << "Unknown function: '" << call.func << "'" << std::endl;
  }
}

void SemanticAnalyser::visit(Map &map)
{
  std::vector<Type> args;
  if (map.vargs) {
    for (Expression *expr : *map.vargs) {
      expr->accept(*this);
      args.push_back(type_);
    }
  }

  auto search = map_args_.find(map.ident);
  if (search != map_args_.end()) {
    if (search->second != args) {
      err_ << "Argument mismatch for " << map.ident << ": ";
      err_ << "trying to access with arguments: [ ";
      for (Type t : args) { err_ << typestr(t) << " "; }
      err_ << "]\n\twhen map already uses the arguments: [ ";
      for (Type t : search->second) { err_ << typestr(t) << " "; }
      err_ << "]\n" << std::endl;
    }
  }
  else {
    map_args_.insert({map.ident, args});
  }

  type_ = map_val_.find(map.ident)->second;
}

void SemanticAnalyser::visit(Binop &binop)
{
  Type lhs, rhs;
  binop.left->accept(*this);
  lhs = type_;
  binop.right->accept(*this);
  rhs = type_;

  if (pass_ == 2 && lhs != rhs) {
    err_ << "Type mismatch for '" << opstr(binop) << "': ";
    err_ << "comparing '" << typestr(lhs) << "' ";
    err_ << "with '" << typestr(rhs) << "'" << std::endl;
  }

  type_ = Type::integer;
}

void SemanticAnalyser::visit(Unop &unop)
{
  unop.expr->accept(*this);
  type_ = Type::integer;
}

void SemanticAnalyser::visit(ExprStatement &expr)
{
  expr.expr->accept(*this);
}

void SemanticAnalyser::visit(AssignMapStatement &assignment)
{
  assignment.map->accept(*this);
  assignment.expr->accept(*this);

  std::string map_ident = assignment.map->ident;
  auto search = map_val_.find(map_ident);
  if (search != map_val_.end()) {
    if (search->second != type_) {
      err_ << "Type mismatch for " << map_ident << ": ";
      err_ << "trying to assign variable of type '" << typestr(type_);
      err_ << "'\n\twhen map already contains a value of type '";
      err_ << typestr(search->second) << "'\n" << std::endl;
    }
  }
  else {
    // This map hasn't been seen before
    map_val_.insert({map_ident, type_});
  }
}

void SemanticAnalyser::visit(AssignMapCallStatement &assignment)
{
  assignment.map->accept(*this);
  assignment.call->accept(*this);

  std::string map_ident = assignment.map->ident;
  auto search = map_val_.find(map_ident);
  if (search != map_val_.end()) {
    if (search->second != type_) {
      err_ << "Type mismatch for " << map_ident << ": ";
      err_ << "trying to assign result of '" << assignment.call->func;
      err_ << "()'\n\twhen map already contains a value of type '";
      err_ << typestr(search->second) << "'\n" << std::endl;
    }
  }
  else {
    // This map hasn't been seen before
    map_val_.insert({map_ident, type_});
  }
}

void SemanticAnalyser::visit(Predicate &pred)
{
  pred.expr->accept(*this);
}

void SemanticAnalyser::visit(Probe &probe)
{
  if (probe.pred) {
    probe.pred->accept(*this);
  }
  for (Statement *stmt : *probe.stmts) {
    stmt->accept(*this);
  }
}

void SemanticAnalyser::visit(Program &program)
{
  for (Probe *probe : *program.probes) {
    probe->accept(*this);
  }
}

int SemanticAnalyser::analyse()
{
  // Two pass analysis, to handle variables being used before they are defined:
  // - First pass checks assignments
  // - Second pass checks expressions
  std::string errors;

  for (pass_ = 1; pass_ <= 2; pass_++) {
    root_->accept(*this);
    errors = err_.str();
    if (!errors.empty()) {
      std::cerr << errors;
      return pass_;
    }
  }

  return 0;
}

std::string SemanticAnalyser::typestr(Type t)
{
  switch (t)
  {
    case Type::none:     return "none";     break;
    case Type::integer:  return "integer";  break;
    case Type::quantize: return "quantize"; break;
    case Type::count:    return "count";    break;
    default: abort();
  }
}

} // namespace ast
} // namespace bpftrace
} // namespace ebpf
