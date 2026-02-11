#include "../src/pipeline/compiler.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

bool resolverSuccessCase() {
  const std::string mainSource = u8R"(استورد util؛
دالة بداية(): عدد {
  اطبع(حي())؛
  أعد 0؛
}
)";

  const std::string utilSource = u8R"(دالة حي(): نص {
  أعد "ok"؛
}
)";

  std::unordered_map<std::string, std::string> modules;
  modules.emplace("util", utilSource);
  modules.emplace("util.dh", utilSource);

  dhad::pipeline::RunOptions options;
  options.sourceName = "<memory>";
  options.moduleResolver = [&modules](std::string_view module, std::string_view) {
    auto it = modules.find(std::string(module));
    if (it == modules.end()) {
      return std::optional<dhad::pipeline::ResolvedModule>{};
    }
    return std::optional<dhad::pipeline::ResolvedModule>{
        dhad::pipeline::ResolvedModule{std::string(module), it->second}};
  };

  const auto result = dhad::pipeline::runString(mainSource, options);
  if (!result.success) {
    std::cerr << "Resolver success case failed:\n" << result.stderrBuffer;
    return false;
  }
  if (result.stdoutBuffer != "ok\n") {
    std::cerr << "Resolver success case unexpected stdout:\n" << result.stdoutBuffer;
    return false;
  }
  if (result.exitCode != 0) {
    std::cerr << "Resolver success case unexpected exit code: " << result.exitCode << "\n";
    return false;
  }
  return true;
}

bool resolverMissingImportCase() {
  const std::string source = u8R"(استورد missing؛
دالة بداية(): عدد {
  أعد 0؛
}
)";

  dhad::pipeline::RunOptions options;
  options.sourceName = "<memory>";
  options.moduleResolver = [](std::string_view, std::string_view) {
    return std::optional<dhad::pipeline::ResolvedModule>{};
  };

  const auto result = dhad::pipeline::runString(source, options);
  if (result.success) {
    std::cerr << "Resolver missing import case unexpectedly succeeded\n";
    return false;
  }
  if (result.stderrBuffer.find("import 'missing' not found") == std::string::npos) {
    std::cerr << "Resolver missing import diagnostic mismatch:\n" << result.stderrBuffer;
    return false;
  }
  if (result.stderrBuffer.find("(expected ") != std::string::npos) {
    std::cerr << "Resolver missing import should not include filesystem expected-path hint:\n"
              << result.stderrBuffer;
    return false;
  }
  return true;
}

} // namespace

int main() {
  bool ok = true;
  ok &= resolverSuccessCase();
  ok &= resolverMissingImportCase();
  return ok ? 0 : 1;
}
