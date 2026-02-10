#include "lexer.hpp"
#include <unordered_map>

static const std::unordered_map<std::string, TokenType> kwTable = [] {
  std::unordered_map<std::string, TokenType> table;
  for (std::size_t i = 0; i < static_cast<std::size_t>(TokenType::Count); ++i) {
    auto type = static_cast<TokenType>(i);
    if (const char* keyword = dhad::lexer::tokenKeywordLexeme(type)) {
      if (keyword[0] != '\0') {
        table.emplace(keyword, type);
      }
    }
  }
  return table;
}();

static const std::unordered_map<char32_t, TokenType> singleCharTokens = {
    {U'(', TokenType::LPAREN},  {U')', TokenType::RPAREN},    {U'{', TokenType::LBRACE},
    {U'}', TokenType::RBRACE},  {U'[', TokenType::LBRACKET},  {U']', TokenType::RBRACKET},
    {U',', TokenType::COMMA},   {U'،', TokenType::COMMA},     {U'؛', TokenType::SEMICOLON},
    {U':', TokenType::COLON},   {U'.', TokenType::DOT},
    {U'=', TokenType::ASSIGN},  {U'+', TokenType::PLUS},      {U'-', TokenType::MINUS},
    {U'*', TokenType::MUL},     {U'/', TokenType::DIV},       {U'<', TokenType::LESS},
    {U'>', TokenType::GREATER}, {U'|', TokenType::PIPE},
};

Lexer::Lexer(std::string source) : source(std::move(source)) {}

std::optional<Lexer::DecodedChar> Lexer::decodeNext() {
  if (bytePos >= source.size()) {
    return std::nullopt;
  }

  auto lead = static_cast<unsigned char>(source[bytePos]);
  char32_t cp = 0;
  size_t byteCount = 1;
  bool valid = true;

  if (lead < 0x80) {
    cp = lead;
  } else if ((lead >> 5) == 0x6) {
    byteCount = 2;
    cp = lead & 0x1F;
  } else if ((lead >> 4) == 0xE) {
    byteCount = 3;
    cp = lead & 0x0F;
  } else if ((lead >> 3) == 0x1E) {
    byteCount = 4;
    cp = lead & 0x07;
  } else {
    valid = false;
  }

  if (bytePos + byteCount > source.size()) {
    return DecodedChar{U'\uFFFD', 1, false};
  }

  for (size_t i = 1; i < byteCount; ++i) {
    auto b = static_cast<unsigned char>(source[bytePos + i]);
    if ((b & 0xC0) != 0x80) {
      valid = false;
      break;
    }
    cp = (cp << 6) | (b & 0x3F);
  }

  if (!valid) {
    return DecodedChar{U'\uFFFD', 1, false};
  }

  return DecodedChar{cp, byteCount, true};
}

std::optional<Lexer::DecodedChar> Lexer::peekChar() { return decodeNext(); }

std::optional<Lexer::DecodedChar> Lexer::getChar() {
  auto decoded = decodeNext();
  if (!decoded) {
    return std::nullopt;
  }

  bytePos += decoded->byteCount;

  if (decoded->codepoint == U'\n') {
    line++;
    column = 1;
  } else {
    column++;
  }

  return decoded;
}

bool Lexer::isDigit(char32_t cp) const {
  return (cp >= U'0' && cp <= U'9') || (cp >= 0x0660 && cp <= 0x0669) || // Arabic-Indic digits
         (cp >= 0x06F0 && cp <= 0x06F9); // Extended Arabic-Indic digits
}

