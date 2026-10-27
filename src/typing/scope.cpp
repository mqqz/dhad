#include "typing/scope.hpp"

#include <cassert>
#include <utility>

#include "std/runtime.hpp"

namespace dhad::typing {
namespace {
const stdlib::StdRuntime& defaultRuntime() {
  static const stdlib::StdRuntime runtime;
  return runtime;
}
} // namespace

ScopeStack::ScopeStack(const stdlib::StdRuntime* runtime, ScopeInitializer initializer)
    : runtime_(runtime ? runtime : &defaultRuntime()) {
  if (initializer) {
    initializer_ = std::move(initializer);
  } else {
    initializer_ = [this](Scope& scope) {
      for (const auto& builtin : runtime_->functions()) {
        scope.emplace(builtin.arabicName, builtin.type);
      }
    };
  }
  enterScope();
  seedGlobalScope();
}

void ScopeStack::enterScope() { scopes_.emplace_back(); }

void ScopeStack::exitScope() {
  assert(scopes_.size() > 1 && "Cannot exit the global scope");
  scopes_.pop_back();
}

bool ScopeStack::declare(const std::string& name, TypePtr type, bool isConst) {
  auto& scope = currentScope();
  auto [_, inserted] = scope.emplace(name, Binding{std::move(type), isConst});
  return inserted;
}

TypePtr ScopeStack::lookup(const std::string& name) const {
  for (auto scopeIt = scopes_.rbegin(); scopeIt != scopes_.rend(); ++scopeIt) {
    const auto& scope = *scopeIt;
    auto found = scope.find(name);
    if (found != scope.end()) {
      return found->second.type;
    }
  }
  return nullptr;
}

bool ScopeStack::isConst(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return found->second.isConst;
  }
  return false;
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
  if (initializer_) {
    initializer_(global);
  }
}

} // namespace dhad::typing
