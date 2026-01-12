#include "codegen/codegen.hpp"
#include "parser/parser.hpp"
#include "typing/checker.hpp"

#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

struct Options {
  std::string inputPath;
  std::string outputPath{"a.out"};
  bool emitIROnly{false};
  bool showHelp{false};
};

void printUsage(const char* progName) {
  std::cerr << "Usage: " << progName << " <input.dh> [-o <output>] [--emit-ir]\n";
}

std::optional<Options> parseArgs(int argc, char** argv) {
  Options opts;
  bool sawInput = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "-h" || arg == "--help") {
      opts.showHelp = true;
      return opts;
    }
    if (arg == "-o") {
      if (i + 1 >= argc) {
        std::cerr << "error: -o requires an output path\n";
        return std::nullopt;
      }
      opts.outputPath = argv[++i];
      continue;
    }
    if (arg == "--emit-ir") {
      opts.emitIROnly = true;
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "error: unknown option '" << arg << "'\n";
      return std::nullopt;
    }
    if (sawInput) {
      std::cerr << "error: multiple input files specified\n";
      return std::nullopt;
    }
    opts.inputPath = arg;
    sawInput = true;
  }

  if (!sawInput) {
    std::cerr << "error: missing input file\n";
    return std::nullopt;
  }
  return opts;
}

bool writeIRToTemp(const std::string& ir, std::string& outPath) {
  llvm::SmallString<128> tempPath;
  int fd = -1;
  if (auto ec = llvm::sys::fs::createTemporaryFile("dhad", "ll", fd, tempPath)) {
    llvm::errs() << "failed to create temporary file: " << ec.message() << "\n";
    return false;
  }

  outPath = tempPath.str().str();
  llvm::raw_fd_ostream os(fd, true);
  os << ir;
  os.flush();
  return true;
}

bool invokeClang(const std::string& inputIR, const std::string& outputPath) {
#ifdef DHAD_DEFAULT_CLANG
  std::string clangPath = DHAD_DEFAULT_CLANG;
#else
  std::string clangPath = "clang";
#endif
  if (const char* env = std::getenv("DHAD_CLANG")) {
    clangPath = env;
  }

  std::string clangExecutable;
  if (clangPath.find('/') != std::string::npos) {
    clangExecutable = clangPath;
  } else {
    auto found = llvm::sys::findProgramByName(clangPath);
    if (!found) {
      llvm::errs() << "clang executable not found in PATH (looked for '" << clangPath << "')\n";
      return false;
    }
    clangExecutable = *found;
  }

  std::vector<std::string> storage = {clangExecutable, "-x", "ir", inputIR, "-o", outputPath};
  llvm::SmallVector<llvm::StringRef, 8> args;
  for (const auto& arg : storage) {
    args.push_back(arg);
  }
  std::string error;
  int result = llvm::sys::ExecuteAndWait(clangExecutable, args, std::nullopt, {}, 0, 0, &error);
  if (result != 0) {
    if (!error.empty()) {
      llvm::errs() << error << "\n";
    }
    llvm::errs() << "clang failed with exit code " << result << "\n";
    return false;
  }
  return true;
}

void printTypeErrors(const dhad::typing::TypeCheckerResult& result, const std::string& source) {
  for (const auto& error : result.errors) {
    std::cerr << dhad::typing::formatTypeError(error, source) << "\n";
  }
}

using ImportSet = std::unordered_set<std::string>;

std::string pathKey(const std::filesystem::path& path) {
  return std::filesystem::absolute(path).lexically_normal().string();
}

std::filesystem::path resolveImportPath(const std::string& module,
                                        const std::filesystem::path& sourcePath) {
  std::filesystem::path name(module);
  if (name.extension() != ".dh") {
    name += ".dh";
  }
  return sourcePath.parent_path() / name;
}

bool mergeImports(dhad::ast::Program& program, const std::filesystem::path& sourcePath,
                  ImportSet& seen, std::string& error) {
  std::vector<dhad::ast::NodePtr<dhad::ast::ASTNode>> merged;

  for (auto& node : program.topLevel) {
    auto* importDecl = llvm::dyn_cast<dhad::ast::ImportDecl>(node.get());
    if (!importDecl) {
      continue;
    }
    auto importPath = resolveImportPath(importDecl->module, sourcePath);
    std::error_code ec;
    if (!std::filesystem::exists(importPath, ec) || ec) {
      error = "error: import '" + importDecl->module + "' not found (expected " +
              importPath.string() + ")";
      return false;
    }
    const auto key = pathKey(importPath);
    if (!seen.insert(key).second) {
      continue;
    }

    auto importResult = dhad::parser::parseFile(importPath.string());
    if (!importResult.success || !importResult.root) {
      error = "error: failed to parse import '" + importDecl->module + "'";
      return false;
    }
    auto* importProgram = llvm::dyn_cast<dhad::ast::Program>(importResult.root.get());
    if (!importProgram) {
      error = "error: import '" + importDecl->module + "' did not produce a Program root";
      return false;
    }
    if (!mergeImports(*importProgram, importPath, seen, error)) {
      return false;
    }
    for (auto& importedNode : importProgram->topLevel) {
      if (!llvm::isa<dhad::ast::ImportDecl>(importedNode.get())) {
        merged.push_back(std::move(importedNode));
      }
    }
  }

  for (auto& node : program.topLevel) {
    if (!llvm::isa<dhad::ast::ImportDecl>(node.get())) {
      merged.push_back(std::move(node));
    }
  }

  program.topLevel = std::move(merged);
  return true;
}

} // namespace

int main(int argc, char** argv) {
  auto optsOr = parseArgs(argc, argv);
  if (!optsOr) {
    printUsage(argv[0]);
    return 1;
  }
  if (optsOr->showHelp) {
    printUsage(argv[0]);
    return 0;
  }
  const auto& opts = *optsOr;

  auto parseResult = dhad::parser::parseFile(opts.inputPath);
  if (!parseResult.success || !parseResult.root) {
    std::cerr << "error: failed to parse " << opts.inputPath << "\n";
    return 1;
  }

  auto* program = llvm::dyn_cast<dhad::ast::Program>(parseResult.root.get());
  if (!program) {
    std::cerr << "error: parser did not produce a Program root\n";
    return 1;
  }

  ImportSet imported;
  imported.insert(pathKey(std::filesystem::path(opts.inputPath)));
  std::string importError;
  if (!mergeImports(*program, std::filesystem::path(opts.inputPath), imported, importError)) {
    std::cerr << importError << "\n";
    return 1;
  }

  dhad::typing::TypeChecker checker;
  auto typeResult = checker.check(*program);
  if (!typeResult.success) {
    printTypeErrors(typeResult, opts.inputPath);
    return 1;
  }

  dhad::codegen::CodeGenModule codegen(opts.outputPath);
  if (!codegen.generate(*program)) {
    std::cerr << "error: code generation failed\n";
    return 1;
  }

  const std::string irText = codegen.emitIR();
  if (opts.emitIROnly) {
    std::cout << irText;
    return 0;
  }

  std::string tempIRPath;
  if (!writeIRToTemp(irText, tempIRPath)) {
    return 1;
  }

  bool clangOK = invokeClang(tempIRPath, opts.outputPath);
  llvm::sys::fs::remove(tempIRPath);

  if (!clangOK) {
    return 1;
  }

  std::cout << "Wrote executable to " << opts.outputPath << "\n";
  return 0;
}
