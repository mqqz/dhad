#pragma once

#include "tokens.hpp"

#include <array>
#include <string_view>

namespace dhad::lexer {

using ::TokenType;

namespace detail {

constexpr std::array<std::string_view, static_cast<std::size_t>(TokenType::Count)>
makeTokenTypeNames() {
  return {"ENDF",      "INVALID", "COMMENT",    "IDENTIFIER", "LIT_NUM",       "LIT_STRING",
// KEYWORDS
#define KEYWORD(ar, kw) #kw,
#include "keywords.def"
#undef KEYWORD
          "PLUS",      "MINUS",   "MUL",        "DIV",        "ASSIGN",        "EQUAL",
          "NOT_EQUAL", "LESS",    "LESS_EQUAL", "GREATER",    "GREATER_EQUAL", "WHITESPACE",
          "LPAREN",    "RPAREN",  "LBRACE",     "RBRACE",     "COMMA",         "SEMICOLON",
          "COLON"};
}

constexpr auto kTokenTypeNames = makeTokenTypeNames();

} // namespace detail

constexpr std::string_view tokenTypeName(TokenType type) {
  const auto index = static_cast<std::size_t>(type);
  if (index >= detail::kTokenTypeNames.size()) {
    return "UNKNOWN";
  }
  return detail::kTokenTypeNames[index];
}

static_assert(detail::kTokenTypeNames.size() == static_cast<std::size_t>(TokenType::Count),
              "TokenType -> name table must cover every token");

} // namespace dhad::lexer