bool Lexer::isIdentChar(char32_t cp) const {
  if (cp == U'_') {
    return true;
  }
  if (cp == U'؛' || cp == U'،' || cp == U'.') {
    return false;
  }

  bool asciiAlpha = (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z');
  return asciiAlpha || isDigit(cp) || cp > 0x7F;
}

Token Lexer::readWhitespace(SourceLocation start) {
  while (auto c = peekChar()) {
    if (!c->valid || !isWhitespace(c->codepoint)) {
      break;
    }
    getChar();
  }
  return Token{TokenType::WHITESPACE, std::nullopt, start};
}

Token Lexer::readLitStr(SourceLocation start, size_t tokenStartByte) {
  // tokenStartByte points at the opening quote (already consumed).
  while (auto ch = getChar()) {
    if (!ch->valid) {
      std::string lexeme = source.substr(tokenStartByte, bytePos - tokenStartByte);
      return Token{TokenType::INVALID, lexeme, start};
    }
    if (ch->codepoint == U'"') {
      std::string lexeme = source.substr(tokenStartByte, bytePos - tokenStartByte);
      return Token{TokenType::LIT_STRING, lexeme, start};
    }
    if (ch->codepoint == U'\n' || ch->codepoint == U'\r') {
      std::string lexeme = source.substr(tokenStartByte, bytePos - tokenStartByte);
      return Token{TokenType::INVALID, lexeme, start};
    }
  }
  std::string lexeme = source.substr(tokenStartByte, bytePos - tokenStartByte);
  return Token{TokenType::INVALID, lexeme, start};
}

Token Lexer::readLitNum(SourceLocation start, size_t tokenStartByte) {
  bool seenDot = false;
  while (auto ch = peekChar()) {
    if (!ch->valid) {
      break;
    }
    if (isDigit(ch->codepoint)) {
      getChar();
      continue;
    }
    if (ch->codepoint == U'.' && !seenDot) {
      seenDot = true;
      getChar();
      continue;
    }
    break;
  }

  size_t tokenEndByte = bytePos;
  if (tokenEndByte == tokenStartByte) {
    return Token{TokenType::INVALID, std::nullopt, start};
  }

  std::string lexeme = source.substr(tokenStartByte, tokenEndByte - tokenStartByte);
  return Token{TokenType::LIT_NUM, lexeme, start};
}

Token Lexer::readWord(SourceLocation start, size_t tokenStartByte) {
  bool consumed = false;
  while (auto ch = peekChar()) {
    if (!ch->valid) {
      getChar();
      consumed = true;
      continue;
    }
    if (isIdentChar(ch->codepoint)) {
      getChar();
      consumed = true;
    } else {
      break;
    }
  }

  size_t tokenEndByte = bytePos;
  if (!consumed) {
    // Unrecognized character: consume one code unit to avoid stalling.
    getChar();
    tokenEndByte = bytePos;
    std::string badLexeme = source.substr(tokenStartByte, tokenEndByte - tokenStartByte);
    return Token{TokenType::INVALID, badLexeme, start};
  }

  std::string lexeme = source.substr(tokenStartByte, tokenEndByte - tokenStartByte);
  if (auto it = kwTable.find(lexeme); it != kwTable.end()) {
    return Token{it->second, lexeme, start}; // keyword
  }
  return Token{TokenType::IDENTIFIER, lexeme, start}; // identifier
}

Token Lexer::getNextToken() {
  auto ch = peekChar();
  if (!ch) {
    return Token{TokenType::ENDF, std::nullopt, {line, column}};
  }

  SourceLocation start{line, column};
  size_t tokenStartByte = bytePos;

  if (!ch->valid) {
    getChar();
    return Token{TokenType::INVALID, std::nullopt, start};
  }

  if (isWhitespace(ch->codepoint)) {
    return readWhitespace(start);
  }

  if (ch->codepoint == U'"') {
    getChar(); // consume opening quote
    return readLitStr(start, tokenStartByte);
  }
  if (isDigit(ch->codepoint)) {
    return readLitNum(start, tokenStartByte);
  }

  if (ch->codepoint == U'=') {
    getChar();
    if (auto next = peekChar(); next && next->valid && next->codepoint == U'=') {
      getChar();
      return Token{TokenType::EQUAL, std::nullopt, start};
    }
    return Token{TokenType::ASSIGN, std::nullopt, start};
  }

  if (ch->codepoint == U'!') {
    getChar();
    if (auto next = peekChar(); next && next->valid && next->codepoint == U'=') {
      getChar();
      return Token{TokenType::NOT_EQUAL, std::nullopt, start};
    }
    return Token{TokenType::INVALID, std::nullopt, start};
  }

  if (ch->codepoint == U'<') {
    getChar();
    if (auto next = peekChar(); next && next->valid && next->codepoint == U'=') {
      getChar();
      return Token{TokenType::LESS_EQUAL, std::nullopt, start};
    }
    return Token{TokenType::LESS, std::nullopt, start};
  }

  if (ch->codepoint == U'>') {
    getChar();
    if (auto next = peekChar(); next && next->valid && next->codepoint == U'=') {
      getChar();
      return Token{TokenType::GREATER_EQUAL, std::nullopt, start};
    }
    return Token{TokenType::GREATER, std::nullopt, start};
  }

  if (auto it = singleCharTokens.find(ch->codepoint); it != singleCharTokens.end()) {
    getChar();
    return Token{it->second, std::nullopt, start};
  }

  return readWord(start, tokenStartByte); // ie identifier or keyword
}
