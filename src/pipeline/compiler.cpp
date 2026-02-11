#include "pipeline/compiler.hpp"

#if DHAD_ENABLE_CODEGEN
#include "../codegen/codegen.hpp"
#endif
#include "../interp/interpreter.hpp"
#include "../parser/parser.hpp"
#include "../std/identifiers.hpp"
#include "../typing/checker.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dhad::pipeline {

namespace {

using ImportSet = std::unordered_set<std::string>;

struct PipelinePrepOptions {
  std::string sourceName;
  ModuleResolver moduleResolver;
};

struct PreparedProgram {
  std::unique_ptr<ast::ASTNode> root;
  ast::Program* program{nullptr};
};

template <typename ResultT> void appendDiagnostic(ResultT& out, Diagnostic diagnostic) {
  std::ostringstream line;
  if (!diagnostic.sourceName.empty()) {
    line << diagnostic.sourceName;
    if (diagnostic.location) {
      line << ":" << diagnostic.location->line << ":" << diagnostic.location->column;
    }
    line << ": ";
  }
  line << "error: " << diagnostic.message;
  out.stderrBuffer += line.str();
  out.stderrBuffer += "\n";
  out.diagnostics.push_back(std::move(diagnostic));
}

std::string pathKey(const std::filesystem::path& path) {
  return std::filesystem::absolute(path).lexically_normal().string();
}

std::filesystem::path resolveImportPath(std::string_view module, std::string_view sourceName) {
  std::filesystem::path modulePath(module);
  if (modulePath.extension() != ".dh") {
    modulePath += ".dh";
  }
  return std::filesystem::path(sourceName).parent_path() / modulePath;
}

bool isStdModuleImport(std::string_view moduleName) {
  if (moduleName == identifiers::kStdModule) {
    return true;
  }
  std::string withExtension(identifiers::kStdModule);
  withExtension += ".dh";
  return moduleName == withExtension;
}

std::optional<ResolvedModule> resolveModule(const ModuleResolver& resolver, std::string_view moduleName,
                                            std::string_view importerSourceName) {
  if (resolver) {
    return resolver(moduleName, importerSourceName);
  }

  const auto resolvedPath = resolveImportPath(moduleName, importerSourceName);
  std::ifstream input(resolvedPath);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return ResolvedModule{resolvedPath.string(), buffer.str()};
}

template <typename ResultT>
bool mergeImports(ast::Program& program, std::string_view sourceName, const PipelinePrepOptions& options,
                  ImportSet& visited, ResultT& out) {
  std::vector<ast::NodePtr<ast::ASTNode>> merged;

  for (auto& node : program.topLevel) {
    auto* importDecl = llvm::dyn_cast<ast::ImportDecl>(node.get());
    if (!importDecl) {
      continue;
    }
    if (isStdModuleImport(importDecl->module)) {
      continue;
    }

    const auto expectedPath = resolveImportPath(importDecl->module, sourceName);
    auto module = resolveModule(options.moduleResolver, importDecl->module, sourceName);
    if (!module) {
      std::string message = "import '" + importDecl->module + "' not found";
      if (!options.moduleResolver) {
        message += " (expected " + expectedPath.string() + ")";
      }
      appendDiagnostic(
          out, Diagnostic{DiagnosticStage::ImportResolution, "", importDecl->getLocation(), message});
      return false;
    }

    const std::string visitedKey = module->sourceName.empty()
                                       ? importDecl->module
                                       : pathKey(std::filesystem::path(module->sourceName));
    if (!visited.insert(visitedKey).second) {
      continue;
    }

    auto parseResult = parser::parseString(module->source);
    if (!parseResult.success || !parseResult.root) {
      appendDiagnostic(out,
                       Diagnostic{DiagnosticStage::Parse, "", importDecl->getLocation(),
                                  "failed to parse import '" + importDecl->module + "'"});
      return false;
    }

    auto* importedProgram = llvm::dyn_cast<ast::Program>(parseResult.root.get());
    if (!importedProgram) {
      appendDiagnostic(out, Diagnostic{DiagnosticStage::ImportResolution, "", importDecl->getLocation(),
                                       "import '" + importDecl->module +
                                           "' did not produce a Program root"});
      return false;
    }

    if (!mergeImports(*importedProgram, module->sourceName, options, visited, out)) {
      return false;
    }

    for (auto& importedNode : importedProgram->topLevel) {
      if (!llvm::isa<ast::ImportDecl>(importedNode.get())) {
        merged.push_back(std::move(importedNode));
      }
    }
  }

  for (auto& node : program.topLevel) {
    if (!llvm::isa<ast::ImportDecl>(node.get())) {
      merged.push_back(std::move(node));
    }
  }

  program.topLevel = std::move(merged);
  return true;
}

template <typename ResultT>
bool prepareProgram(std::string source, const PipelinePrepOptions& options, ResultT& out,
                    PreparedProgram& prepared) {
  auto parseResult = parser::parseString(std::move(source));
  if (!parseResult.success || !parseResult.root) {
    appendDiagnostic(out, Diagnostic{DiagnosticStage::Parse, "", std::nullopt,
                                     "failed to parse " + options.sourceName});
    return false;
  }

  auto* program = llvm::dyn_cast<ast::Program>(parseResult.root.get());
  if (!program) {
    appendDiagnostic(out, Diagnostic{DiagnosticStage::Parse, "", std::nullopt,
                                     "parser did not produce a Program root"});
    return false;
  }

  ImportSet visited;
  if (!options.sourceName.empty() && options.sourceName != "<memory>") {
    visited.insert(pathKey(std::filesystem::path(options.sourceName)));
  }
  if (!mergeImports(*program, options.sourceName, options, visited, out)) {
    return false;
  }

  typing::TypeChecker checker;
  const auto typeResult = checker.check(*program);
  if (!typeResult.success) {
    for (const auto& error : typeResult.errors) {
      appendDiagnostic(out, Diagnostic{DiagnosticStage::TypeCheck, options.sourceName, error.location,
                                       error.message});
    }
    return false;
  }

  prepared.program = program;
  prepared.root = std::move(parseResult.root);
  return true;
}

} // namespace

