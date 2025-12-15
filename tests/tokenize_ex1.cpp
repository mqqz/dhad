#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "../src/lexer/lexer.hpp"
#include "../src/lexer/token_utils.hpp"
#include "../src/lexer/tokens.hpp"

static std::string_view tokenKindToString(TokenType kind) {
  return dhad::lexer::tokenTypeName(kind);
}

int main(int argc, char** argv) {
  std::string path = argc > 1 ? argv[1] : "tests/sources/ex1.dh";

  std::ifstream input(path);
  if (!input) {
    std::cerr << "Failed to open " << path << "\n";
    return 1;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  Lexer lexer(buffer.str());

  while (true) {
    Token tok = lexer.getNextToken();

    if (tok.kind == TokenType::WHITESPACE) {
      continue; // skip noisy whitespace output
    }

    std::cout << tok.loc.line << ":" << tok.loc.column << " " << tokenKindToString(tok.kind);

    if (tok.lexeme) {
      std::cout << " -> " << *tok.lexeme;
    }
    std::cout << "\n";

    if (tok.kind == TokenType::ENDF) {
      break;
    }
  }

  return 0;
}
