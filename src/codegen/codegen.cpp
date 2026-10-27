#include "codegen.hpp"
#include "../common/numeric.hpp"

#include "../ast/ast.hpp"
#include "../std/identifiers.hpp"
#include "../std/runtime.hpp"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dhad::codegen {
using llvm::sys::getDefaultTargetTriple;

namespace {

constexpr std::string_view kEntryFunctionName = u8"بداية";

struct LoopContext {
  llvm::BasicBlock* continueTarget{nullptr};
  llvm::BasicBlock* breakTarget{nullptr};
};

std::string stripQuotes(std::string value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

void setModuleTargetTriple(llvm::Module& module) {
#if LLVM_VERSION_MAJOR >= 21
  module.setTargetTriple(llvm::Triple(getDefaultTargetTriple()));
#else
  module.setTargetTriple(getDefaultTargetTriple());
#endif
}

} // namespace

struct CodeGenModule::Impl {
  struct StatementEmitter : ast::ASTVisitor {
    explicit StatementEmitter(Impl& owner) : impl(owner) {}

    void visit(const ast::BlockStmt& node) override { result = impl.emitBlock(&node); }
    void visit(const ast::DeclarationStmt& node) override { result = impl.emitDeclaration(node); }
    void visit(const ast::AssignmentStmt& node) override { result = impl.emitAssignment(node); }
    void visit(const ast::IndexAssignmentStmt& node) override {
      result = impl.emitIndexAssignment(node);
    }
    void visit(const ast::IfStmt& node) override { result = impl.emitIf(node); }
    void visit(const ast::WhileStmt& node) override { result = impl.emitWhile(node); }
    void visit(const ast::ForStmt& node) override { result = impl.emitFor(node); }
    void visit(const ast::ReturnStmt& node) override { result = impl.emitReturn(node); }
    void visit(const ast::BreakStmt&) override { result = impl.emitBreak(); }
    void visit(const ast::ContinueStmt&) override { result = impl.emitContinue(); }
    void visit(const ast::ExpressionStmt& node) override {
      result = !node.expr || impl.emitExpression(node.expr.get()) != nullptr;
    }

    bool result{false};

  private:
    Impl& impl;
  };

  struct ExpressionEmitter : ast::ASTVisitor {
    explicit ExpressionEmitter(Impl& owner) : impl(owner) {}

    void visit(const ast::BinaryExpr& node) override { value = impl.emitBinaryExpr(node); }
    void visit(const ast::UnaryExpr& node) override { value = impl.emitUnaryExpr(node); }
    void visit(const ast::LiteralExpr& node) override { value = impl.emitLiteralExpr(node); }
    void visit(const ast::IdentifierExpr& node) override { value = impl.emitIdentifierExpr(node); }
    void visit(const ast::FieldAccessExpr& node) override {
      value = impl.emitFieldAccessExpr(node);
    }
    void visit(const ast::IndexExpr& node) override { value = impl.emitIndexExpr(node); }
    void visit(const ast::CallExpr& node) override { value = impl.emitCallExpr(node); }
    void visit(const ast::ArrayLiteralExpr& node) override { value = impl.emitArrayLiteral(node); }
    void visit(const ast::StructLiteralExpr& node) override {
      value = impl.emitStructLiteral(node);
    }

    llvm::Value* value{nullptr};

  private:
    Impl& impl;
  };

  explicit Impl(std::string name)
      : moduleName(std::move(name)), module(std::make_unique<llvm::Module>(moduleName, context)),
        builder(context) {
    setModuleTargetTriple(*module);
  }

  bool generate(const ast::Program& program);
  llvm::Module& getModule() { return *module; }
  llvm::LLVMContext& getContext() { return context; }
  std::string emitIR() const;
  std::string lastError() const { return lastErrorMessage; }

private:
  llvm::Type* getTypeByExpr(const ast::TypeExpr* type);
  llvm::Type* getTypeByLiteral(const ast::LiteralExpr& literal);
  llvm::PointerType* getBytePtrType();
  llvm::Value* emitExpression(const ast::Expression* expr);
  llvm::Value* emitBinaryExpr(const ast::BinaryExpr& expr);
  llvm::Value* emitUnaryExpr(const ast::UnaryExpr& expr);
  llvm::Value* emitLiteralExpr(const ast::LiteralExpr& expr);
  llvm::Value* emitIdentifierExpr(const ast::IdentifierExpr& expr);
  llvm::Value* emitFieldAccessExpr(const ast::FieldAccessExpr& expr);
  llvm::Value* emitIndexExpr(const ast::IndexExpr& expr);
  llvm::Value* emitCallExpr(const ast::CallExpr& expr);
  llvm::Value* emitArrayLiteral(const ast::ArrayLiteralExpr& expr);
  llvm::Value* emitStructLiteral(const ast::StructLiteralExpr& expr);
  llvm::Value* emitArithmeticBinary(ast::BinaryOp op, llvm::Value* lhs, llvm::Value* rhs);
  llvm::Value* emitShortCircuit(const ast::BinaryExpr& expr);
  llvm::Value* emitStringEqual(llvm::Value* lhs, llvm::Value* rhs);
  void emitGuard(llvm::Value* valid, const std::string& message);
  llvm::Value* checkedInteger(ast::BinaryOp op, llvm::Value* lhs, llvm::Value* rhs);
  bool emitEntryWrapper();
  llvm::Value* emitComparisonBinary(ast::BinaryOp op, llvm::Value* lhs, llvm::Value* rhs);
  llvm::Function* getOrDeclareFunction(const std::string& name);
  llvm::Function* declareBuiltinFunction(const stdlib::StdFunctionDescriptor& descriptor);
  bool emitStatement(const ast::Statement* stmt);
  bool emitBlock(const ast::BlockStmt* block);
  bool emitDeclaration(const ast::DeclarationStmt& stmt);
  bool emitAssignment(const ast::AssignmentStmt& stmt);
  bool emitIndexAssignment(const ast::IndexAssignmentStmt& stmt);
  bool emitIf(const ast::IfStmt& stmt);
  bool emitWhile(const ast::WhileStmt& stmt);
  bool emitFor(const ast::ForStmt& stmt);
  bool emitReturn(const ast::ReturnStmt& stmt);
  bool emitBreak();
  bool emitContinue();

  bool declareFunction(const ast::FunctionDecl& fn);
  bool emitFunction(const ast::FunctionDecl& fn);
  void injectStdRuntime();
  void defineStdFunctionBodies();
  void definePrintFunction(const stdlib::StdFunctionDescriptor& descriptor);

  llvm::AllocaInst* lookupVariable(const std::string& name);
  void defineVariable(const std::string& name, llvm::AllocaInst* alloca);
  void pushScope();
  void popScope();

  llvm::AllocaInst* createAlloca(llvm::Function* function, llvm::Type* type,
                                 const std::string& name);

