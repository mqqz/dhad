#include "pipeline/compiler.hpp"
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto result = dhad::pipeline::runFile(argv[1]);
  std::cout << result.stdoutBuffer;
  std::cerr << result.stderrBuffer;
  return result.success ? result.exitCode : 1;
}
