#pragma once

#include <cstdint>
#include <optional>
#include <string>

enum class TokenType : uint8_t {
  // Special
  ENDF,
  INVALID,
  COMMENT,

  // IDENTIFIERS & LITERALS
  IDENTIFIER,
  LIT_NUM,
  LIT_STRING,

// KEYWORDS
#define KEYWORD(ar, kw) kw,
#include "keywords.def"
#undef KEYWORD

  // OPERATORS
  PLUS,          // +
  MINUS,         // -
  MUL,           // *
  DIV,           // /
  ASSIGN,        // =
  EQUAL,         // ==
  NOT_EQUAL,     // !=
  LESS,          // <
  LESS_EQUAL,    // <=
  GREATER,       // >
  GREATER_EQUAL, // >=

  // DELIMITERS
  WHITESPACE,
  LPAREN,    // (
  RPAREN,    // )
  LBRACE,    // {
  RBRACE,    // }
  COMMA,     // ,
  SEMICOLON, // ;
  COLON,     // :

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