  llvm::Value* convertToType(llvm::Value* value, llvm::Type* type);
  llvm::Value* ensureBoolean(llvm::Value* value);
  llvm::Value* ensureInteger(llvm::Value* value);
  llvm::Value* ensureFloat(llvm::Value* value);
  bool promoteNumericOperands(llvm::Value*& lhs, llvm::Value*& rhs, bool& useFloat);
  llvm::StructType* getStructType(const ast::StructDecl& decl);
  llvm::StructType* getStructTypeByName(std::string_view name);
  const ast::StructDecl* findStructDecl(std::string_view name) const;
  llvm::Type* getTypeByCheckedType(const typing::TypePtr& type);
  llvm::StructType* getArrayRuntimeType();
  llvm::Value* emitCheckedArrayDataPtr(llvm::Value* arrayValue, llvm::Value* indexValue,
                                       llvm::Type* elementType);

  std::string moduleName;
  llvm::LLVMContext context;
  std::unique_ptr<llvm::Module> module;
  llvm::IRBuilder<> builder;
  llvm::Function* currentFunction{nullptr};
  llvm::Type* currentReturnType{nullptr};
  std::vector<std::unordered_map<std::string, llvm::AllocaInst*>> scopes;
  std::vector<LoopContext> loopStack;
  std::unordered_map<const ast::FunctionDecl*, llvm::Function*> functions;
  std::unordered_map<std::string, llvm::Function*> functionNames;
  std::unordered_map<std::string, const ast::StructDecl*> structDecls;
  std::unordered_map<std::string, const ast::EnumDecl*> enumDecls;
  std::unordered_map<std::string, llvm::StructType*> structTypes;
  llvm::StructType* arrayRuntimeType{nullptr};
  bool stdRuntimeInjected{false};
  stdlib::StdRuntime stdRuntime;
  std::string lastErrorMessage;
};

bool CodeGenModule::Impl::generate(const ast::Program& program) {
  lastErrorMessage.clear();
  module = std::make_unique<llvm::Module>(moduleName, context);
  functions.clear();
  functionNames.clear();
  setModuleTargetTriple(*module);
  structDecls.clear();
  enumDecls.clear();
  structTypes.clear();
  arrayRuntimeType = nullptr;
  stdRuntimeInjected = false;
  bool ok = true;

  for (const auto& node : program.topLevel) {
    if (auto* decl = llvm::dyn_cast<ast::StructDecl>(node.get())) {
      structDecls.emplace(decl->name, decl);
    } else if (auto* decl = llvm::dyn_cast<ast::EnumDecl>(node.get())) {
      enumDecls.emplace(decl->name, decl);
    }
  }

  for (const auto& node : program.topLevel) {
    if (auto* fn = llvm::dyn_cast<ast::FunctionDecl>(node.get())) {
      ok &= declareFunction(*fn);
    }
  }

  for (const auto& node : program.topLevel) {
    if (auto* fn = llvm::dyn_cast<ast::FunctionDecl>(node.get())) {
      ok &= emitFunction(*fn);
    }
  }

  if (!ok) {
    if (lastErrorMessage.empty()) lastErrorMessage = "Unsupported operation during code generation";
    return false;
  }

  if (!emitEntryWrapper()) return false;
  injectStdRuntime();

  std::string verifyErrors;
  llvm::raw_string_ostream verifyStream(verifyErrors);
  const bool invalid = llvm::verifyModule(*module, &verifyStream);
  verifyStream.flush();
  if (invalid) {
    lastErrorMessage = verifyErrors.empty() ? "LLVM module verification failed" : verifyErrors;
    return false;
  }
  return true;
}

llvm::PointerType* CodeGenModule::Impl::getBytePtrType() {
  return llvm::PointerType::get(context, 0);
}

const ast::StructDecl* CodeGenModule::Impl::findStructDecl(std::string_view name) const {
  auto it = structDecls.find(std::string(name));
  if (it == structDecls.end()) {
    return nullptr;
  }
  return it->second;
}

llvm::StructType* CodeGenModule::Impl::getStructTypeByName(std::string_view name) {
  auto it = structTypes.find(std::string(name));
  if (it != structTypes.end()) {
    return it->second;
  }
  const auto* decl = findStructDecl(name);
  if (!decl) {
    return nullptr;
  }
  return getStructType(*decl);
}

llvm::StructType* CodeGenModule::Impl::getStructType(const ast::StructDecl& decl) {
  auto it = structTypes.find(decl.name);
  if (it != structTypes.end()) {
    return it->second;
  }
  auto* type = llvm::StructType::create(context, decl.name);
  structTypes.emplace(decl.name, type);
  std::vector<llvm::Type*> members;
  members.reserve(decl.fields.size());
  for (const auto& field : decl.fields) {
    auto* fieldType = getTypeByExpr(field->type.get());
    if (!fieldType) {
      return nullptr;
    }
    members.push_back(fieldType);
  }
  type->setBody(members, /*isPacked=*/false);
  return type;
}

llvm::StructType* CodeGenModule::Impl::getArrayRuntimeType() {
  if (!arrayRuntimeType) {
    arrayRuntimeType = llvm::StructType::create(context, "dhad.array");
    arrayRuntimeType->setBody({builder.getInt64Ty(), getBytePtrType()}, /*isPacked=*/false);
  }
  return arrayRuntimeType;
}

llvm::Value* CodeGenModule::Impl::emitCheckedArrayDataPtr(llvm::Value* arrayValue,
                                                          llvm::Value* indexValue,
                                                          llvm::Type* elementType) {
  if (!arrayValue || !indexValue || !elementType) {
    return nullptr;
  }

  auto* arrayType = getArrayRuntimeType();
  auto* lenPtr = builder.CreateStructGEP(arrayType, arrayValue, 0, "arr.len.ptr");
  auto* dataPtrPtr = builder.CreateStructGEP(arrayType, arrayValue, 1, "arr.data.ptr");
  auto* length = builder.CreateLoad(builder.getInt64Ty(), lenPtr, "arr.len");
  auto* data = builder.CreateLoad(getBytePtrType(), dataPtrPtr, "arr.data");

  auto* idx = ensureInteger(indexValue);
  if (!idx) {
    return nullptr;
  }

  auto* nonNegative = builder.CreateICmpSGE(idx, builder.getInt64(0), "idx.nn");
  auto* lessThanLen = builder.CreateICmpSLT(idx, length, "idx.lt_len");
  auto* inRange = builder.CreateAnd(nonNegative, lessThanLen, "idx.in_range");

  emitGuard(inRange, "Array index out of bounds");
  return builder.CreateInBoundsGEP(elementType, data, idx, "arr.elem.ptr");
}

llvm::Type* CodeGenModule::Impl::getTypeByExpr(const ast::TypeExpr* type) {
  if (!type) {
    return nullptr;
  }
  if (auto* prim = llvm::dyn_cast<ast::TypePrimitiveExpr>(type)) {
    switch (prim->kind) {
    case typing::TypeKind::Int:
      return llvm::Type::getInt64Ty(context);
    case typing::TypeKind::Bool:
      return llvm::Type::getInt1Ty(context);
    case typing::TypeKind::Float:
      return llvm::Type::getDoubleTy(context);
    case typing::TypeKind::Char:
      return llvm::Type::getInt8Ty(context);
    case typing::TypeKind::String:
    case typing::TypeKind::Null:
      return getBytePtrType();
    default:
      return nullptr;
    }
  }
  if (auto* named = llvm::dyn_cast<ast::NamedTypeExpr>(type)) {
    if (structDecls.find(named->name) != structDecls.end()) {
      return getStructTypeByName(named->name);
    }
    if (enumDecls.find(named->name) != enumDecls.end()) {
      return getBytePtrType();
    }
    return nullptr;
  }
  if (auto* array = llvm::dyn_cast<ast::TypeArrayExpr>(type)) {
    auto* elementType = getTypeByExpr(array->element.get());
    if (!elementType) {
      return nullptr;
    }
    return llvm::PointerType::get(context, 0);
  }
  if (llvm::isa<ast::TypeSumExpr>(type)) {
    return getBytePtrType();
  }
  if (llvm::isa<ast::TypeProductExpr>(type)) {
    return getBytePtrType();
  }
  return nullptr;
}

llvm::Type* CodeGenModule::Impl::getTypeByCheckedType(const typing::TypePtr& type) {
  if (!type) {
    return nullptr;
  }
  switch (type->kind) {
  case typing::TypeKind::Int:
    return llvm::Type::getInt64Ty(context);
  case typing::TypeKind::Bool:
    return llvm::Type::getInt1Ty(context);
  case typing::TypeKind::Float:
    return llvm::Type::getDoubleTy(context);
  case typing::TypeKind::Char:
    return llvm::Type::getInt8Ty(context);
  case typing::TypeKind::String:
  case typing::TypeKind::Null:
    return getBytePtrType();
  case typing::TypeKind::Array: {
    const auto& info = std::get<typing::ArrayTypeInfo>(type->payload);
    auto* elementType = getTypeByCheckedType(info.element);
    return elementType ? llvm::PointerType::get(context, 0) : nullptr;
  }
  case typing::TypeKind::Product: {
    const auto& info = std::get<typing::ProductTypeInfo>(type->payload);
    if (!info.name.empty()) {
      if (auto* structType = getStructTypeByName(info.name)) {
        return structType;
      }
    }
    return getBytePtrType();
  }
  case typing::TypeKind::Sum:
  case typing::TypeKind::Function:
    return getBytePtrType();
  }
  return nullptr;
}

llvm::Type* CodeGenModule::Impl::getTypeByLiteral(const ast::LiteralExpr& literal) {
  if (literal.value == "true" || literal.value == "false") {
    return llvm::Type::getInt1Ty(context);
  }
  if (literal.value == "null") {
    return getBytePtrType();
  }
  if (!literal.value.empty() && literal.value.front() == '"') {
    return getBytePtrType();
  }
  if (literal.value.find('.') != std::string::npos) {
    return llvm::Type::getDoubleTy(context);
  }
  return llvm::Type::getInt64Ty(context);
}

llvm::AllocaInst* CodeGenModule::Impl::createAlloca(llvm::Function* function, llvm::Type* type,
                                                    const std::string& name) {
  llvm::IRBuilder<> tmp(&function->getEntryBlock(), function->getEntryBlock().begin());
  return tmp.CreateAlloca(type, nullptr, name);
}

void CodeGenModule::Impl::pushScope() { scopes.emplace_back(); }

void CodeGenModule::Impl::popScope() {
  if (!scopes.empty()) {
    scopes.pop_back();
  }
}

llvm::AllocaInst* CodeGenModule::Impl::lookupVariable(const std::string& name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return found->second;
    }
  }
  return nullptr;
}

