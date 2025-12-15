#pragma once

#include "../lexer/tokens.hpp"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <type_traits>

namespace dhad::parser {

enum class ActionType { Shift, Reduce, Accept, Error };

struct Action {
  ActionType type;
  int value; // target state for shift, rule index for reduce
};

struct LR0Item {
  int production;
  int dot;
};

struct LR0StateRange {
  std::size_t offset;
  std::size_t count;
};

constexpr Action makeShift(int state) { return Action{ActionType::Shift, state}; }
constexpr Action makeReduce(int rule) { return Action{ActionType::Reduce, rule}; }
constexpr Action makeAccept() { return Action{ActionType::Accept, 0}; }
constexpr Action makeError() { return Action{ActionType::Error, -1}; }

struct ActionEntry {
  TokenType terminal;
  Action action;
};

constexpr std::size_t kTerminalCount = static_cast<std::size_t>(TokenType::Count);

template <std::size_t TokenCount = kTerminalCount>
constexpr std::array<Action, TokenCount> makeActionRow(std::initializer_list<ActionEntry> entries) {
  std::array<Action, TokenCount> row{};
  for (auto& cell : row) {
    cell = makeError();
  }
  for (auto entry : entries) {
    row[static_cast<std::size_t>(entry.terminal)] = entry.action;
  }
  return row;
}

#define NONTERM(name) name,
enum class NonTerminalId : std::size_t {
#include "nonterminals.def"
  Count
};
#undef NONTERM

constexpr std::size_t kNonTerminalCount = static_cast<std::size_t>(NonTerminalId::Count);

struct GotoEntry {
  NonTerminalId symbol;
  int state;
};

template <std::size_t NonTermCount = kNonTerminalCount>
constexpr std::array<int, NonTermCount> makeGotoRow(std::initializer_list<GotoEntry> entries) {
  std::array<int, NonTermCount> row{};
  for (auto& cell : row) {
    cell = -1;
  }
  for (auto entry : entries) {
    row[static_cast<std::size_t>(entry.symbol)] = entry.state;
  }
  return row;
}

template <typename... Rows> constexpr auto makeActionTable(Rows... rows) {
  static_assert((std::is_same_v<Rows, std::array<Action, kTerminalCount>> && ...),
                "Action rows must be std::array<Action, kTerminalCount>");
  return std::array<std::array<Action, kTerminalCount>, sizeof...(Rows)>{rows...};
}

template <typename... Rows> constexpr auto makeGotoTable(Rows... rows) {
  static_assert((std::is_same_v<Rows, std::array<int, kNonTerminalCount>> && ...),
                "Goto rows must be std::array<int, kNonTerminalCount>");
  return std::array<std::array<int, kNonTerminalCount>, sizeof...(Rows)>{rows...};
}

#define ACTION_ENTRY(token, actionExpr)                                                            \
  ::dhad::parser::ActionEntry { TokenType::token, actionExpr }
#define ACTION_ROW(...) ::dhad::parser::makeActionRow({__VA_ARGS__})
#define ACTION_TABLE(...) ::dhad::parser::makeActionTable(__VA_ARGS__)

#define GOTO_ENTRY(symbol, stateValue)                                                             \
  ::dhad::parser::GotoEntry { ::dhad::parser::NonTerminalId::symbol, stateValue }
#define GOTO_ROW(...) ::dhad::parser::makeGotoRow({__VA_ARGS__})
#define GOTO_TABLE(...) ::dhad::parser::makeGotoTable(__VA_ARGS__)

} // namespace dhad::parser
