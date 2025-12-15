#include <fstream>
#include <iostream>
#include <string>

#include "../src/codegen/codegen.hpp"
#include "../src/parser/parser.hpp"
#include "../src/typing/checker.hpp"

#include <llvm/Support/raw_ostream.h>

namespace {

std::string resolvePath(std::string path) {
  std::ifstream file(path);
  if (file.good()) {
    return path;
  }
  std::string alt = "../" + path;
  std::ifstream altStream(alt);
  if (altStream.good()) {
    return alt;
  }
  return path;
}

void printTypeErrors(const dhad::typing::TypeCheckerResult& result, const std::string& source) {
  for (const auto& error : result.errors) {
    std::cerr << dhad::typing::formatTypeError(error, source) << "\n";
  }
}

} // namespace

int main(int argc, char** argv) {
  std::string path = resolvePath(argc > 1 ? argv[1] : "tests/sources/ex1.dh");

  auto parseResult = dhad::parser::parseFile(path);
  if (!parseResult.success || !parseResult.root) {
    std::cerr << "Failed to parse " << path << "\n";
    return 1;
  }

  auto* program = llvm::dyn_cast<dhad::ast::Program>(parseResult.root.get());
  if (!program) {
    std::cerr << "Parse tree root is not a Program\n";
    return 1;
  }

  dhad::typing::TypeChecker checker;
  auto typeResult = checker.check(*program);
  if (!typeResult.success) {
    printTypeErrors(typeResult, path);
    return 1;
  }

  dhad::codegen::CodeGenModule codegen("dhad_module");
  if (!codegen.generate(*program)) {
    std::cerr << "Code generation failed\n";
    return 1;
  }

  codegen.module().print(llvm::outs(), nullptr);
  return 0;
}