void CodeGenModule::Impl::defineVariable(const std::string& name, llvm::AllocaInst* alloca) {
  if (scopes.empty()) {
    pushScope();
  }
  scopes.back()[name] = alloca;
}

llvm::Value* CodeGenModule::Impl::convertToType(llvm::Value* value, llvm::Type* type) {
  if (!value || !type) {
    return nullptr;
  }
  if (value->getType() == type) {
    return value;
  }
  if (value->getType()->isFloatingPointTy() && type->isFloatingPointTy()) {
    return builder.CreateFPCast(value, type, "fpcast");
  }
  if (value->getType()->isFloatingPointTy() && type->isIntegerTy()) {
    return builder.CreateFPToSI(value, type, "fptosi");
  }
  if (value->getType()->isIntegerTy() && type->isFloatingPointTy()) {
    return builder.CreateSIToFP(value, type, "sitofp");
  }
  if (value->getType()->isIntegerTy(1) && type->isIntegerTy(64)) {
    return builder.CreateZExt(value, type, "booltoint");
  }
  if (value->getType()->isIntegerTy(64) && type->isIntegerTy(1)) {
    return builder.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0), "inttobool");
  }
  if (value->getType()->isIntegerTy() && type->isIntegerTy()) {
    return builder.CreateIntCast(value, type, true, "intcast");
  }
  if (value->getType()->isPointerTy() && type->isPointerTy()) {
    return builder.CreateBitCast(value, type);
  }
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::ensureBoolean(llvm::Value* value) {
  if (!value) {
    return nullptr;
  }
  if (value->getType()->isIntegerTy(1)) {
    return value;
  }
  if (value->getType()->isIntegerTy()) {
    return builder.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0), "tobool");
  }
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::ensureInteger(llvm::Value* value) {
  if (!value) {
    return nullptr;
  }
  if (value->getType()->isIntegerTy(64)) {
    return value;
  }
  if (value->getType()->isIntegerTy(1)) {
    return builder.CreateZExt(value, builder.getInt64Ty(), "booltoint");
  }
  if (value->getType()->isIntegerTy()) {
    return builder.CreateIntCast(value, builder.getInt64Ty(), true, "intcast");
  }
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::ensureFloat(llvm::Value* value) {
  if (!value) {
    return nullptr;
  }
  if (value->getType()->isFloatingPointTy()) {
    return value;
  }
  if (value->getType()->isIntegerTy()) {
    return builder.CreateSIToFP(value, builder.getDoubleTy(), "sitofp");
  }
  return nullptr;
}

bool CodeGenModule::Impl::promoteNumericOperands(llvm::Value*& lhs, llvm::Value*& rhs,
                                                 bool& useFloat) {
  if (!lhs || !rhs) {
    return false;
  }
  const bool lhsFloat = lhs->getType()->isFloatingPointTy();
  const bool rhsFloat = rhs->getType()->isFloatingPointTy();
  useFloat = lhsFloat || rhsFloat;
  if (useFloat) {
    lhs = ensureFloat(lhs);
    rhs = ensureFloat(rhs);
  } else {
    lhs = ensureInteger(lhs);
    rhs = ensureInteger(rhs);
  }
  return lhs && rhs;
}

bool CodeGenModule::Impl::declareFunction(const ast::FunctionDecl& fn) {
  std::vector<llvm::Type*> paramTypes;
  paramTypes.reserve(fn.params.size());
  for (const auto& param : fn.params) {
    auto* type = getTypeByExpr(param->type.get());
    if (!type) {
      return false;
    }
    paramTypes.push_back(type);
  }
  llvm::Type* returnType = fn.returnType ? getTypeByExpr(fn.returnType.get()) : nullptr;
  if (!returnType) {
    returnType = llvm::Type::getVoidTy(context);
  }
  auto* type = llvm::FunctionType::get(returnType, paramTypes, /*isVarArg=*/false);
  std::string llvmName = "dhad.fn." + fn.name;
  auto* function =
      llvm::Function::Create(type, llvm::Function::ExternalLinkage, llvmName, module.get());
  functions[&fn] = function;
  functionNames[fn.name] = function;
  return true;
}

bool CodeGenModule::Impl::emitFunction(const ast::FunctionDecl& fn) {
  auto it = functions.find(&fn);
  if (it == functions.end()) {
    return false;
  }
  auto* function = it->second;
  currentFunction = function;
  currentReturnType = function->getReturnType()->isVoidTy() ? nullptr : function->getReturnType();
  auto* entry = llvm::BasicBlock::Create(context, "entry", function);
  builder.SetInsertPoint(entry);
  scopes.clear();
  pushScope();

  unsigned idx = 0;
  for (auto& arg : function->args()) {
    auto& param = fn.params[idx++];
    arg.setName(param->name);
    auto* alloca = createAlloca(function, arg.getType(), param->name);
    builder.CreateStore(&arg, alloca);
    defineVariable(param->name, alloca);
  }

  bool ok = true;
  if (fn.body) {
    ok = emitBlock(fn.body.get());
  }

  if (ok && !builder.GetInsertBlock()->getTerminator()) {
    if (function->getReturnType()->isVoidTy()) {
      builder.CreateRetVoid();
    } else {
      builder.CreateRet(llvm::Constant::getNullValue(function->getReturnType()));
    }
  }

  popScope();
  currentFunction = nullptr;
  currentReturnType = nullptr;
  return ok;
}

bool CodeGenModule::Impl::emitBlock(const ast::BlockStmt* block) {
  pushScope();
  for (const auto& stmt : block->statements) {
    if (builder.GetInsertBlock()->getTerminator()) {
      break;
    }
    if (!emitStatement(stmt.get())) {
      popScope();
      return false;
    }
  }
  popScope();
  return true;
}

bool CodeGenModule::Impl::emitDeclaration(const ast::DeclarationStmt& stmt) {
  auto* initializer = stmt.initializer ? emitExpression(stmt.initializer.get()) : nullptr;
  if (!initializer) return false;
  llvm::Type* type = getTypeByCheckedType(stmt.checkedType);
  if (!type) type = initializer->getType();
  auto* value = convertToType(initializer, type);
  if (!value) return false;
  auto* var = createAlloca(currentFunction, type, stmt.name);
  builder.CreateStore(value, var);
  defineVariable(stmt.name, var);
  return true;
}

bool CodeGenModule::Impl::emitAssignment(const ast::AssignmentStmt& stmt) {
  auto* alloca = lookupVariable(stmt.target);
  if (!alloca) {
    return false;
  }
  auto* value = emitExpression(stmt.value.get());
  if (!value) {
    return false;
  }
  value = convertToType(value, alloca->getAllocatedType());
  if (!value) {
    return false;
  }
  builder.CreateStore(value, alloca);
  return true;
}

bool CodeGenModule::Impl::emitIndexAssignment(const ast::IndexAssignmentStmt& stmt) {
  auto* alloca = lookupVariable(stmt.target);
  if (!alloca || !stmt.index || !stmt.value) {
    return false;
  }

  auto* arrayValue = builder.CreateLoad(alloca->getAllocatedType(), alloca, stmt.target);
  emitGuard(builder.CreateIsNotNull(arrayValue), "Null array access");
  auto* indexValue = emitExpression(stmt.index.get());
  if (!arrayValue || !indexValue) {
    return false;
  }

  llvm::Type* elementType = getTypeByCheckedType(stmt.resolvedElementType());
  if (!elementType) {
    return false;
  }

  auto* elementPtr = emitCheckedArrayDataPtr(arrayValue, indexValue, elementType);
  if (!elementPtr) {
    return false;
  }

  auto* value = convertToType(emitExpression(stmt.value.get()), elementType);
  if (!value) {
    return false;
  }
  builder.CreateStore(value, elementPtr);
  return true;
}

bool CodeGenModule::Impl::emitIf(const ast::IfStmt& stmt) {
  auto* cond = ensureBoolean(emitExpression(stmt.condition.get()));
  if (!cond) {
    return false;
  }
  auto* function = builder.GetInsertBlock()->getParent();
  auto* thenBB = llvm::BasicBlock::Create(context, "if.then", function);
  llvm::BasicBlock* elseBB =
      stmt.elseBranch ? llvm::BasicBlock::Create(context, "if.else", function) : nullptr;
  auto* mergeBB = llvm::BasicBlock::Create(context, "if.end", function);

  if (elseBB) {
    builder.CreateCondBr(cond, thenBB, elseBB);
  } else {
    builder.CreateCondBr(cond, thenBB, mergeBB);
  }

  builder.SetInsertPoint(thenBB);
  if (!emitStatement(stmt.thenBranch.get())) {
    return false;
  }
  if (!builder.GetInsertBlock()->getTerminator()) {
    builder.CreateBr(mergeBB);
  }

  if (elseBB) {
    builder.SetInsertPoint(elseBB);
    if (!emitStatement(stmt.elseBranch.get())) {
      return false;
    }
    if (!builder.GetInsertBlock()->getTerminator()) {
      builder.CreateBr(mergeBB);
    }
  }

  builder.SetInsertPoint(mergeBB);
  return true;
}

bool CodeGenModule::Impl::emitWhile(const ast::WhileStmt& stmt) {
  auto* function = builder.GetInsertBlock()->getParent();
  auto* condBB = llvm::BasicBlock::Create(context, "while.cond", function);
  auto* bodyBB = llvm::BasicBlock::Create(context, "while.body", function);
  auto* afterBB = llvm::BasicBlock::Create(context, "while.end", function);

  builder.CreateBr(condBB);
  builder.SetInsertPoint(condBB);
  auto* cond = ensureBoolean(emitExpression(stmt.condition.get()));
  if (!cond) {
    return false;
  }
  builder.CreateCondBr(cond, bodyBB, afterBB);

  builder.SetInsertPoint(bodyBB);
  loopStack.push_back(LoopContext{condBB, afterBB});
  if (!emitStatement(stmt.body.get())) {
    loopStack.pop_back();
    return false;
  }
  if (!builder.GetInsertBlock()->getTerminator()) {
    builder.CreateBr(condBB);
  }
  loopStack.pop_back();
  builder.SetInsertPoint(afterBB);
  return true;
}

bool CodeGenModule::Impl::emitFor(const ast::ForStmt& stmt) {
  // Treat as while loop with condition expression.
  auto* function = builder.GetInsertBlock()->getParent();
  auto* condBB = llvm::BasicBlock::Create(context, "for.cond", function);
  auto* bodyBB = llvm::BasicBlock::Create(context, "for.body", function);
  auto* afterBB = llvm::BasicBlock::Create(context, "for.end", function);

  builder.CreateBr(condBB);
  builder.SetInsertPoint(condBB);
  llvm::Value* condValue = stmt.condition ? ensureBoolean(emitExpression(stmt.condition.get()))
                                          : llvm::ConstantInt::getTrue(context);
  if (!condValue) {
    return false;
  }
  builder.CreateCondBr(condValue, bodyBB, afterBB);

  builder.SetInsertPoint(bodyBB);
  loopStack.push_back(LoopContext{condBB, afterBB});
  if (!emitStatement(stmt.body.get())) {
    loopStack.pop_back();
    return false;
  }
  if (!builder.GetInsertBlock()->getTerminator()) {
    builder.CreateBr(condBB);
  }
  loopStack.pop_back();
  builder.SetInsertPoint(afterBB);
  return true;
}

bool CodeGenModule::Impl::emitReturn(const ast::ReturnStmt& stmt) {
  llvm::Value* value = nullptr;
  if (stmt.value) {
    value = emitExpression(stmt.value.get());
  }
  if (stmt.value && !value) return false;
  if (currentReturnType) {
    if (!value) {
      value = llvm::Constant::getNullValue(currentReturnType);
    } else {
      value = convertToType(value, currentReturnType);
    }
    if (!value) {
      return false;
    }
    builder.CreateRet(value);
  } else {
    builder.CreateRetVoid();
  }
  return true;
}

bool CodeGenModule::Impl::emitBreak() {
  if (loopStack.empty()) {
    return false;
  }
  builder.CreateBr(loopStack.back().breakTarget);
  return true;
}

bool CodeGenModule::Impl::emitContinue() {
  if (loopStack.empty()) {
    return false;
  }
  builder.CreateBr(loopStack.back().continueTarget);
  return true;
}

bool CodeGenModule::Impl::emitStatement(const ast::Statement* stmt) {
  if (!stmt) {
    return true;
  }
  StatementEmitter emitter(*this);
  stmt->accept(emitter);
  return emitter.result;
}

llvm::Value* CodeGenModule::Impl::emitExpression(const ast::Expression* expr) {
  if (!expr) {
    return nullptr;
  }
  ExpressionEmitter emitter(*this);
  expr->accept(emitter);
  if (!emitter.value && lastErrorMessage.empty()) {
    lastErrorMessage = "Cannot lower " + std::string(expr->kindName()) + " at " +
                       std::to_string(expr->getLocation().line) + ":" +
                       std::to_string(expr->getLocation().column);
  }
  return emitter.value;
}

llvm::Value* CodeGenModule::Impl::emitBinaryExpr(const ast::BinaryExpr& expr) {
  if (expr.op == ast::BinaryOp::And || expr.op == ast::BinaryOp::Or) return emitShortCircuit(expr);
  auto* lhs = emitExpression(expr.lhs.get());
  auto* rhs = emitExpression(expr.rhs.get());
  if (!lhs || !rhs) {
    return nullptr;
  }

  if ((expr.op == ast::BinaryOp::Eq || expr.op == ast::BinaryOp::Ne) &&
      lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
    const auto left = expr.lhs->resolvedType(), right = expr.rhs->resolvedType();
    auto* equal = (left && left->kind == typing::TypeKind::String) ||
                          (right && right->kind == typing::TypeKind::String)
                      ? emitStringEqual(lhs, rhs)
                      : builder.CreateICmpEQ(lhs, rhs);
    return expr.op == ast::BinaryOp::Eq ? equal : builder.CreateNot(equal);
  }
  switch (expr.op) {
  case ast::BinaryOp::Add:
  case ast::BinaryOp::Sub:
  case ast::BinaryOp::Mul:
  case ast::BinaryOp::Div:
    return emitArithmeticBinary(expr.op, lhs, rhs);
  case ast::BinaryOp::And:
  case ast::BinaryOp::Or:
    return nullptr;
  case ast::BinaryOp::Eq:
  case ast::BinaryOp::Ne:
  case ast::BinaryOp::Lt:
  case ast::BinaryOp::Le:
  case ast::BinaryOp::Gt:
  case ast::BinaryOp::Ge:
    return emitComparisonBinary(expr.op, lhs, rhs);
  }
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::emitUnaryExpr(const ast::UnaryExpr& expr) {
  if (expr.op == ast::UnaryOp::Negate) {
    if (const auto* literal = llvm::dyn_cast<ast::LiteralExpr>(expr.operand.get());
        literal && numeric::isMinMagnitude(literal->value))
      return builder.getInt64(INT64_MIN);
  }
  auto* operand = emitExpression(expr.operand.get());
  if (!operand) {
    return nullptr;
  }
  if (expr.op == ast::UnaryOp::Negate) {
    if (operand->getType()->isFloatingPointTy()) return builder.CreateFNeg(operand, "fneg");
    operand = ensureInteger(operand);
    return operand ? checkedInteger(ast::BinaryOp::Sub, builder.getInt64(0), operand) : nullptr;
  }
  if (expr.op == ast::UnaryOp::Not) {
    operand = ensureBoolean(operand);
    if (!operand) {
      return nullptr;
    }
    return builder.CreateNot(operand, "nottmp");
  }
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::emitLiteralExpr(const ast::LiteralExpr& expr) {
  if (expr.value == "true") {
    return llvm::ConstantInt::getTrue(context);
  }
  if (expr.value == "false") {
    return llvm::ConstantInt::getFalse(context);
  }
  if (expr.value == "null") {
    return llvm::ConstantPointerNull::get(getBytePtrType());
  }
  if (!expr.value.empty() && expr.value.front() == '"') {
    auto value = stripQuotes(expr.value);
    return builder.CreateGlobalString(value, "str", 0, module.get());
  }
  if (expr.value.find('.') != std::string::npos) {
    double number = 0.0;
    if (!numeric::parseFloat(expr.value, number)) {
      return nullptr;
    }
    return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context), number);
  }
  int64_t number = 0;
  if (!numeric::parseInt(expr.value, number)) {
    return nullptr;
  }
  return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), number, true);
}

