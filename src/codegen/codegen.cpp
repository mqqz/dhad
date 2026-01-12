#include "codegen.hpp"

#include "../ast/ast.hpp"
#include "../std/identifiers.hpp"
#include "../std/runtime.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

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
constexpr const char* kEntryFunctionLLVMName = "main";

enum class ArithmeticOp { Add, Sub, Mul, Div };
enum class LogicalOp { And, Or };
enum class ComparisonOp { Eq, Ne, Lt, Le, Gt, Ge };

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

std::optional<ArithmeticOp> decodeArithmeticOp(std::string_view token) {
  if (token == "+") return ArithmeticOp::Add;
  if (token == "-") return ArithmeticOp::Sub;
  if (token == "*") return ArithmeticOp::Mul;
  if (token == "/") return ArithmeticOp::Div;
  return std::nullopt;
}

std::optional<LogicalOp> decodeLogicalOp(std::string_view token) {
  if (token == "and") return LogicalOp::And;
  if (token == "or") return LogicalOp::Or;
  return std::nullopt;
}

std::optional<ComparisonOp> decodeComparisonOp(std::string_view token) {
  if (token == "==") return ComparisonOp::Eq;
  if (token == "!=") return ComparisonOp::Ne;
  if (token == "<") return ComparisonOp::Lt;
  if (token == "<=") return ComparisonOp::Le;
  if (token == ">") return ComparisonOp::Gt;
  if (token == ">=") return ComparisonOp::Ge;
  return std::nullopt;
}

} // namespace

struct CodeGenModule::Impl {
  struct StatementEmitter : ast::ASTVisitor {
    explicit StatementEmitter(Impl& owner) : impl(owner) {}

    void visit(const ast::BlockStmt& node) override { result = impl.emitBlock(&node); }
    void visit(const ast::DeclarationStmt& node) override { result = impl.emitDeclaration(node); }
    void visit(const ast::AssignmentStmt& node) override { result = impl.emitAssignment(node); }
    void visit(const ast::IfStmt& node) override { result = impl.emitIf(node); }
    void visit(const ast::WhileStmt& node) override { result = impl.emitWhile(node); }
    void visit(const ast::ForStmt& node) override { result = impl.emitFor(node); }
    void visit(const ast::ReturnStmt& node) override { result = impl.emitReturn(node); }
    void visit(const ast::BreakStmt&) override { result = impl.emitBreak(); }
    void visit(const ast::ContinueStmt&) override { result = impl.emitContinue(); }
    void visit(const ast::ExpressionStmt& node) override {
      if (node.expr) {
        (void)impl.emitExpression(node.expr.get());
      }
      result = true;
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
    void visit(const ast::CallExpr& node) override { value = impl.emitCallExpr(node); }
    void visit(const ast::ArrayLiteralExpr& node) override { value = impl.emitArrayLiteral(node); }

    llvm::Value* value{nullptr};

  private:
    Impl& impl;
  };

  explicit Impl(std::string name)
      : moduleName(std::move(name)), module(std::make_unique<llvm::Module>(moduleName, context)),
        builder(context) {
    module->setTargetTriple(llvm::Triple(getDefaultTargetTriple()));
  }

  bool generate(const ast::Program& program);
  llvm::Module& getModule() { return *module; }
  llvm::LLVMContext& getContext() { return context; }
  std::string emitIR() const;

private:
  llvm::Type* getTypeByExpr(const ast::TypeExpr* type);
  llvm::Type* getTypeByLiteral(const ast::LiteralExpr& literal);
  llvm::PointerType* getBytePtrType();
  llvm::Value* emitExpression(const ast::Expression* expr);
  llvm::Value* emitBinaryExpr(const ast::BinaryExpr& expr);
  llvm::Value* emitUnaryExpr(const ast::UnaryExpr& expr);
  llvm::Value* emitLiteralExpr(const ast::LiteralExpr& expr);
  llvm::Value* emitIdentifierExpr(const ast::IdentifierExpr& expr);
  llvm::Value* emitCallExpr(const ast::CallExpr& expr);
  llvm::Value* emitArrayLiteral(const ast::ArrayLiteralExpr& expr);
  llvm::Value* emitArithmeticBinary(ArithmeticOp op, llvm::Value* lhs, llvm::Value* rhs);
  llvm::Value* emitLogicalBinary(LogicalOp op, llvm::Value* lhs, llvm::Value* rhs);
  llvm::Value* emitComparisonBinary(ComparisonOp op, llvm::Value* lhs, llvm::Value* rhs);
  llvm::Function* getOrDeclareFunction(const std::string& name);
  llvm::Function* declareBuiltinFunction(const stdlib::StdFunctionDescriptor& descriptor);
  llvm::Function* declareExternalFallback(const std::string& name);
  bool emitStatement(const ast::Statement* stmt);
  bool emitBlock(const ast::BlockStmt* block);
  bool emitDeclaration(const ast::DeclarationStmt& stmt);
  bool emitAssignment(const ast::AssignmentStmt& stmt);
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

