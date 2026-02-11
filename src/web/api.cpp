#include "pipeline/compiler.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define DHAD_WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define DHAD_WEB_EXPORT
#endif

namespace {

char* allocCString(const std::string& text) {
  char* buffer = static_cast<char*>(std::malloc(text.size() + 1));
  if (!buffer) {
    return nullptr;
  }
  std::memcpy(buffer, text.c_str(), text.size() + 1);
  return buffer;
}

char* failCString(const std::string& message) { return allocCString("error: " + message); }

} // namespace

extern "C" {

DHAD_WEB_EXPORT char* dhad_parse_ast(const char* source) {
  if (!source) {
    return failCString("null source");
  }

  dhad::pipeline::CompileOptions options;
  options.sourceName = "<memory>";
  options.includeAstDump = true;
  options.includeIR = false;

  auto result = dhad::pipeline::compileString(std::string(source), options);
  if (!result.success) {
    return allocCString(result.stderrBuffer);
  }
  return allocCString(result.astDump);
}

DHAD_WEB_EXPORT char* dhad_run(const char* source) {
  if (!source) {
    return failCString("null source");
  }

  dhad::pipeline::RunOptions options;
  options.sourceName = "<memory>";

  auto result = dhad::pipeline::runString(std::string(source), options);
  if (!result.success) {
    return allocCString(result.stderrBuffer);
  }
  return allocCString(result.stdoutBuffer);
}

DHAD_WEB_EXPORT void dhad_free(char* ptr) { std::free(ptr); }

} // extern "C"