llvm::Value* CodeGenModule::Impl::emitIdentifierExpr(const ast::IdentifierExpr& expr) {
  auto* alloca = lookupVariable(expr.name);
  if (!alloca) {
    auto it = functionNames.find(expr.name);
    return it == functionNames.end() ? nullptr : it->second;
  }
  return builder.CreateLoad(alloca->getAllocatedType(), alloca, expr.name);
}

llvm::Value* CodeGenModule::Impl::emitFieldAccessExpr(const ast::FieldAccessExpr& expr) {
  if (!expr.base) {
    return nullptr;
  }
  auto* baseValue = emitExpression(expr.base.get());
  if (!baseValue) {
    return nullptr;
  }
  auto baseType = expr.base->resolvedType();
  if (!baseType) {
    return nullptr;
  }
  if (baseType->kind == typing::TypeKind::Array) {
    if (expr.field != u8"طول") {
      return nullptr;
    }
    emitGuard(builder.CreateIsNotNull(baseValue), "Null array access");
    auto* arrayType = getArrayRuntimeType();
    auto* lenPtr = builder.CreateStructGEP(arrayType, baseValue, 0, "arr.len.ptr");
    return builder.CreateLoad(builder.getInt64Ty(), lenPtr, "arr.len");
  }
  if (baseType->kind != typing::TypeKind::Product) {
    return nullptr;
  }
  const auto& product = std::get<typing::ProductTypeInfo>(baseType->payload);
  if (product.name.empty()) {
    return nullptr;
  }
  const auto* decl = findStructDecl(product.name);
  if (!decl) {
    return nullptr;
  }
  std::size_t index = 0;
  for (const auto& field : decl->fields) {
    if (field->name == expr.field) {
      return builder.CreateExtractValue(baseValue, {static_cast<unsigned>(index)}, expr.field);
    }
    index++;
  }
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::emitCallExpr(const ast::CallExpr& expr) {
  llvm::Function* callee = getOrDeclareFunction(expr.callee);
  if (!callee) {
    return nullptr;
  }
  std::vector<llvm::Value*> args;
  args.reserve(expr.args.size());
  auto* funcType = callee->getFunctionType();
  for (std::size_t i = 0; i < expr.args.size(); ++i) {
    auto* value = emitExpression(expr.args[i].get());
    if (!value) {
      return nullptr;
    }
    if (i < funcType->getNumParams()) {
      value = convertToType(value, funcType->getParamType(i));
      if (!value) {
        return nullptr;
      }
    }
    args.push_back(value);
  }
  if (callee->getReturnType()->isVoidTy()) {
    builder.CreateCall(callee, args);
    return llvm::ConstantPointerNull::get(getBytePtrType());
  }
  return builder.CreateCall(callee, args, expr.callee + ".call");
}

llvm::Value* CodeGenModule::Impl::emitIndexExpr(const ast::IndexExpr& expr) {
  if (!expr.base || !expr.index) {
    return nullptr;
  }
  auto* arrayValue = emitExpression(expr.base.get());
  if (!arrayValue) return nullptr;
  emitGuard(builder.CreateIsNotNull(arrayValue), "Null array access");
  auto* indexValue = emitExpression(expr.index.get());
  if (!arrayValue || !indexValue) {
    return nullptr;
  }

  llvm::Type* elementType = nullptr;
  auto baseType = expr.base->resolvedType();
  if (baseType && baseType->kind == typing::TypeKind::Array) {
    const auto& info = std::get<typing::ArrayTypeInfo>(baseType->payload);
    elementType = getTypeByCheckedType(info.element);
  }
  if (!elementType) {
    return nullptr;
  }

  auto* elementPtr = emitCheckedArrayDataPtr(arrayValue, indexValue, elementType);
  if (!elementPtr) {
    return nullptr;
  }
  return builder.CreateLoad(elementType, elementPtr, "arr.elem");
}

llvm::Value* CodeGenModule::Impl::emitArrayLiteral(const ast::ArrayLiteralExpr& expr) {
  llvm::Type* elementType = nullptr;
  auto resolvedArrayType = expr.resolvedType();
  if (resolvedArrayType && resolvedArrayType->kind == typing::TypeKind::Array) {
    const auto& info = std::get<typing::ArrayTypeInfo>(resolvedArrayType->payload);
    elementType = getTypeByCheckedType(info.element);
  }

  std::vector<llvm::Value*> values;
  values.reserve(expr.elements.size());
  for (const auto& element : expr.elements) {
    auto* value = emitExpression(element.get());
    if (!value) {
      return nullptr;
    }
    values.push_back(value);
  }

  if (!elementType && !values.empty()) {
    elementType = values.front()->getType();
  }
  if (!elementType) {
    return llvm::ConstantPointerNull::get(getBytePtrType());
  }

  auto* mallocFn = module->getFunction("malloc");
  if (!mallocFn) {
    auto* mallocType = llvm::FunctionType::get(getBytePtrType(), {builder.getInt64Ty()}, false);
    mallocFn =
        llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
  }

  auto* count = llvm::ConstantInt::get(builder.getInt64Ty(), values.size());
  auto* count64 = builder.CreateZExt(count, builder.getInt64Ty(), "arr.count64");
  auto* elementSize = llvm::ConstantExpr::getSizeOf(elementType);
  auto* bytes = builder.CreateMul(count64, elementSize, "arr.bytes");
  auto* rawData = builder.CreateCall(mallocFn, {bytes}, "arr.data.raw");
  emitGuard(builder.CreateIsNotNull(rawData), "Allocation failed");

  for (std::size_t i = 0; i < values.size(); ++i) {
    auto* converted = convertToType(values[i], elementType);
    if (!converted) {
      return nullptr;
    }
    auto* index = llvm::ConstantInt::get(builder.getInt64Ty(), i);
    auto* slot = builder.CreateInBoundsGEP(elementType, rawData, index, "arrayelt.ptr");
    builder.CreateStore(converted, slot);
  }

  auto* runtimeArrayType = getArrayRuntimeType();
  auto* headerSize = llvm::ConstantExpr::getSizeOf(runtimeArrayType);
  auto* rawHeader = builder.CreateCall(mallocFn, {headerSize}, "arr.header.raw");
  emitGuard(builder.CreateIsNotNull(rawHeader), "Allocation failed");
  auto* lenPtr = builder.CreateStructGEP(runtimeArrayType, rawHeader, 0, "arr.len.ptr");
  auto* dataPtrPtr = builder.CreateStructGEP(runtimeArrayType, rawHeader, 1, "arr.data.ptr");
  builder.CreateStore(count, lenPtr);
  builder.CreateStore(rawData, dataPtrPtr);
  return rawHeader;
}

llvm::Value* CodeGenModule::Impl::emitStructLiteral(const ast::StructLiteralExpr& expr) {
  const auto* decl = findStructDecl(expr.typeName);
  if (!decl) {
    return nullptr;
  }
  auto* structTy = getStructType(*decl);
  if (!structTy) {
    return nullptr;
  }

  std::unordered_map<std::string, const ast::StructFieldInit*> initMap;
  initMap.reserve(expr.fields.size());
  for (const auto& init : expr.fields) {
    if (init) {
      initMap[init->name] = init.get();
    }
  }

  llvm::Value* value = llvm::UndefValue::get(structTy);
  unsigned index = 0;
  for (const auto& field : decl->fields) {
    auto it = initMap.find(field->name);
    if (it == initMap.end()) {
      return nullptr;
    }
    auto* fieldValue = emitExpression(it->second->value.get());
    if (!fieldValue) {
      return nullptr;
    }
    auto* fieldTy = structTy->getElementType(index);
    fieldValue = convertToType(fieldValue, fieldTy);
    if (!fieldValue) {
      return nullptr;
    }
    value = builder.CreateInsertValue(value, fieldValue, {index}, field->name);
    index++;
  }
  return value;
}

llvm::Value* CodeGenModule::Impl::emitArithmeticBinary(ast::BinaryOp op, llvm::Value* lhs,
                                                       llvm::Value* rhs) {
  bool useFloat = false;
  if (!promoteNumericOperands(lhs, rhs, useFloat)) {
    return nullptr;
  }
  if (!useFloat) return checkedInteger(op, lhs, rhs);
  switch (op) {
  case ast::BinaryOp::Add:
    return builder.CreateFAdd(lhs, rhs, "faddtmp");
  case ast::BinaryOp::Sub:
    return builder.CreateFSub(lhs, rhs, "fsubtmp");
  case ast::BinaryOp::Mul:
    return builder.CreateFMul(lhs, rhs, "fmultmp");
  case ast::BinaryOp::Div:
    emitGuard(builder.CreateFCmpUNE(rhs, llvm::ConstantFP::get(rhs->getType(), 0.0)),
              "Division by zero");
    return builder.CreateFDiv(lhs, rhs, "fdivtmp");
  default:
    return nullptr;
  }
}

void CodeGenModule::Impl::emitGuard(llvm::Value* valid, const std::string& message) {
  auto* function = builder.GetInsertBlock()->getParent();
  auto* ok = llvm::BasicBlock::Create(context, "check.ok", function);
  auto* fail = llvm::BasicBlock::Create(context, "check.fail", function);
  builder.CreateCondBr(valid, ok, fail);
  builder.SetInsertPoint(fail);
  const std::string text = "runtime error: " + message + "\n";
  auto* msg = builder.CreateGlobalString(text, "error", 0, module.get());
  auto write = module->getOrInsertFunction(
      "write", llvm::FunctionType::get(
                   builder.getInt64Ty(),
                   {builder.getInt32Ty(), getBytePtrType(), builder.getInt64Ty()}, false));
  builder.CreateCall(write, {builder.getInt32(2), msg, builder.getInt64(text.size())});
  auto exit = module->getOrInsertFunction(
      "exit", llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty()}, false));
  builder.CreateCall(exit, {builder.getInt32(1)});
  builder.CreateUnreachable();
  builder.SetInsertPoint(ok);
}

