#include <fstream>
#include <iostream>
#include <string>

#include "../src/codegen/codegen.hpp"
#include "../src/parser/parser.hpp"

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

  dhad::codegen::CodeGenModule codegen("dhad_module");
  if (!codegen.generate(*program)) {
    std::cerr << "Code generation failed\n";
    return 1;
  }

  codegen.module().print(llvm::outs(), nullptr);
  return 0;
}
