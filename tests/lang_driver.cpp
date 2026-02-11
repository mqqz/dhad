#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "../src/lexer/lexer.hpp"
#include "../src/lexer/tokens.hpp"
#include "../src/parser/parser.hpp"

namespace {

enum class Mode { Lexer, Parser };

std::optional<Mode> parseMode(std::string_view arg) {
  if (arg == "lexer") {
    return Mode::Lexer;
  }
  if (arg == "parser") {
    return Mode::Parser;
  }
  return std::nullopt;
}

void emitUsage(const char* program) {
  std::cerr << "Usage: " << program << " --mode <lexer|parser> [--input path]\n";
}

std::string_view tokenKindToString(TokenType kind) { return dhad::lexer::tokenTypeName(kind); }

int runLexer(const std::string& inputPath) {
  std::ifstream input(inputPath);
  if (!input) {
    std::cerr << "Failed to open " << inputPath << "\n";
    return 1;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  Lexer lexer(buffer.str());

  while (true) {
    Token tok = lexer.getNextToken();
    if (tok.kind == TokenType::WHITESPACE) {
      continue;
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

int runParser(const std::string& inputPath) {
  auto result = dhad::parser::parseFile(inputPath);
  if (!result.success || !result.root) {
    std::cerr << "Failed to parse " << inputPath << "\n";
    return 1;
  }
  std::cout << "Parse succeeded: " << inputPath << "\n";
  result.root->dump(std::cout);
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  std::string modeArg;
  std::string inputPath = "tests/sources/ex1.dh";

  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (arg == "--mode" && i + 1 < argc) {
      modeArg = argv[++i];
      continue;
    }
    if (arg == "--input" && i + 1 < argc) {
      inputPath = argv[++i];
      continue;
    }
    if (arg == "--help") {
      emitUsage(argv[0]);
      return 0;
    }
    if (arg.rfind("--", 0) == 0) {
      std::cerr << "Unknown option: " << arg << "\n";
      emitUsage(argv[0]);
      return 1;
    }
    inputPath = std::string(arg);
  }

  if (modeArg.empty()) {
    std::cerr << "Missing --mode argument\n";
    emitUsage(argv[0]);
    return 1;
  }

  const auto mode = parseMode(modeArg);
  if (!mode) {
    std::cerr << "Invalid mode: " << modeArg << "\n";
    emitUsage(argv[0]);
    return 1;
  }

  switch (*mode) {
  case Mode::Lexer:
    return runLexer(inputPath);
  case Mode::Parser:
    return runParser(inputPath);
  }
  return 1;
}