llvm::Value* CodeGenModule::Impl::checkedInteger(ast::BinaryOp op, llvm::Value* lhs,
                                                 llvm::Value* rhs) {
  if (op == ast::BinaryOp::Div) {
    emitGuard(builder.CreateICmpNE(rhs, builder.getInt64(0)), "Division by zero");
    auto* overflow = builder.CreateAnd(builder.CreateICmpEQ(lhs, builder.getInt64(INT64_MIN)),
                                       builder.CreateICmpEQ(rhs, builder.getInt64(-1)));
    emitGuard(builder.CreateNot(overflow), "Integer overflow");
    return builder.CreateSDiv(lhs, rhs, "divtmp");
  }
  const auto id = op == ast::BinaryOp::Add   ? llvm::Intrinsic::sadd_with_overflow
                  : op == ast::BinaryOp::Sub ? llvm::Intrinsic::ssub_with_overflow
                                             : llvm::Intrinsic::smul_with_overflow;
#if LLVM_VERSION_MAJOR >= 19
  auto* intrinsic =
      llvm::Intrinsic::getOrInsertDeclaration(module.get(), id, {builder.getInt64Ty()});
#else
  auto* intrinsic = llvm::Intrinsic::getDeclaration(module.get(), id, {builder.getInt64Ty()});
#endif
  auto* result = builder.CreateCall(intrinsic, {lhs, rhs});
  auto* value = builder.CreateExtractValue(result, 0);
  emitGuard(builder.CreateNot(builder.CreateExtractValue(result, 1)), "Integer overflow");
  return value;
}

