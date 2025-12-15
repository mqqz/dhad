#include "rules.hpp"

namespace dhad::parser {

namespace nt {
#define NONTERM(name) inline constexpr NonTerminal name{#name};
#include "nonterminals.def"
#undef NONTERM
} // namespace nt

const std::vector<ProductionRule>& getGrammar() {
#define NT(name) makeSymbol(nt::name)
#define TOK(token) makeSymbol(TokenType::token)
#define RULE(lhs, ...) ProductionRule(nt::lhs, {__VA_ARGS__})
#define RULE_EMPTY(lhs) ProductionRule(nt::lhs, {})

  static const std::vector<ProductionRule> grammar = {
#include "grammar.def"
  };

#undef RULE_EMPTY
#undef RULE
#undef TOK
#undef NT

  return grammar;
}

} // namespace dhad::parser
