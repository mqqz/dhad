#include "typing/scope.hpp"

#include <cassert>

#include "std/runtime.hpp"

namespace dhad::typing {
namespace {
const stdlib::StdRuntime& defaultRuntime() {
  static const stdlib::StdRuntime runtime;
  return runtime;
}
} // namespace

ScopeStack::ScopeStack(const stdlib::StdRuntime* runtime)
    : runtime_(runtime ? runtime : &defaultRuntime()) {
  enterScope();
  seedGlobalScope();
}

void ScopeStack::enterScope() { scopes_.emplace_back(); }

void ScopeStack::exitScope() {
  assert(scopes_.size() > 1 && "Cannot exit the global scope");
  scopes_.pop_back();
}

bool ScopeStack::declare(const std::string& name, TypePtr type) {
  auto& scope = currentScope();
  auto [_, inserted] = scope.emplace(name, std::move(type));
  return inserted;
}

TypePtr ScopeStack::lookup(const std::string& name) const {
  for (auto scopeIt = scopes_.rbegin(); scopeIt != scopes_.rend(); ++scopeIt) {
    const auto& scope = *scopeIt;
    auto found = scope.find(name);
    if (found != scope.end()) {
      return found->second;
    }
  }
  return nullptr;
}

ScopeStack::Scope& ScopeStack::currentScope() {
  assert(!scopes_.empty() && "Scope stack is empty");
  return scopes_.back();
}

const ScopeStack::Scope& ScopeStack::currentScope() const {
  assert(!scopes_.empty() && "Scope stack is empty");
  return scopes_.back();
}

void ScopeStack::seedGlobalScope() {
  assert(scopes_.size() == 1 && "Global scope must exist before seeding");

  auto& global = currentScope();
  for (const auto& builtin : runtime_->functions()) {
    global.emplace(builtin.arabicName, builtin.type);
  }
}

} // namespace dhad::typing