llvm::Value* CodeGenModule::Impl::emitShortCircuit(const ast::BinaryExpr& expr) {
  auto* lhs = ensureBoolean(emitExpression(expr.lhs.get()));
  if (!lhs) return nullptr;
  auto* from = builder.GetInsertBlock();
  auto* function = from->getParent();
  auto* rhsBlock = llvm::BasicBlock::Create(context, "logic.rhs", function);
  auto* end = llvm::BasicBlock::Create(context, "logic.end", function);
  const bool isAnd = expr.op == ast::BinaryOp::And;
  builder.CreateCondBr(lhs, isAnd ? rhsBlock : end, isAnd ? end : rhsBlock);
  builder.SetInsertPoint(rhsBlock);
  auto* rhs = ensureBoolean(emitExpression(expr.rhs.get()));
  if (!rhs) return nullptr;
  auto* rhsEnd = builder.GetInsertBlock();
  builder.CreateBr(end);
  builder.SetInsertPoint(end);
  auto* result = builder.CreatePHI(builder.getInt1Ty(), 2);
  result->addIncoming(builder.getInt1(!isAnd), from);
  result->addIncoming(rhs, rhsEnd);
  return result;
}

llvm::Value* CodeGenModule::Impl::emitStringEqual(llvm::Value* lhs, llvm::Value* rhs) {
  auto* from = builder.GetInsertBlock();
  auto* function = from->getParent();
  auto* compare = llvm::BasicBlock::Create(context, "str.compare", function);
  auto* end = llvm::BasicBlock::Create(context, "str.end", function);
  auto* same = builder.CreateICmpEQ(lhs, rhs);
  auto* both = builder.CreateAnd(builder.CreateIsNotNull(lhs), builder.CreateIsNotNull(rhs));
  builder.CreateCondBr(both, compare, end);
  builder.SetInsertPoint(compare);
  auto strcmp = module->getOrInsertFunction(
      "strcmp",
      llvm::FunctionType::get(builder.getInt32Ty(), {getBytePtrType(), getBytePtrType()}, false));
  auto* equal = builder.CreateICmpEQ(builder.CreateCall(strcmp, {lhs, rhs}), builder.getInt32(0));
  builder.CreateBr(end);
  builder.SetInsertPoint(end);
  auto* result = builder.CreatePHI(builder.getInt1Ty(), 2);
  result->addIncoming(same, from);
  result->addIncoming(equal, compare);
  return result;
}