  std::string moduleName;
  llvm::LLVMContext context;
  std::unique_ptr<llvm::Module> module;
  llvm::IRBuilder<> builder;
  llvm::Function* currentFunction{nullptr};
  llvm::Type* currentReturnType{nullptr};
  std::vector<std::unordered_map<std::string, llvm::AllocaInst*>> scopes;
  std::vector<LoopContext> loopStack;
  std::unordered_map<const ast::FunctionDecl*, llvm::Function*> functions;
  bool stdRuntimeInjected{false};
  stdlib::StdRuntime stdRuntime;
};

bool CodeGenModule::Impl::generate(const ast::Program& program) {
  module = std::make_unique<llvm::Module>(moduleName, context);
  functions.clear();
  stdRuntimeInjected = false;
  bool ok = true;

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
    return false;
  }

  injectStdRuntime();

  return !llvm::verifyModule(*module, &llvm::errs());
}

llvm::PointerType* CodeGenModule::Impl::getBytePtrType() {
  return llvm::PointerType::get(context, 0);
}

llvm::Type* CodeGenModule::Impl::getTypeByExpr(const ast::TypeExpr* type) {
  if (!type) {
    return nullptr;
  }
  if (auto* prim = llvm::dyn_cast<ast::TypePrimitiveExpr>(type)) {
    switch (prim->kind) {
    case typing::TypeKind::Int:
      return llvm::Type::getInt32Ty(context);
    case typing::TypeKind::Bool:
      return llvm::Type::getInt1Ty(context);
    case typing::TypeKind::Float:
      return llvm::Type::getFloatTy(context);
    case typing::TypeKind::Char:
      return llvm::Type::getInt8Ty(context);
    case typing::TypeKind::String:
    case typing::TypeKind::Null:
      return getBytePtrType();
    default:
      return nullptr;
    }
  }
  if (llvm::isa<ast::TypeArrayExpr>(type)) {
    return getBytePtrType();
  }
  if (llvm::isa<ast::TypeSumExpr>(type)) {
    return getBytePtrType();
  }
  if (llvm::isa<ast::TypeProductExpr>(type)) {
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
    return llvm::Type::getFloatTy(context);
  }
  return llvm::Type::getInt32Ty(context);
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
  if (value->getType()->isIntegerTy(1) && type->isIntegerTy(32)) {
    return builder.CreateZExt(value, type, "booltoint");
  }
  if (value->getType()->isIntegerTy(32) && type->isIntegerTy(1)) {
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
  if (value->getType()->isIntegerTy(32)) {
    return value;
  }
  if (value->getType()->isIntegerTy(1)) {
    return builder.CreateZExt(value, builder.getInt32Ty(), "booltoint");
  }
  if (value->getType()->isIntegerTy()) {
    return builder.CreateIntCast(value, builder.getInt32Ty(), true, "intcast");
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
    return builder.CreateSIToFP(value, builder.getFloatTy(), "sitofp");
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
  std::string llvmName =
      fn.name == kEntryFunctionName ? std::string(kEntryFunctionLLVMName) : fn.name;
  auto* function =
      llvm::Function::Create(type, llvm::Function::ExternalLinkage, llvmName, module.get());
  functions[&fn] = function;
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
  llvm::Type* type = stmt.typeName ? getTypeByExpr(stmt.typeName.get()) : nullptr;
  if (!type && initializer) {
    type = initializer->getType();
  }
  if (!type) {
    type = llvm::Type::getInt32Ty(context);
  }
  auto* var = createAlloca(currentFunction, type, stmt.name);
  llvm::Value* value =
      initializer ? convertToType(initializer, type) : llvm::Constant::getNullValue(type);
  if (!value) {
    value = llvm::Constant::getNullValue(type);
  }
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

  auto* after =
      llvm::BasicBlock::Create(context, "ret.cont", builder.GetInsertBlock()->getParent());
  builder.SetInsertPoint(after);
  return true;
}

bool CodeGenModule::Impl::emitBreak() {
  if (loopStack.empty()) {
    return false;
  }
  builder.CreateBr(loopStack.back().breakTarget);
  auto* after =
      llvm::BasicBlock::Create(context, "break.cont", builder.GetInsertBlock()->getParent());
  builder.SetInsertPoint(after);
  return true;
}

bool CodeGenModule::Impl::emitContinue() {
  if (loopStack.empty()) {
    return false;
  }
  builder.CreateBr(loopStack.back().continueTarget);
  auto* after =
      llvm::BasicBlock::Create(context, "continue.cont", builder.GetInsertBlock()->getParent());
  builder.SetInsertPoint(after);
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
  return emitter.value;
}

llvm::Value* CodeGenModule::Impl::emitBinaryExpr(const ast::BinaryExpr& expr) {
  auto* lhs = emitExpression(expr.lhs.get());
  auto* rhs = emitExpression(expr.rhs.get());
  if (!lhs || !rhs) {
    return nullptr;
  }

  const std::string_view op(expr.op);
  if (auto arith = decodeArithmeticOp(op)) {
    return emitArithmeticBinary(*arith, lhs, rhs);
  }
  if (auto logical = decodeLogicalOp(op)) {
    return emitLogicalBinary(*logical, lhs, rhs);
  }
  if (auto cmp = decodeComparisonOp(op)) {
    return emitComparisonBinary(*cmp, lhs, rhs);
  }

  return nullptr;
}

llvm::Value* CodeGenModule::Impl::emitUnaryExpr(const ast::UnaryExpr& expr) {
  auto* operand = emitExpression(expr.operand.get());
  if (!operand) {
    return nullptr;
  }
  if (expr.op == "-") {
    operand = ensureInteger(operand);
    if (!operand) {
      return nullptr;
    }
    return builder.CreateNeg(operand, "negtmp");
  }
  if (expr.op == "not") {
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
    float number = std::stof(expr.value);
    return llvm::ConstantFP::get(llvm::Type::getFloatTy(context), number);
  }
  int number = std::stoi(expr.value);
  return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), number, true);
}

