#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "typing/types.hpp"

namespace dhad::stdlib {
class StdRuntime;
}

namespace dhad::typing {

class ScopeStack {
public:
  using Scope = std::unordered_map<std::string, TypePtr>;

  explicit ScopeStack(const stdlib::StdRuntime* runtime = nullptr);

  void enterScope();
  void exitScope();

  bool declare(const std::string& name, TypePtr type);
  [[nodiscard]] TypePtr lookup(const std::string& name) const;

  [[nodiscard]] std::size_t depth() const { return scopes_.size(); }

private:
  std::vector<Scope> scopes_;
  const stdlib::StdRuntime* runtime_{nullptr};

  Scope& currentScope();
  [[nodiscard]] const Scope& currentScope() const;
  void seedGlobalScope();
};

} // namespace dhad::typing