bool CodeGenModule::Impl::emitEntryWrapper() {
  auto it = functionNames.find(std::string(kEntryFunctionName));
  if (it == functionNames.end()) {
    lastErrorMessage = "Missing entry function 'بداية'";
    return false;
  }
  auto* entry = it->second;
  auto* main = llvm::Function::Create(llvm::FunctionType::get(builder.getInt32Ty(), false),
                                      llvm::Function::ExternalLinkage, "main", module.get());
  builder.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", main));
  auto* result = builder.CreateCall(entry);
  if (result->getType()->isIntegerTy())
    builder.CreateRet(builder.CreateIntCast(result, builder.getInt32Ty(), false));
  else builder.CreateRet(builder.getInt32(0));
  return true;
}

llvm::Value* CodeGenModule::Impl::emitComparisonBinary(ast::BinaryOp op, llvm::Value* lhs,
                                                       llvm::Value* rhs) {
  bool useFloat = false;
  if (!promoteNumericOperands(lhs, rhs, useFloat)) {
    return nullptr;
  }
  switch (op) {
  case ast::BinaryOp::Eq:
    return useFloat ? builder.CreateFCmpOEQ(lhs, rhs, "feqtmp")
                    : builder.CreateICmpEQ(lhs, rhs, "eqtmp");
  case ast::BinaryOp::Ne:
    return useFloat ? builder.CreateFCmpUNE(lhs, rhs, "fnetmp")
                    : builder.CreateICmpNE(lhs, rhs, "netmp");
  case ast::BinaryOp::Lt:
    return useFloat ? builder.CreateFCmpOLT(lhs, rhs, "flttmp")
                    : builder.CreateICmpSLT(lhs, rhs, "lttmp");
  case ast::BinaryOp::Le:
    return useFloat ? builder.CreateFCmpOLE(lhs, rhs, "fletmp")
                    : builder.CreateICmpSLE(lhs, rhs, "letmp");
  case ast::BinaryOp::Gt:
    return useFloat ? builder.CreateFCmpOGT(lhs, rhs, "fgttmp")
                    : builder.CreateICmpSGT(lhs, rhs, "gttmp");
  case ast::BinaryOp::Ge:
    return useFloat ? builder.CreateFCmpOGE(lhs, rhs, "fgetmp")
                    : builder.CreateICmpSGE(lhs, rhs, "getmp");
  default:
    return nullptr;
  }
}

