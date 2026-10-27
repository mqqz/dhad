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

std::string jsonString(const std::string& value) {
  std::string out = "\"";
  const char hex[] = "0123456789abcdef";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      out += "\\u00";
      out += hex[c >> 4];
      out += hex[c & 15];
    } else out += static_cast<char>(c);
  }
  return out + "\"";
}

char* envelope(bool success, const std::string& stdoutText, const std::string& stderrText,
               const std::string& ast, int exitCode) {
  return allocCString(std::string("{\"success\":") + (success ? "true" : "false") +
                      ",\"stdout\":" + jsonString(stdoutText) +
                      ",\"stderr\":" + jsonString(stderrText) + ",\"ast\":" + jsonString(ast) +
                      ",\"exitCode\":" + std::to_string(exitCode) + "}");
}
char* failCString(const std::string& message) {
  return envelope(false, "", "error: " + message, "", 1);
}

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
  return envelope(result.success, result.stdoutBuffer, result.stderrBuffer, result.astDump,
                  result.success ? 0 : 1);
}

DHAD_WEB_EXPORT char* dhad_run(const char* source) {
  if (!source) {
    return failCString("null source");
  }

  dhad::pipeline::RunOptions options;
  options.sourceName = "<memory>";

  auto result = dhad::pipeline::runString(std::string(source), options);
  return envelope(result.success, result.stdoutBuffer, result.stderrBuffer, "", result.exitCode);
}

DHAD_WEB_EXPORT void dhad_free(char* ptr) { std::free(ptr); }

} // extern "C"
