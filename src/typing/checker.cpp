#include "typing/checker.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "../std/runtime.hpp"

namespace dhad::typing {
namespace {

TypePtr voidType() { return makePrimitive(TypeKind::Null); }

} // namespace

TypeChecker::TypeChecker() : scopes_(), currentReturnType_(voidType()) {}

TypeCheckerResult TypeChecker::check(ast::Program& program) {
  errors_.clear();
  scopes_ = ScopeStack{};
  currentFunction_ = nullptr;
  currentReturnType_ = voidType();
  loopDepth_ = 0;
  structDecls_.clear();
  enumDecls_.clear();
  namedTypes_.clear();
  resolvingTypes_.clear();

  registerTypeDecls(program);
  for (const auto& entry : structDecls_) {
    (void)resolveNamedType(entry.first, entry.second->getLocation());
  }
  for (const auto& entry : enumDecls_) {
    (void)resolveNamedType(entry.first, entry.second->getLocation());
  }

  // Declare functions first so they can reference each other.
  for (auto& node : program.topLevel) {
    if (auto* fn = llvm::dyn_cast<ast::FunctionDecl>(node.get())) {
      declareGlobalFunction(*fn);
    }
  }

  bool ok = true;
  for (auto& node : program.topLevel) {
    ok &= checkTopLevel(*node);
  }

  return TypeCheckerResult{ok && errors_.empty(), errors_};
}

void TypeChecker::reportError(const SourceLocation& loc, std::string message) {
  errors_.push_back(TypeError{loc, std::move(message)});
}

bool TypeChecker::declareGlobalFunction(ast::FunctionDecl& fn) {
  std::vector<TypePtr> params;
  params.reserve(fn.params.size());
  for (const auto& param : fn.params) {
    auto type = resolveTypeExpr(param->type.get());
    if (!type) {
      std::ostringstream os;
      os << "Unknown parameter type in function '" << fn.name << "'";
      reportError(param->getLocation(), os.str());
      return false;
    }
    params.push_back(std::move(type));
  }

  TypePtr returnType = fn.returnType ? resolveTypeExpr(fn.returnType.get()) : voidType();
  if (!returnType) {
    std::ostringstream os;
    os << "Unknown return type in function '" << fn.name << "'";
    reportError(fn.getLocation(), os.str());
    return false;
  }

  auto signature = makeFunction(std::move(params), returnType);
  if (!scopes_.declare(fn.name, signature)) {
    reportError(fn.getLocation(), "Duplicate function '" + fn.name + "'");
    return false;
  }
  return true;
}

bool TypeChecker::checkTopLevel(ast::ASTNode& node) {
  if (auto* fn = llvm::dyn_cast<ast::FunctionDecl>(&node)) {
    return checkFunction(*fn);
  }
  if (llvm::isa<ast::StructDecl>(&node) || llvm::isa<ast::EnumDecl>(&node)) {
    return true;
  }
  if (auto* stmt = llvm::dyn_cast<ast::Statement>(&node)) {
    return checkStatement(*stmt);
  }
  if (auto* importDecl = llvm::dyn_cast<ast::ImportDecl>(&node)) {
    // Imports currently have no semantics.
    (void)importDecl;
    return true;
  }
  reportError(node.getLocation(), "Unsupported top-level construct");
  return false;
}

bool TypeChecker::checkFunction(ast::FunctionDecl& fn) {
  currentFunction_ = &fn.name;
  currentReturnType_ = fn.returnType ? resolveTypeExpr(fn.returnType.get()) : voidType();
  if (!currentReturnType_) {
    reportError(fn.getLocation(), "Unknown return type");
    currentReturnType_ = voidType();
  }

  scopes_.enterScope();
  for (const auto& param : fn.params) {
    auto type = resolveTypeExpr(param->type.get());
    if (!type) {
      reportError(param->getLocation(), "Unknown parameter type");
      continue;
    }
    if (!scopes_.declare(param->name, type)) {
      reportError(param->getLocation(), "Duplicate parameter '" + param->name + "'");
    }
  }

  bool ok = fn.body ? checkBlock(*fn.body) : true;
  if (ok && fn.body && currentReturnType_ && currentReturnType_->kind != TypeKind::Null) {
    if (!blockAlwaysReturns(*fn.body)) {
      reportError(fn.getLocation(), "Missing return in non-void function");
      ok = false;
    }
  }
  scopes_.exitScope();
  return ok;
}

bool TypeChecker::checkBlock(ast::BlockStmt& block) {
  scopes_.enterScope();
  bool ok = true;
  for (auto& stmt : block.statements) {
    ok &= checkStatement(*stmt);
  }
  scopes_.exitScope();
  return ok;
}

bool TypeChecker::checkStatement(ast::Statement& stmt) {
  if (auto* decl = llvm::dyn_cast<ast::DeclarationStmt>(&stmt)) {
    return checkDeclaration(*decl);
  }
  if (auto* assign = llvm::dyn_cast<ast::AssignmentStmt>(&stmt)) {
    return checkAssignment(*assign);
  }
  if (auto* assignIndex = llvm::dyn_cast<ast::IndexAssignmentStmt>(&stmt)) {
    return checkIndexAssignment(*assignIndex);
  }
  if (auto* ifStmt = llvm::dyn_cast<ast::IfStmt>(&stmt)) {
    return checkIf(*ifStmt);
  }
  if (auto* whileStmt = llvm::dyn_cast<ast::WhileStmt>(&stmt)) {
    return checkWhile(*whileStmt);
  }
  if (auto* forStmt = llvm::dyn_cast<ast::ForStmt>(&stmt)) {
    return checkFor(*forStmt);
  }
  if (auto* ret = llvm::dyn_cast<ast::ReturnStmt>(&stmt)) {
    return checkReturn(*ret);
  }
  if (auto* brk = llvm::dyn_cast<ast::BreakStmt>(&stmt)) {
    if (loopDepth_ == 0) {
      reportError(brk->getLocation(), "break used outside of a loop");
      return false;
    }
    return true;
  }
  if (auto* cont = llvm::dyn_cast<ast::ContinueStmt>(&stmt)) {
    if (loopDepth_ == 0) {
      reportError(cont->getLocation(), "continue used outside of a loop");
      return false;
    }
    return true;
  }
  if (auto* exprStmt = llvm::dyn_cast<ast::ExpressionStmt>(&stmt)) {
    return checkExpressionStmt(*exprStmt);
  }
  if (auto* block = llvm::dyn_cast<ast::BlockStmt>(&stmt)) {
    return checkBlock(*block);
  }

  // Other statements (break/continue) are structurally valid at parse-time.
  return true;
}

bool TypeChecker::checkDeclaration(ast::DeclarationStmt& stmt) {
  if (!stmt.initializer) {
    reportError(stmt.getLocation(), "Declarations require an initializer");
    return false;
  }
  auto initType = checkExpression(*stmt.initializer);
  TypePtr declaredType = stmt.typeName ? resolveTypeExpr(stmt.typeName.get()) : initType;
  if (!declaredType) {
    if (!stmt.typeName) {
      reportError(stmt.getLocation(), "Unable to resolve declared type");
    }
    return false;
  }
  if (!expectAssignable(stmt.initializer->getLocation(), declaredType, initType, "initializer")) {
    return false;
  }
  if (!scopes_.declare(stmt.name, declaredType)) {
    reportError(stmt.getLocation(), "Duplicate declaration of '" + stmt.name + "'");
    return false;
  }
  return true;
}

bool TypeChecker::checkAssignment(ast::AssignmentStmt& stmt) {
  auto targetType = scopes_.lookup(stmt.target);
  if (!targetType) {
    reportError(stmt.getLocation(), "Undeclared identifier '" + stmt.target + "'");
    return false;
  }
  auto valueType = checkExpression(*stmt.value);
  return expectAssignable(stmt.value->getLocation(), targetType, valueType, "assignment");
}

bool TypeChecker::checkIndexAssignment(ast::IndexAssignmentStmt& stmt) {
  auto targetType = scopes_.lookup(stmt.target);
  if (!targetType) {
    reportError(stmt.getLocation(), "Undeclared identifier '" + stmt.target + "'");
    return false;
  }
  if (targetType->kind != TypeKind::Array) {
    reportError(stmt.getLocation(), "Index assignment requires an array target");
    return false;
  }
  if (!stmt.index || !stmt.value) {
    reportError(stmt.getLocation(), "Index assignment requires index and value");
    return false;
  }

  auto indexType = checkExpression(*stmt.index);
  if (!indexType || (indexType->kind != TypeKind::Int && indexType->kind != TypeKind::Char &&
                     indexType->kind != TypeKind::Bool)) {
    reportError(stmt.index->getLocation(), "Array index must be an integer");
    return false;
  }

  const auto& array = std::get<ArrayTypeInfo>(targetType->payload);
  auto valueType = checkExpression(*stmt.value);
  stmt.setResolvedElementType(array.element);
  return expectAssignable(stmt.value->getLocation(), array.element, valueType, "index assignment");
}

bool TypeChecker::checkIf(ast::IfStmt& stmt) {
  auto condType = checkExpression(*stmt.condition);
  requireBoolean(*stmt.condition, condType);
  bool ok = true;
  if (stmt.thenBranch) {
    ok &= checkStatement(*stmt.thenBranch);
  }
  if (stmt.elseBranch) {
    ok &= checkStatement(*stmt.elseBranch);
  }
  return ok;
}

bool TypeChecker::checkWhile(ast::WhileStmt& stmt) {
  auto condType = checkExpression(*stmt.condition);
  requireBoolean(*stmt.condition, condType);
  loopDepth_++;
  bool ok = stmt.body ? checkStatement(*stmt.body) : true;
  loopDepth_--;
  return ok;
}

bool TypeChecker::checkFor(ast::ForStmt& stmt) {
  auto condType = checkExpression(*stmt.condition);
  requireBoolean(*stmt.condition, condType);
  loopDepth_++;
  bool ok = stmt.body ? checkStatement(*stmt.body) : true;
  loopDepth_--;
  return ok;
}

bool TypeChecker::checkReturn(ast::ReturnStmt& stmt) {
  TypePtr valueType = voidType();
  if (stmt.value) {
    valueType = checkExpression(*stmt.value);
  }
  if (!expectAssignable(stmt.getLocation(), currentReturnType_, valueType, "return")) {
    return false;
  }
  return true;
}

bool TypeChecker::checkExpressionStmt(ast::ExpressionStmt& stmt) {
  if (!stmt.expr) {
    return true;
  }
  (void)checkExpression(*stmt.expr);
  return true;
}

bool TypeChecker::statementAlwaysReturns(ast::Statement& stmt) {
  if (llvm::isa<ast::ReturnStmt>(stmt)) {
    return true;
  }
  if (auto* block = llvm::dyn_cast<ast::BlockStmt>(&stmt)) {
    return blockAlwaysReturns(*block);
  }
  if (auto* ifStmt = llvm::dyn_cast<ast::IfStmt>(&stmt)) {
    if (!ifStmt->thenBranch || !ifStmt->elseBranch) {
      return false;
    }
    return statementAlwaysReturns(*ifStmt->thenBranch) &&
           statementAlwaysReturns(*ifStmt->elseBranch);
  }
  return false;
}

bool TypeChecker::blockAlwaysReturns(ast::BlockStmt& block) {
  for (auto& stmt : block.statements) {
    if (statementAlwaysReturns(*stmt)) {
      return true;
    }
  }
  return false;
}

TypePtr TypeChecker::checkExpression(ast::Expression& expr) {
  if (auto* binary = llvm::dyn_cast<ast::BinaryExpr>(&expr)) {
    auto type = checkBinary(*binary);
    expr.setResolvedType(type);
    return type;
  }
  if (auto* unary = llvm::dyn_cast<ast::UnaryExpr>(&expr)) {
    auto type = checkUnary(*unary);
    expr.setResolvedType(type);
    return type;
  }
  if (auto* literal = llvm::dyn_cast<ast::LiteralExpr>(&expr)) {
    auto type = checkLiteral(*literal);
    expr.setResolvedType(type);
    return type;
  }
  if (auto* ident = llvm::dyn_cast<ast::IdentifierExpr>(&expr)) {
    auto type = checkIdentifier(*ident);
    expr.setResolvedType(type);
    return type;
  }
  if (auto* access = llvm::dyn_cast<ast::FieldAccessExpr>(&expr)) {
    auto type = checkFieldAccess(*access);
    expr.setResolvedType(type);
    return type;
  }
  if (auto* index = llvm::dyn_cast<ast::IndexExpr>(&expr)) {
    auto type = checkIndexAccess(*index);
    expr.setResolvedType(type);
    return type;
  }
  if (auto* call = llvm::dyn_cast<ast::CallExpr>(&expr)) {
    auto type = checkCall(*call);
    expr.setResolvedType(type);
    return type;
  }
  if (auto* lit = llvm::dyn_cast<ast::ArrayLiteralExpr>(&expr)) {
    auto type = checkArrayLiteral(*lit);
    expr.setResolvedType(type);
    return type;
  }
  if (auto* lit = llvm::dyn_cast<ast::StructLiteralExpr>(&expr)) {
    auto type = checkStructLiteral(*lit);
    expr.setResolvedType(type);
    return type;
  }
  return nullptr;
}

TypePtr TypeChecker::checkBinary(ast::BinaryExpr& expr) {
  auto lhsType = checkExpression(*expr.lhs);
  auto rhsType = checkExpression(*expr.rhs);
  if (!lhsType || !rhsType) {
    return nullptr;
  }
  if (expr.op == ast::BinaryOp::Add || expr.op == ast::BinaryOp::Sub ||
      expr.op == ast::BinaryOp::Mul || expr.op == ast::BinaryOp::Div) {
    auto resultType = arithmeticResultType(lhsType, rhsType);
    if (!resultType) {
      reportError(expr.getLocation(), "Arithmetic operands must be numeric");
      return nullptr;
    }
    return resultType;
  }
  if (expr.op == ast::BinaryOp::And || expr.op == ast::BinaryOp::Or) {
    requireBoolean(*expr.lhs, lhsType);
    requireBoolean(*expr.rhs, rhsType);
    return makePrimitive(TypeKind::Bool);
  }
  if (expr.op == ast::BinaryOp::Eq || expr.op == ast::BinaryOp::Ne ||
      expr.op == ast::BinaryOp::Lt || expr.op == ast::BinaryOp::Le ||
      expr.op == ast::BinaryOp::Gt || expr.op == ast::BinaryOp::Ge) {
    if (expr.op == ast::BinaryOp::Eq || expr.op == ast::BinaryOp::Ne) {
      auto numeric = arithmeticResultType(lhsType, rhsType);
      if (numeric) {
        return makePrimitive(TypeKind::Bool);
      }
      const bool lhsNull = lhsType && lhsType->kind == TypeKind::Null;
      const bool rhsNull = rhsType && rhsType->kind == TypeKind::Null;
      const bool nullPointerCompare = (lhsNull && isNullableType(rhsType)) ||
                                      (rhsNull && isNullableType(lhsType));
      if ((canImplicitlyConvert(lhsType, rhsType) && canImplicitlyConvert(rhsType, lhsType)) ||
          typeEquals(lhsType, rhsType) || nullPointerCompare) {
        return makePrimitive(TypeKind::Bool);
      }
      reportError(expr.getLocation(), "Operands of equality must be comparable");
      return nullptr;
    }
    auto numeric = arithmeticResultType(lhsType, rhsType);
    if (!numeric) {
      reportError(expr.getLocation(), "Comparison operands must be numeric");
      return nullptr;
    }
    return makePrimitive(TypeKind::Bool);
  }
  reportError(expr.getLocation(),
              "Unsupported binary operator '" + std::string(ast::binaryOpName(expr.op)) + "'");
  return nullptr;
}

TypePtr TypeChecker::checkUnary(ast::UnaryExpr& expr) {
  auto operandType = checkExpression(*expr.operand);
  if (expr.op == ast::UnaryOp::Negate) {
    return requireNumeric(expr, operandType);
  }
  if (expr.op == ast::UnaryOp::Not) {
    return requireBoolean(expr, operandType);
  }
  reportError(expr.getLocation(),
              "Unsupported unary operator '" + std::string(ast::unaryOpName(expr.op)) + "'");
  return nullptr;
}

TypePtr TypeChecker::checkLiteral(ast::LiteralExpr& expr) {
  const auto& value = expr.value;
  if (value == "true" || value == "false") {
    return makePrimitive(TypeKind::Bool);
  }
  if (value == "null") {
    return makePrimitive(TypeKind::Null);
  }
  if (!value.empty() && value.front() == '"') {
    return makePrimitive(TypeKind::String);
  }
  if (value.find('.') != std::string::npos) {
    return makePrimitive(TypeKind::Float);
  }
  return makePrimitive(TypeKind::Int);
}

TypePtr TypeChecker::checkIdentifier(ast::IdentifierExpr& expr) {
  auto type = scopes_.lookup(expr.name);
  if (!type) {
    reportError(expr.getLocation(), "Unknown identifier '" + expr.name + "'");
  }
  return type;
}

TypePtr TypeChecker::checkFieldAccess(ast::FieldAccessExpr& expr) {
  if (!expr.base) {
    reportError(expr.getLocation(), "Missing field access base");
    return nullptr;
  }
  auto baseType = checkExpression(*expr.base);
  if (!baseType) {
    reportError(expr.getLocation(), "Field access requires a struct value");
    return nullptr;
  }
  if (baseType->kind == TypeKind::Array) {
    if (expr.field == u8"طول") {
      return makePrimitive(TypeKind::Int);
    }
    reportError(expr.getLocation(), "Unknown field '" + expr.field + "' on array");
    return nullptr;
  }
  if (baseType->kind != TypeKind::Product) {
    reportError(expr.getLocation(), "Field access requires a struct value");
    return nullptr;
  }
  const auto& product = std::get<ProductTypeInfo>(baseType->payload);
  if (product.name.empty()) {
    reportError(expr.getLocation(), "Field access requires a named struct type");
    return nullptr;
  }
  auto it = structDecls_.find(product.name);
  if (it == structDecls_.end()) {
    reportError(expr.getLocation(), "Unknown struct type '" + product.name + "'");
    return nullptr;
  }
  const auto* decl = it->second;
  for (const auto& field : decl->fields) {
    if (field->name == expr.field) {
      auto type = resolveTypeExpr(field->type.get());
      if (!type) {
        reportError(expr.getLocation(), "Unknown type for field '" + expr.field + "'");
      }
      return type;
    }
  }
  reportError(expr.getLocation(), "Unknown field '" + expr.field + "' on struct '" +
                                      decl->name + "'");
  return nullptr;
}

TypePtr TypeChecker::checkIndexAccess(ast::IndexExpr& expr) {
  if (!expr.base || !expr.index) {
    reportError(expr.getLocation(), "Array index access requires base and index");
    return nullptr;
  }
  auto baseType = checkExpression(*expr.base);
  if (!baseType || baseType->kind != TypeKind::Array) {
    reportError(expr.getLocation(), "Index access requires an array value");
    return nullptr;
  }
  auto indexType = checkExpression(*expr.index);
  if (!indexType || (indexType->kind != TypeKind::Int && indexType->kind != TypeKind::Char &&
                     indexType->kind != TypeKind::Bool)) {
    reportError(expr.index->getLocation(), "Array index must be an integer");
    return nullptr;
  }
  const auto& info = std::get<ArrayTypeInfo>(baseType->payload);
  return info.element;
}

TypePtr TypeChecker::checkCall(ast::CallExpr& expr) {
  auto calleeType = scopes_.lookup(expr.callee);
  if (!calleeType || calleeType->kind != TypeKind::Function) {
    reportError(expr.getLocation(), "Attempting to call non-function '" + expr.callee + "'");
    return nullptr;
  }
  const auto& fnInfo = std::get<FunctionTypeInfo>(calleeType->payload);
  if (expr.args.size() != fnInfo.params.size()) {
    std::ostringstream os;
    os << "Function '" << expr.callee << "' expects " << fnInfo.params.size()
       << " arguments but got " << expr.args.size();
    reportError(expr.getLocation(), os.str());
  }
  const std::size_t paramCount = std::min(expr.args.size(), fnInfo.params.size());
  for (std::size_t i = 0; i < paramCount; ++i) {
    auto argType = checkExpression(*expr.args[i]);
    expectAssignable(expr.args[i]->getLocation(), fnInfo.params[i], argType, "argument");
  }
  return fnInfo.result;
}

TypePtr TypeChecker::checkArrayLiteral(ast::ArrayLiteralExpr& expr) {
  if (expr.elements.empty()) {
    reportError(expr.getLocation(), "Cannot infer type of empty array literal");
    return nullptr;
  }

  std::vector<TypePtr> elementTypes;
  elementTypes.reserve(expr.elements.size());
  for (const auto& element : expr.elements) {
    auto type = element ? checkExpression(*element) : nullptr;
    if (!type) {
      return nullptr;
    }
    elementTypes.push_back(std::move(type));
  }

  TypePtr elementType = elementTypes.front();
  for (std::size_t i = 1; i < elementTypes.size(); ++i) {
    const auto& next = elementTypes[i];
    if (typeEquals(elementType, next)) {
      continue;
    }
    if (auto promoted = arithmeticResultType(elementType, next)) {
      elementType = std::move(promoted);
      continue;
    }
    if (canImplicitlyConvert(next, elementType)) {
      continue;
    }
    if (canImplicitlyConvert(elementType, next)) {
      elementType = next;
      continue;
    }
    reportError(expr.elements[i]->getLocation(), "Array literal elements must share a type");
    return nullptr;
  }

  for (std::size_t i = 0; i < expr.elements.size(); ++i) {
    if (!expectAssignable(expr.elements[i]->getLocation(), elementType, elementTypes[i],
                          "array element")) {
      return nullptr;
    }
  }

  return makeArray(elementType);
}

TypePtr TypeChecker::checkStructLiteral(ast::StructLiteralExpr& expr) {
  auto it = structDecls_.find(expr.typeName);
  if (it == structDecls_.end()) {
    reportError(expr.getLocation(), "Unknown struct type '" + expr.typeName + "'");
    return nullptr;
  }
  const auto* decl = it->second;
  std::unordered_map<std::string, const ast::StructField*> fields;
  fields.reserve(decl->fields.size());
  for (const auto& field : decl->fields) {
    fields.emplace(field->name, field.get());
  }

  std::unordered_set<std::string> seen;
  for (const auto& init : expr.fields) {
    if (!init) continue;
    if (!seen.insert(init->name).second) {
      reportError(init->getLocation(),
                  "Duplicate field initializer '" + init->name + "' in struct literal");
      continue;
    }
    auto fit = fields.find(init->name);
    if (fit == fields.end()) {
      reportError(init->getLocation(),
                  "Unknown field '" + init->name + "' in struct '" + decl->name + "'");
      continue;
    }
    auto fieldType = resolveTypeExpr(fit->second->type.get());
    auto valueType = init->value ? checkExpression(*init->value) : nullptr;
    expectAssignable(init->getLocation(), fieldType, valueType, "field initializer");
  }

  for (const auto& field : decl->fields) {
    if (seen.find(field->name) == seen.end()) {
      reportError(expr.getLocation(),
                  "Missing field '" + field->name + "' in struct literal '" + decl->name + "'");
    }
  }

  return resolveNamedType(expr.typeName, expr.getLocation());
}

TypePtr TypeChecker::requireBoolean(ast::Expression& expr, TypePtr type) {
  if (!type || type->kind != TypeKind::Bool) {
    reportError(expr.getLocation(), "Boolean expression required");
    return nullptr;
  }
  return type;
}

TypePtr TypeChecker::requireNumeric(ast::Expression& expr, TypePtr type) {
  if (!type || !isArithmeticOperandKind(type->kind)) {
    reportError(expr.getLocation(), "Numeric expression required");
    return nullptr;
  }
  if (type->kind == TypeKind::Float) {
    return makePrimitive(TypeKind::Float);
  }
  return makePrimitive(TypeKind::Int);
}

bool TypeChecker::expectAssignable(const SourceLocation& loc, const TypePtr& target,
                                   const TypePtr& value, std::string_view context) {
  if (!target || !value) {
    return false;
  }
  if (canImplicitlyConvert(value, target)) {
    return true;
  }
  std::ostringstream os;
  os << "Type mismatch for " << context << ": expected " << static_cast<int>(target->kind)
     << " but got " << static_cast<int>(value->kind);
  reportError(loc, os.str());
  return false;
}

TypePtr TypeChecker::resolveTypeExpr(const ast::TypeExpr* type) {
  if (!type) {
    return nullptr;
  }
  if (auto* prim = llvm::dyn_cast<ast::TypePrimitiveExpr>(type)) {
    return makePrimitive(prim->kind);
  }
  if (auto* named = llvm::dyn_cast<ast::NamedTypeExpr>(type)) {
    return resolveNamedType(named->name, named->getLocation());
  }
  if (auto* array = llvm::dyn_cast<ast::TypeArrayExpr>(type)) {
    auto element = resolveTypeExpr(array->element.get());
    if (!element) {
      return nullptr;
    }
    return makeArray(std::move(element));
  }
  if (auto* sum = llvm::dyn_cast<ast::TypeSumExpr>(type)) {
    std::vector<TypePtr> variants;
    variants.reserve(sum->variants.size());
    for (const auto& variant : sum->variants) {
      auto resolved = resolveTypeExpr(variant.get());
      if (!resolved) {
        return nullptr;
      }
      variants.push_back(std::move(resolved));
    }
    return makeSum(std::move(variants));
  }
  if (auto* product = llvm::dyn_cast<ast::TypeProductExpr>(type)) {
    std::vector<TypePtr> members;
    members.reserve(product->members.size());
    for (const auto& member : product->members) {
      auto resolved = resolveTypeExpr(member.get());
      if (!resolved) {
        return nullptr;
      }
      members.push_back(std::move(resolved));
    }
    return makeProduct(std::move(members));
  }
  return nullptr;
}

void TypeChecker::registerTypeDecls(const ast::Program& program) {
  for (const auto& node : program.topLevel) {
    if (auto* decl = llvm::dyn_cast<ast::StructDecl>(node.get())) {
      if (structDecls_.find(decl->name) != structDecls_.end() ||
          enumDecls_.find(decl->name) != enumDecls_.end()) {
        reportError(decl->getLocation(), "Duplicate type name '" + decl->name + "'");
        continue;
      }
      structDecls_.emplace(decl->name, decl);
    } else if (auto* decl = llvm::dyn_cast<ast::EnumDecl>(node.get())) {
      if (structDecls_.find(decl->name) != structDecls_.end() ||
          enumDecls_.find(decl->name) != enumDecls_.end()) {
        reportError(decl->getLocation(), "Duplicate type name '" + decl->name + "'");
        continue;
      }
      enumDecls_.emplace(decl->name, decl);
    }
  }
}

TypePtr TypeChecker::resolveNamedType(std::string_view name, const SourceLocation& loc) {
  const std::string key(name);
  if (auto it = namedTypes_.find(key); it != namedTypes_.end()) {
    return it->second;
  }
  if (resolvingTypes_.find(key) != resolvingTypes_.end()) {
    reportError(loc, "Recursive type definition for '" + std::string(name) + "'");
    return nullptr;
  }

  const ast::StructDecl* structDecl = nullptr;
  const ast::EnumDecl* enumDecl = nullptr;
  if (auto it = structDecls_.find(key); it != structDecls_.end()) {
    structDecl = it->second;
  } else if (auto it = enumDecls_.find(key); it != enumDecls_.end()) {
    enumDecl = it->second;
  } else {
    reportError(loc, "Unknown type '" + std::string(name) + "'");
    return nullptr;
  }

  resolvingTypes_.insert(key);
  TypePtr result;
  if (structDecl) {
    std::unordered_set<std::string> seen;
    std::vector<TypePtr> members;
    members.reserve(structDecl->fields.size());
    for (const auto& field : structDecl->fields) {
      if (!seen.insert(field->name).second) {
        reportError(field->getLocation(), "Duplicate field '" + field->name + "' in struct '" +
                                            structDecl->name + "'");
        continue;
      }
      auto memberType = resolveTypeExpr(field->type.get());
      if (!memberType) {
        resolvingTypes_.erase(key);
        return nullptr;
      }
      members.push_back(std::move(memberType));
    }
    result = makeProduct(std::move(members), structDecl->name);
  } else if (enumDecl) {
    std::unordered_set<std::string> seen;
    std::vector<TypePtr> variants;
    variants.reserve(enumDecl->variants.size());
    for (const auto& variant : enumDecl->variants) {
      if (!seen.insert(variant->name).second) {
        reportError(variant->getLocation(), "Duplicate variant '" + variant->name +
                                               "' in enum '" + enumDecl->name + "'");
        continue;
      }
      TypePtr variantType;
      if (variant->payload) {
        variantType = resolveTypeExpr(variant->payload.get());
      } else {
        variantType = makePrimitive(TypeKind::Null);
      }
      if (!variantType) {
        resolvingTypes_.erase(key);
        return nullptr;
      }
      variants.push_back(std::move(variantType));
    }
    result = makeSum(std::move(variants), enumDecl->name);
  }

  if (result) {
    namedTypes_.emplace(key, result);
  }
  resolvingTypes_.erase(key);
  return result;
}

bool TypeChecker::typeEquals(const TypePtr& lhs, const TypePtr& rhs) const {
  if (lhs == rhs) {
    return true;
  }
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->kind != rhs->kind) {
    return false;
  }
  if (lhs->kind == TypeKind::Array) {
    const auto& la = std::get<ArrayTypeInfo>(lhs->payload);
    const auto& rb = std::get<ArrayTypeInfo>(rhs->payload);
    return typeEquals(la.element, rb.element);
  }
  if (lhs->kind == TypeKind::Sum) {
    const auto& la = std::get<SumTypeInfo>(lhs->payload);
    const auto& rb = std::get<SumTypeInfo>(rhs->payload);
    if (la.name != rb.name) {
      return false;
    }
    if (la.variants.size() != rb.variants.size()) {
      return false;
    }
    for (std::size_t i = 0; i < la.variants.size(); ++i) {
      if (!typeEquals(la.variants[i], rb.variants[i])) {
        return false;
      }
    }
    return true;
  }
  if (lhs->kind == TypeKind::Product) {
    const auto& la = std::get<ProductTypeInfo>(lhs->payload);
    const auto& rb = std::get<ProductTypeInfo>(rhs->payload);
    if (la.name != rb.name) {
      return false;
    }
    if (la.members.size() != rb.members.size()) {
      return false;
    }
    for (std::size_t i = 0; i < la.members.size(); ++i) {
      if (!typeEquals(la.members[i], rb.members[i])) {
        return false;
      }
    }
    return true;
  }
  if (lhs->kind == TypeKind::Function) {
    const auto& la = std::get<FunctionTypeInfo>(lhs->payload);
    const auto& rb = std::get<FunctionTypeInfo>(rhs->payload);
    if (la.params.size() != rb.params.size()) {
      return false;
    }
    for (std::size_t i = 0; i < la.params.size(); ++i) {
      if (!typeEquals(la.params[i], rb.params[i])) {
        return false;
      }
    }
    return typeEquals(la.result, rb.result);
  }
  return true;
}

