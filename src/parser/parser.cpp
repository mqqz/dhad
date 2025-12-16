#include "parser.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#ifdef DHAD_GENERATED_PARSER_TABLE_HEADER
#include DHAD_GENERATED_PARSER_TABLE_HEADER
#else
#include "generated/parser_tables.gen.hpp"
#endif

namespace dhad::parser {

Parser::Parser(Lexer& lexerRef)
    : lexer(lexerRef), grammar(getGrammar()), ruleActions(getRuleActions()),
      ruleLHSIds(getRuleLHSIds()) {
  assert(ruleActions.size() == grammar.size());
  assert(ruleLHSIds.size() == grammar.size());
  stack.push_back(StackEntry{0, SemanticValue{}});
}

Token Parser::nextToken() {
  while (true) {
    Token token = lexer.getNextToken();
    if (token.kind == TokenType::WHITESPACE || token.kind == TokenType::COMMENT) {
      continue;
    }
    return token;
  }
}

void Parser::ensureLookahead() {
  if (!lookahead) {
    lookahead = nextToken();
  }
}

Action Parser::actionFor(int state, TokenType terminal) const {
  if (state < 0 || static_cast<std::size_t>(state) >= kGeneratedActionTable.size()) {
    return makeError();
  }
  const auto& row = kGeneratedActionTable[static_cast<std::size_t>(state)];
  return row[static_cast<std::size_t>(terminal)];
}

int Parser::gotoFor(int state, NonTerminalId symbol) const {
  if (state < 0 || static_cast<std::size_t>(state) >= kGeneratedGotoTable.size()) {
    return -1;
  }
  const auto& row = kGeneratedGotoTable[static_cast<std::size_t>(state)];
  return row[static_cast<std::size_t>(symbol)];
}

SemanticValue Parser::performReduction(int ruleIndex, std::vector<SemanticValue>&& children) {
  const auto index = static_cast<std::size_t>(ruleIndex);
  if (index >= grammar.size() || index >= ruleActions.size()) {
    return SemanticValue{};
  }
  return applyAction(ruleActions[index], std::move(children));
}

ParseResult Parser::makeFailure() const { return ParseResult{}; }

ParseResult Parser::parse() {
  ParseResult result{};
  while (true) {
    ensureLookahead();
    const int state = stack.back().state;
    const TokenType lookaheadKind = lookahead ? lookahead->kind : TokenType::ENDF;
    Action action = actionFor(state, lookaheadKind);

    switch (action.type) {
    case ActionType::Shift: {
      if (!lookahead) {
        return makeFailure();
      }
      stack.push_back(StackEntry{action.value, SemanticValue(std::move(*lookahead))});
      lookahead.reset();
      break;
    }
    case ActionType::Reduce: {
      if (action.value < 0 || static_cast<std::size_t>(action.value) >= grammar.size()) {
        return makeFailure();
      }
      const auto index = static_cast<std::size_t>(action.value);
      const auto& rule = grammar[index];
      std::vector<SemanticValue> children;
      const std::size_t rhsSize = rule.rhs.size();
      children.reserve(rhsSize);
      for (std::size_t i = 0; i < rhsSize; ++i) {
        children.push_back(std::move(stack.back().value));
        stack.pop_back();
        if (stack.empty()) {
          return makeFailure();
        }
      }
      std::reverse(children.begin(), children.end());
      if (index >= ruleLHSIds.size()) {
        return makeFailure();
      }
      const NonTerminalId lhsId = ruleLHSIds[index];
      if (lhsId == NonTerminalId::Count || stack.empty()) {
        return makeFailure();
      }
      SemanticValue reduced = performReduction(action.value, std::move(children));
      const int gotoState = gotoFor(stack.back().state, lhsId);
      if (gotoState < 0) {
        return makeFailure();
      }
      stack.push_back(StackEntry{gotoState, std::move(reduced)});
      break;
    }
    case ActionType::Accept: {
      result.success = true;
      if (!stack.empty() && stack.back().value.hasNode()) {
        result.root = stack.back().value.takeNode();
      }
      return result;
    }
    case ActionType::Error:
    default:
      return makeFailure();
    }
  }
}

ParseResult parseString(std::string source) {
  Lexer lexer(std::move(source));
  Parser parser(lexer);
  return parser.parse();
}

ParseResult parseFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return ParseResult{};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parseString(buffer.str());
}

} // namespace dhad::parser
