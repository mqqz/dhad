#include "pipeline/compiler.hpp"

#if DHAD_ENABLE_CODEGEN
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>
#endif

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Options {
  std::string inputPath;
  std::string outputPath{"a.out"};
  bool emitIROnly{false};
  bool showHelp{false};
};

void printUsage(const char* progName) {
#if DHAD_ENABLE_CODEGEN
  std::cerr << "Usage: " << progName << " <input.dh> [-o <output>] [--emit-ir]\n";
#else
  std::cerr << "Usage: " << progName << " <input.dh>\n";
#endif
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
#if DHAD_ENABLE_CODEGEN
      opts.emitIROnly = true;
      continue;
#else
      std::cerr << "error: --emit-ir is unavailable in this build\n";
      return std::nullopt;
#endif
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

#if DHAD_ENABLE_CODEGEN
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
#endif

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

#if DHAD_ENABLE_CODEGEN
  dhad::pipeline::CompileOptions compileOptions;
  compileOptions.sourceName = opts.inputPath;
  compileOptions.moduleName = opts.outputPath;
  compileOptions.includeIR = true;

  auto result = dhad::pipeline::compileFile(opts.inputPath, compileOptions);
  if (!result.success) {
    std::cerr << result.stderrBuffer;
    return 1;
  }

  const std::string& irText = result.ir;
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
#else
  dhad::pipeline::RunOptions runOptions;
  runOptions.sourceName = opts.inputPath;

  const auto runResult = dhad::pipeline::runFile(opts.inputPath, runOptions);
  if (!runResult.stderrBuffer.empty()) {
    std::cerr << runResult.stderrBuffer;
  }
  if (!runResult.stdoutBuffer.empty()) {
    std::cout << runResult.stdoutBuffer;
  }
  if (!runResult.success) {
    return 1;
  }
  return runResult.exitCode;
#endif
}