bool TypeChecker::isNumericKind(TypeKind kind) const {
  return kind == TypeKind::Int || kind == TypeKind::Float;
}

bool TypeChecker::isArithmeticOperandKind(TypeKind kind) const {
  return kind == TypeKind::Int || kind == TypeKind::Float || kind == TypeKind::Char;
}

bool TypeChecker::isPointerLike(TypeKind kind) const {
  switch (kind) {
  case TypeKind::String:
  case TypeKind::Array:
  case TypeKind::Sum:
    return true;
  case TypeKind::Product:
    return true;
  default:
    return false;
  }
}

bool TypeChecker::isNullableType(const TypePtr& type) const {
  if (!type) {
    return false;
  }
  if (type->kind == TypeKind::Product) {
    const auto& product = std::get<ProductTypeInfo>(type->payload);
    return product.name.empty();
  }
  return isPointerLike(type->kind);
}

TypePtr TypeChecker::arithmeticResultType(const TypePtr& lhs, const TypePtr& rhs) const {
  if (!lhs || !rhs) {
    return nullptr;
  }
  if (!isArithmeticOperandKind(lhs->kind) || !isArithmeticOperandKind(rhs->kind)) {
    return nullptr;
  }
  if (lhs->kind == TypeKind::Float || rhs->kind == TypeKind::Float) {
    return makePrimitive(TypeKind::Float);
  }
  return makePrimitive(TypeKind::Int);
}

