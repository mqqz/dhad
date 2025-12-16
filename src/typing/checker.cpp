#include "typing/checker.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>

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
    auto type = resolveTypeName(param->typeName);
    if (!type) {
      std::ostringstream os;
      os << "Unknown parameter type '" << param->typeName << "' in function '" << fn.name << "'";
      reportError(param->getLocation(), os.str());
      return false;
    }
    params.push_back(std::move(type));
  }

  TypePtr returnType = fn.returnType ? resolveTypeName(*fn.returnType) : voidType();
  if (!returnType) {
    std::ostringstream os;
    os << "Unknown return type '" << *fn.returnType << "' in function '" << fn.name << "'";
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
  currentReturnType_ = fn.returnType ? resolveTypeName(*fn.returnType) : voidType();
  if (!currentReturnType_) {
    reportError(fn.getLocation(), "Unknown return type");
    currentReturnType_ = voidType();
  }

  scopes_.enterScope();
  for (const auto& param : fn.params) {
    auto type = resolveTypeName(param->typeName);
    if (!type) {
      reportError(param->getLocation(), "Unknown parameter type '" + param->typeName + "'");
      continue;
    }
    if (!scopes_.declare(param->name, type)) {
      reportError(param->getLocation(), "Duplicate parameter '" + param->name + "'");
    }
  }

  bool ok = fn.body ? checkBlock(*fn.body) : true;
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
  TypePtr declaredType = stmt.typeName ? resolveTypeName(*stmt.typeName) : initType;
  if (!declaredType) {
    reportError(stmt.getLocation(), "Unable to resolve declared type");
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
  return stmt.body ? checkStatement(*stmt.body) : true;
}

bool TypeChecker::checkFor(ast::ForStmt& stmt) {
  auto condType = checkExpression(*stmt.condition);
  requireBoolean(*stmt.condition, condType);
  return stmt.body ? checkStatement(*stmt.body) : true;
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
  if (auto* call = llvm::dyn_cast<ast::CallExpr>(&expr)) {
    auto type = checkCall(*call);
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
  const auto& op = expr.op;
  if (op == "+" || op == "-" || op == "*" || op == "/") {
    auto resultType = arithmeticResultType(lhsType, rhsType);
    if (!resultType) {
      reportError(expr.getLocation(), "Arithmetic operands must be numeric");
      return nullptr;
    }
    return resultType;
  }
  if (op == "and" || op == "or") {
    requireBoolean(*expr.lhs, lhsType);
    requireBoolean(*expr.rhs, rhsType);
    return makePrimitive(TypeKind::Bool);
  }
  if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
    if (op == "==" || op == "!=") {
      auto numeric = arithmeticResultType(lhsType, rhsType);
      if (numeric) {
        return makePrimitive(TypeKind::Bool);
      }
      const bool lhsNull = lhsType && lhsType->kind == TypeKind::Null;
      const bool rhsNull = rhsType && rhsType->kind == TypeKind::Null;
      const bool nullPointerCompare =
          (lhsNull && rhsType && isPointerLike(rhsType->kind)) ||
          (rhsNull && lhsType && isPointerLike(lhsType->kind));
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
  reportError(expr.getLocation(), "Unsupported binary operator '" + op + "'");
  return nullptr;
}

TypePtr TypeChecker::checkUnary(ast::UnaryExpr& expr) {
  auto operandType = checkExpression(*expr.operand);
  if (expr.op == "-") {
    return requireNumeric(expr, operandType);
  }
  if (expr.op == "not") {
    return requireBoolean(expr, operandType);
  }
  reportError(expr.getLocation(), "Unsupported unary operator '" + expr.op + "'");
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

TypePtr TypeChecker::resolveTypeName(const std::string& name) {
  static const std::unordered_map<std::string, TypeKind> mapping = {
      {"int", TypeKind::Int},   {"float", TypeKind::Float},   {"bool", TypeKind::Bool},
      {"char", TypeKind::Char}, {"string", TypeKind::String}, {"null", TypeKind::Null},
  };
  auto it = mapping.find(name);
  if (it == mapping.end()) {
    return nullptr;
  }
  return makePrimitive(it->second);
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
  case TypeKind::Product:
    return true;
  default:
    return false;
  }
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
  if (from->kind == TypeKind::Null && isPointerLike(to->kind)) {
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