llvm::Function* CodeGenModule::Impl::getOrDeclareFunction(const std::string& name) {
  if (auto it = functionNames.find(name); it != functionNames.end()) return it->second;
  if (auto* existing = module->getFunction(name)) {
    return existing;
  }
  if (const auto* builtin = stdRuntime.resolve(name)) {
    return declareBuiltinFunction(*builtin);
  }
  lastErrorMessage = "Unsupported function: " + name;
  return nullptr;
}

llvm::Function*
CodeGenModule::Impl::declareBuiltinFunction(const stdlib::StdFunctionDescriptor& descriptor) {
  if (descriptor.asciiName == "std_print") {
    auto* type = llvm::FunctionType::get(builder.getInt64Ty(), {getBytePtrType()},
                                         /*isVarArg=*/false);
    return llvm::Function::Create(type, llvm::Function::ExternalLinkage, descriptor.arabicName,
                                  module.get());
  }
  lastErrorMessage = "Unsupported builtin: " + descriptor.arabicName;
  return nullptr;
}

void CodeGenModule::Impl::injectStdRuntime() {
  if (stdRuntimeInjected) {
    return;
  }
  stdRuntimeInjected = true;

  auto* bytePtr = getBytePtrType();
  auto* intTy = builder.getInt32Ty();

  auto* printfType = llvm::FunctionType::get(intTy, {bytePtr}, true);
  auto* printfFunc = module->getFunction("printf");
  if (!printfFunc) {
    printfFunc =
        llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", module.get());
  }

  (void)printfFunc;
  defineStdFunctionBodies();
}

void CodeGenModule::Impl::defineStdFunctionBodies() {
  for (const auto& fn : stdRuntime.functions()) {
    if (fn.asciiName == "std_print") {
      definePrintFunction(fn);
    }
  }
}

void CodeGenModule::Impl::definePrintFunction(const stdlib::StdFunctionDescriptor& descriptor) {
  auto* printFunc = getOrDeclareFunction(descriptor.arabicName);
  if (!printFunc || !printFunc->empty()) {
    return;
  }
  if (printFunc->isVarArg() || printFunc->arg_size() != 1) {
    return;
  }

  auto* printfFunc = module->getFunction("printf");
  if (!printfFunc) {
    return;
  }

  auto argIter = printFunc->arg_begin();
  argIter->setName("msg");

  auto* entry = llvm::BasicBlock::Create(context, "entry", printFunc);
  const auto saved = builder.saveIP();
  builder.SetInsertPoint(entry);
  llvm::Value* msg = &*argIter;
  emitGuard(builder.CreateIsNotNull(msg), "Null string access");
  const std::string formatName(identifiers::kStdPrintFormatSymbol);
  auto* format = builder.CreateGlobalString("%s\n", formatName, 0, module.get());
  builder.CreateCall(printfFunc, {format, msg});
  // The language print result is a status, not a platform-dependent byte count.
  builder.CreateRet(builder.getInt64(0));
  builder.restoreIP(saved);
}

std::string CodeGenModule::Impl::emitIR() const {
  std::string buffer;
  llvm::raw_string_ostream os(buffer);
  module->print(os, nullptr);
  return os.str();
}

CodeGenModule::CodeGenModule(std::string moduleName)
    : impl(std::make_unique<Impl>(std::move(moduleName))) {}

CodeGenModule::~CodeGenModule() = default;

bool CodeGenModule::generate(const ast::Program& program) { return impl->generate(program); }

llvm::Module& CodeGenModule::module() { return impl->getModule(); }

const llvm::Module& CodeGenModule::module() const { return impl->getModule(); }

llvm::LLVMContext& CodeGenModule::context() { return impl->getContext(); }

std::string CodeGenModule::emitIR() const { return impl->emitIR(); }

std::string CodeGenModule::lastError() const { return impl->lastError(); }

CodeGenResult emitModuleIR(const ast::Program& program, std::string moduleName) {
  CodeGenModule codegen(std::move(moduleName));
  CodeGenResult result;
  result.success = codegen.generate(program);
  if (result.success) {
    result.ir = codegen.emitIR();
  } else {
    result.error = codegen.lastError();
  }
  return result;
}

} // namespace dhad::codegen