bool TypeChecker::canImplicitlyConvert(const TypePtr& from, const TypePtr& to) const {
  if (!from || !to) {
    return false;
  }
  if (typeEquals(from, to)) {
    return true;
  }
  if (from->kind == TypeKind::Bool && to->kind == TypeKind::Int) {
    return true;
  }
  if (from->kind == TypeKind::Char && to->kind == TypeKind::Int) {
    return true;
  }
  if ((from->kind == TypeKind::Char || from->kind == TypeKind::Int) &&
      to->kind == TypeKind::Float) {
    return true;
  }
  if (from->kind == TypeKind::Null && isNullableType(to)) {
    return true;
  }
  if (to->kind == TypeKind::Sum) {
    const auto& sum = std::get<SumTypeInfo>(to->payload);
    return std::any_of(sum.variants.begin(), sum.variants.end(),
                       [&](const TypePtr& variant) { return typeEquals(variant, from); });
  }
  return false;
}

std::string formatTypeError(const TypeError& error, std::string_view sourceName) {
  std::ostringstream os;
  bool hasPath = !sourceName.empty();
  if (hasPath) {
    os << sourceName;
  }
  if (error.location.line != 0 || error.location.column != 0) {
    if (hasPath) {
      os << ":";
    }
    os << error.location.line << ":" << error.location.column;
    os << ": ";
  } else if (hasPath) {
    os << ": ";
  }
  os << "error: " << error.message;
  return os.str();
}

} // namespace dhad::typing
