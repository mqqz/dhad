#pragma once

#include "../ast/ast.hpp"
#include "../lexer/tokens.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dhad::pipeline {

enum class DiagnosticStage {
  Parse,
  ImportResolution,
  TypeCheck,
  CodeGen,
  Io,
};

struct Diagnostic {
  DiagnosticStage stage{DiagnosticStage::Parse};
  std::string sourceName;
  std::optional<SourceLocation> location;
  std::string message;
};

struct ResolvedModule {
  std::string sourceName;
  std::string source;
};

using ModuleResolver =
    std::function<std::optional<ResolvedModule>(std::string_view moduleName,
                                                std::string_view importerSourceName)>;

struct CompileOptions {
  std::string sourceName{"<memory>"};
  std::string moduleName{"dhad"};
  bool includeAstDump{false};
  bool includeIR{true};
  ModuleResolver moduleResolver;
};

struct CompileResult {
  bool success{false};
  std::vector<Diagnostic> diagnostics;
  std::string astDump;
  std::string ir;
  std::string stdoutBuffer;
  std::string stderrBuffer;
};

CompileResult compileString(std::string source, const CompileOptions& options = {});
CompileResult compileFile(const std::string& path, const CompileOptions& options = {});

} // namespace dhad::pipeline
