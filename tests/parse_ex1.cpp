#include <iostream>
#include <string>

#include "../src/parser/parser.hpp"

#include <llvm/Support/raw_ostream.h>

int main(int argc, char** argv) {
  std::string path = argc > 1 ? argv[1] : "tests/sources/ex1.dh";

  auto result = dhad::parser::parseFile(path);
  if (!result.success || !result.root) {
    std::cerr << "Failed to parse " << path << "\n";
    return 1;
  }

  llvm::outs() << "Parse succeeded: " << path << "\n";
  result.root->dump(llvm::outs());
  return 0;
}
