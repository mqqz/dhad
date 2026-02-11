#include "pipeline/compiler.hpp"

#include "../codegen/codegen.hpp"
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

#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

namespace dhad::pipeline {

namespace {

using ImportSet = std::unordered_set<std::string>;

void appendDiagnostic(CompileResult& out, Diagnostic diagnostic) {
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

std::optional<ResolvedModule> resolveModule(const CompileOptions& options, std::string_view moduleName,
                                            std::string_view importerSourceName) {
  if (options.moduleResolver) {
    return options.moduleResolver(moduleName, importerSourceName);
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

bool mergeImports(ast::Program& program, std::string_view sourceName, const CompileOptions& options,
                  ImportSet& visited, CompileResult& out) {
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
    auto module = resolveModule(options, importDecl->module, sourceName);
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

} // namespace

CompileResult compileString(std::string source, const CompileOptions& options) {
  CompileResult out;

  auto parseResult = parser::parseString(std::move(source));
  if (!parseResult.success || !parseResult.root) {
    appendDiagnostic(out, Diagnostic{DiagnosticStage::Parse, "", std::nullopt,
                                     "failed to parse " + options.sourceName});
    return out;
  }

  auto* program = llvm::dyn_cast<ast::Program>(parseResult.root.get());
  if (!program) {
    appendDiagnostic(out, Diagnostic{DiagnosticStage::Parse, "", std::nullopt,
                                     "parser did not produce a Program root"});
    return out;
  }

  ImportSet visited;
  if (!options.sourceName.empty() && options.sourceName != "<memory>") {
    visited.insert(pathKey(std::filesystem::path(options.sourceName)));
  }
  if (!mergeImports(*program, options.sourceName, options, visited, out)) {
    return out;
  }

  typing::TypeChecker checker;
  const auto typeResult = checker.check(*program);
  if (!typeResult.success) {
    for (const auto& error : typeResult.errors) {
      appendDiagnostic(out, Diagnostic{DiagnosticStage::TypeCheck, options.sourceName, error.location,
                                       error.message});
    }
    return out;
  }

  if (options.includeAstDump) {
    llvm::raw_string_ostream astStream(out.astDump);
    program->dump(astStream);
    astStream.flush();
  }

  if (options.includeIR) {
    codegen::CodeGenModule codegen(options.moduleName);
    if (!codegen.generate(*program)) {
      appendDiagnostic(out, Diagnostic{DiagnosticStage::CodeGen, "", std::nullopt,
                                       codegen.lastError().empty() ? "code generation failed"
                                                                    : codegen.lastError()});
      return out;
    }
    out.ir = codegen.emitIR();
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

} // namespace dhad::pipeline