llvm::Value* CodeGenModule::Impl::emitIdentifierExpr(const ast::IdentifierExpr& expr) {
  auto* alloca = lookupVariable(expr.name);
  if (!alloca) {
    return module->getFunction(expr.name);
  }
  return builder.CreateLoad(alloca->getAllocatedType(), alloca, expr.name);
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
  return builder.CreateCall(callee, args, expr.callee + ".call");
}

llvm::Value* CodeGenModule::Impl::emitArrayLiteral(const ast::ArrayLiteralExpr& expr) {
  (void)expr;
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::emitArithmeticBinary(ArithmeticOp op, llvm::Value* lhs,
                                                       llvm::Value* rhs) {
  bool useFloat = false;
  if (!promoteNumericOperands(lhs, rhs, useFloat)) {
    return nullptr;
  }
  switch (op) {
  case ArithmeticOp::Add:
    return useFloat ? builder.CreateFAdd(lhs, rhs, "faddtmp")
                    : builder.CreateAdd(lhs, rhs, "addtmp");
  case ArithmeticOp::Sub:
    return useFloat ? builder.CreateFSub(lhs, rhs, "fsubtmp")
                    : builder.CreateSub(lhs, rhs, "subtmp");
  case ArithmeticOp::Mul:
    return useFloat ? builder.CreateFMul(lhs, rhs, "fmultmp")
                    : builder.CreateMul(lhs, rhs, "multmp");
  case ArithmeticOp::Div:
    return useFloat ? builder.CreateFDiv(lhs, rhs, "fdivtmp")
                    : builder.CreateSDiv(lhs, rhs, "divtmp");
  }
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::emitLogicalBinary(LogicalOp op, llvm::Value* lhs,
                                                    llvm::Value* rhs) {
  lhs = ensureBoolean(lhs);
  rhs = ensureBoolean(rhs);
  if (!lhs || !rhs) {
    return nullptr;
  }
  switch (op) {
  case LogicalOp::And:
    return builder.CreateAnd(lhs, rhs, "andtmp");
  case LogicalOp::Or:
    return builder.CreateOr(lhs, rhs, "ortmp");
  }
  return nullptr;
}

llvm::Value* CodeGenModule::Impl::emitComparisonBinary(ComparisonOp op, llvm::Value* lhs,
                                                       llvm::Value* rhs) {
  bool useFloat = false;
  if (!promoteNumericOperands(lhs, rhs, useFloat)) {
    return nullptr;
  }
  switch (op) {
  case ComparisonOp::Eq:
    return useFloat ? builder.CreateFCmpOEQ(lhs, rhs, "feqtmp")
                    : builder.CreateICmpEQ(lhs, rhs, "eqtmp");
  case ComparisonOp::Ne:
    return useFloat ? builder.CreateFCmpONE(lhs, rhs, "fnetmp")
                    : builder.CreateICmpNE(lhs, rhs, "netmp");
  case ComparisonOp::Lt:
    return useFloat ? builder.CreateFCmpOLT(lhs, rhs, "flttmp")
                    : builder.CreateICmpSLT(lhs, rhs, "lttmp");
  case ComparisonOp::Le:
    return useFloat ? builder.CreateFCmpOLE(lhs, rhs, "fletmp")
                    : builder.CreateICmpSLE(lhs, rhs, "letmp");
  case ComparisonOp::Gt:
    return useFloat ? builder.CreateFCmpOGT(lhs, rhs, "fgttmp")
                    : builder.CreateICmpSGT(lhs, rhs, "gttmp");
  case ComparisonOp::Ge:
    return useFloat ? builder.CreateFCmpOGE(lhs, rhs, "fgetmp")
                    : builder.CreateICmpSGE(lhs, rhs, "getmp");
  }
  return nullptr;
}

llvm::Function* CodeGenModule::Impl::getOrDeclareFunction(const std::string& name) {
  if (auto* existing = module->getFunction(name)) {
    return existing;
  }
  if (const auto* builtin = stdRuntime.resolve(name)) {
    return declareBuiltinFunction(*builtin);
  }
  return declareExternalFallback(name);
}

llvm::Function*
CodeGenModule::Impl::declareBuiltinFunction(const stdlib::StdFunctionDescriptor& descriptor) {
  if (descriptor.asciiName == "std_print") {
    auto* type = llvm::FunctionType::get(builder.getInt32Ty(), {getBytePtrType()},
                                         /*isVarArg=*/false);
    return llvm::Function::Create(type, llvm::Function::ExternalLinkage, descriptor.arabicName,
                                  module.get());
  }
  return declareExternalFallback(descriptor.arabicName);
}

llvm::Function* CodeGenModule::Impl::declareExternalFallback(const std::string& name) {
  std::vector<llvm::Type*> params;
  auto* type = llvm::FunctionType::get(builder.getInt32Ty(), params, /*isVarArg=*/true);
  return llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, module.get());
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
  llvm::IRBuilder<> runtimeBuilder(entry);
  const std::string formatName(identifiers::kStdPrintFormatSymbol);
  llvm::Value* format = runtimeBuilder.CreateGlobalString("%s\n", formatName, 0, module.get());
  llvm::Value* msg = &*argIter;
  auto* call = runtimeBuilder.CreateCall(printfFunc, {format, msg});
  runtimeBuilder.CreateRet(call);
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

CodeGenResult emitModuleIR(const ast::Program& program, std::string moduleName) {
  CodeGenModule codegen(std::move(moduleName));
  CodeGenResult result;
  result.success = codegen.generate(program);
  if (result.success) {
    result.ir = codegen.emitIR();
  }
  return result;
}

} // namespace dhad::codegen
