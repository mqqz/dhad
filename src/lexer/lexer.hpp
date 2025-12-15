#pragma once

#include "tokens.hpp"
#include <optional>
#include <string>

class Lexer {
public:
  explicit Lexer(std::string source);

  Token getNextToken();

private:
  struct DecodedChar {
    char32_t codepoint;
    size_t byteCount;
    bool valid;
  };

  std::optional<DecodedChar> decodeNext();
  std::optional<DecodedChar> peekChar();
  std::optional<DecodedChar> getChar();

  bool isWhitespace(char32_t cp) const;
  bool isDigit(char32_t cp) const;
  bool isIdentChar(char32_t cp) const;

  Token readWhitespace(SourceLocation start);
  Token readLitStr(SourceLocation start, size_t tokenStartByte);
  Token readLitNum(SourceLocation start, size_t tokenStartByte);
  Token readWord(SourceLocation start, size_t tokenStartByte);

  std::string source;
  size_t bytePos = 0;
  size_t line = 1;
  size_t column = 1;
};
