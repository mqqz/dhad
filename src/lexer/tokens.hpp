#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class TokenType : uint8_t {
#define TOKEN(name, canonical, keyword) name,
#include "tokens.def"
#undef TOKEN
  Count
};

struct SourceLocation {
  size_t line;
  size_t column;
};

struct Token {
  TokenType kind;
  std::optional<std::string> lexeme; // raw text
  SourceLocation loc;
};

namespace dhad::lexer {

namespace detail {

constexpr std::size_t tokenIndex(TokenType type) { return static_cast<std::size_t>(type); }

constexpr auto makeTokenTypeNames() {
  std::array<std::string_view, static_cast<std::size_t>(TokenType::Count)> names{};
#define TOKEN(name, canonical, keyword) names[tokenIndex(TokenType::name)] = #name;
#include "tokens.def"
#undef TOKEN
  return names;
}

constexpr auto makeCanonicalLexemes() {
  std::array<std::string_view, static_cast<std::size_t>(TokenType::Count)> values{};
#define TOKEN(name, canonical, keyword) values[tokenIndex(TokenType::name)] = canonical;
#include "tokens.def"
#undef TOKEN
  return values;
}

constexpr auto makeKeywordLexemes() {
  std::array<const char*, static_cast<std::size_t>(TokenType::Count)> values{};
#define TOKEN(name, canonical, keyword) values[tokenIndex(TokenType::name)] = keyword;
#include "tokens.def"
#undef TOKEN
  return values;
}

constexpr auto kTokenTypeNames = makeTokenTypeNames();
constexpr auto kCanonicalLexemes = makeCanonicalLexemes();
constexpr auto kKeywordLexemes = makeKeywordLexemes();

} // namespace detail

constexpr std::string_view tokenTypeName(TokenType type) {
  const auto index = detail::tokenIndex(type);
  if (index >= detail::kTokenTypeNames.size()) {
    return "UNKNOWN";
  }
  return detail::kTokenTypeNames[index];
}

constexpr std::string_view tokenCanonicalLexeme(TokenType type) {
  const auto index = detail::tokenIndex(type);
  if (index >= detail::kCanonicalLexemes.size()) {
    return {};
  }
  return detail::kCanonicalLexemes[index];
}

constexpr const char* tokenKeywordLexeme(TokenType type) {
  const auto index = detail::tokenIndex(type);
  if (index >= detail::kKeywordLexemes.size()) {
    return nullptr;
  }
  return detail::kKeywordLexemes[index];
}

static_assert(detail::kTokenTypeNames.size() == static_cast<std::size_t>(TokenType::Count),
              "TokenType name table must cover each token");

} // namespace dhad::lexer
