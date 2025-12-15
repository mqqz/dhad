#pragma once

#include "../lexer/tokens.hpp"

#include <initializer_list>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace dhad::parser {

struct NonTerminal {
  constexpr NonTerminal() = default;
  constexpr explicit NonTerminal(std::string_view identifier) : name(identifier) {}

  std::string_view name;
};

struct Terminal {
  constexpr Terminal() = default;
  constexpr explicit Terminal(TokenType token) : kind(token) {}

  TokenType kind{TokenType::INVALID};
};

using Symbol = std::variant<Terminal, NonTerminal>;

inline Symbol makeSymbol(TokenType token) { return Symbol{Terminal{token}}; }
inline Symbol makeSymbol(const NonTerminal& nt) { return Symbol{nt}; }

struct ProductionRule {
  NonTerminal lhs;
  std::vector<Symbol> rhs;

  ProductionRule() = default;
  ProductionRule(NonTerminal left, std::vector<Symbol> symbols)
      : lhs(left), rhs(std::move(symbols)) {}
  ProductionRule(NonTerminal left, std::initializer_list<Symbol> symbols)
      : lhs(left), rhs(symbols) {}
};

const std::vector<ProductionRule>& getGrammar();

} // namespace dhad::parser
