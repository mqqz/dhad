#include "../src/pipeline/compiler.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::filesystem::path resolvePath(const std::string& relativePath) {
  const std::filesystem::path direct(relativePath);
  if (std::filesystem::exists(direct)) {
    return direct;
  }
  const std::filesystem::path parent = std::filesystem::path("..") / relativePath;
  if (std::filesystem::exists(parent)) {
    return parent;
  }
  return direct;
}

bool expectSuccessCase(const std::string& file, int expectedExit, std::string_view expectedOutput) {
  const auto path = resolvePath(file);
  dhad::pipeline::RunOptions options;
  options.sourceName = path.string();
  const auto result = dhad::pipeline::runFile(path.string(), options);
  if (!result.success) {
    std::cerr << "Expected success for " << file << ", got failure:\n" << result.stderrBuffer;
    return false;
  }
  if (result.exitCode != expectedExit) {
    std::cerr << "Unexpected exit code for " << file << ": expected " << expectedExit << ", got "
              << result.exitCode << "\n";
    return false;
  }
  if (result.stdoutBuffer != expectedOutput) {
    std::cerr << "Unexpected stdout for " << file << "\nExpected:\n"
              << std::string(expectedOutput) << "\nActual:\n"
              << result.stdoutBuffer << "\n";
    return false;
  }
  return true;
}

bool expectFailureContains(const std::string& file, std::string_view expectedFragment) {
  const auto path = resolvePath(file);
  dhad::pipeline::RunOptions options;
  options.sourceName = path.string();
  const auto result = dhad::pipeline::runFile(path.string(), options);
  if (result.success) {
    std::cerr << "Expected failure for " << file << " but run succeeded\n";
    return false;
  }
  if (result.stderrBuffer.find(expectedFragment) == std::string::npos) {
    std::cerr << "Missing expected diagnostic fragment for " << file << ": "
              << expectedFragment << "\nActual:\n" << result.stderrBuffer;
    return false;
  }
  return true;
}

} // namespace

int main() {
  bool ok = true;

  ok &= expectSuccessCase("tests/sources/float_runtime.dh", 0, "ok\n");
  ok &= expectSuccessCase("tests/sources/struct_runtime.dh", 0, "ok\n");
  ok &= expectSuccessCase("tests/sources/array_index_runtime.dh", 23, "");
  ok &= expectSuccessCase("tests/sources/array_index_write_runtime.dh", 99, "");
  ok &= expectSuccessCase("tests/sources/ex1.dh", 1, "س أقل\n");

  ok &= expectFailureContains("tests/sources/break_outside_loop.dh", "break used outside of a loop");

  return ok ? 0 : 1;
}
