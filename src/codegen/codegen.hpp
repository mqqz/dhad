#pragma once

#include "../ast/ast.hpp"

#include <memory>
#include <optional>
#include <string>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace dhad::codegen {

class CodeGenModule {
public:
  explicit CodeGenModule(std::string moduleName = "dhad");
  ~CodeGenModule();

  bool generate(const ast::Program& program);

  llvm::Module& module();
  const llvm::Module& module() const;
  llvm::LLVMContext& context();

  std::string emitIR() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

struct CodeGenResult {
  bool success{false};
  std::string ir;
};

CodeGenResult emitModuleIR(const ast::Program& program, std::string moduleName = "dhad");

} // namespace dhad::codegen