CompileResult compileString(std::string source, const CompileOptions& options) {
  CompileResult out;
  const PipelinePrepOptions prepOptions{options.sourceName, options.moduleResolver};
  PreparedProgram prepared;

  if (!prepareProgram(std::move(source), prepOptions, out, prepared)) {
    return out;
  }

  if (options.includeAstDump) {
    std::ostringstream astStream;
    prepared.program->dump(astStream);
    out.astDump = astStream.str();
  }

  if (options.includeIR) {
#if DHAD_ENABLE_CODEGEN
    codegen::CodeGenModule codegen(options.moduleName);
    if (!codegen.generate(*prepared.program)) {
      appendDiagnostic(out, Diagnostic{DiagnosticStage::CodeGen, "", std::nullopt,
                                       codegen.lastError().empty() ? "code generation failed"
                                                                    : codegen.lastError()});
      return out;
    }
    out.ir = codegen.emitIR();
#else
    appendDiagnostic(out,
                     Diagnostic{DiagnosticStage::CodeGen, "", std::nullopt,
                                "IR code generation is disabled in this build"});
    return out;
#endif
  }

  out.success = true;
  return out;
}

CompileResult compileFile(const std::string& path, const CompileOptions& options) {
  std::ifstream input(path);
  if (!input) {
    CompileResult out;
    appendDiagnostic(out, Diagnostic{DiagnosticStage::Io, path, std::nullopt,
                                     "failed to open input file"});
    return out;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  CompileOptions fileOptions = options;
  fileOptions.sourceName = path;
  return compileString(buffer.str(), fileOptions);
}

RunResult runString(std::string source, const RunOptions& options) {
  RunResult out;
  const PipelinePrepOptions prepOptions{options.sourceName, options.moduleResolver};
  PreparedProgram prepared;
  if (!prepareProgram(std::move(source), prepOptions, out, prepared)) {
    return out;
  }

  interp::Interpreter interpreter;
  auto runtimeResult = interpreter.run(*prepared.program);
  out.stdoutBuffer = runtimeResult.stdoutBuffer;
  out.exitCode = runtimeResult.exitCode;
  if (!runtimeResult.success) {
    for (const auto& error : runtimeResult.errors) {
      appendDiagnostic(out, Diagnostic{DiagnosticStage::Runtime, options.sourceName, error.location,
                                       error.message});
    }
    return out;
  }

  out.success = true;
  return out;
}

RunResult runFile(const std::string& path, const RunOptions& options) {
  std::ifstream input(path);
  if (!input) {
    RunResult out;
    appendDiagnostic(out, Diagnostic{DiagnosticStage::Io, path, std::nullopt,
                                     "failed to open input file"});
    return out;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  RunOptions fileOptions = options;
  fileOptions.sourceName = path;
  return runString(buffer.str(), fileOptions);
}

} // namespace dhad::pipeline
