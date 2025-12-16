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
#define ACTION(name) name
#define RULE(lhs, action, ...) ProductionRule(nt::lhs, {__VA_ARGS__})
#define RULE_EMPTY(lhs, action) ProductionRule(nt::lhs, {})

  static const std::vector<ProductionRule> grammar = {
#include "grammar.def"
  };

#undef RULE_EMPTY
#undef ACTION
#undef RULE
#undef TOK
#undef NT

  return grammar;
}

const std::vector<ActionId>& getRuleActions() {
  static const std::vector<ActionId> actions = {
#define ACTION(name) ActionId::name
#define NT(name) name
#define TOK(token) token
#define RULE(lhs, action, ...) action
#define RULE_EMPTY(lhs, action) action
#include "grammar.def"
#undef RULE_EMPTY
#undef RULE
#undef TOK
#undef NT
#undef ACTION
  };
  return actions;
}

const std::vector<NonTerminalId>& getRuleLHSIds() {
  static const std::vector<NonTerminalId> lhs = {
#define ACTION(name) name
#define NT(name) NonTerminalId::name
#define TOK(token) token
#define RULE(lhs, action, ...) NT(lhs)
#define RULE_EMPTY(lhs, action) NT(lhs)
#include "grammar.def"
#undef RULE_EMPTY
#undef RULE
#undef TOK
#undef NT
#undef ACTION
  };
  return lhs;
}

} // namespace dhad::parser
