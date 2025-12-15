#pragma once

#include "../ast/ast.hpp"

#include "scope.hpp"
#include "types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace dhad::typing {

struct TypeError {
  SourceLocation location;
  std::string message;
};

struct TypeCheckerResult {
  bool success{false};
  std::vector<TypeError> errors;
};

class TypeChecker {
public:
  TypeChecker();

  TypeCheckerResult check(ast::Program& program);

private:
  ScopeStack scopes_;
  const std::string* currentFunction_{nullptr};
  TypePtr currentReturnType_;
  std::vector<TypeError> errors_;

  void reportError(const SourceLocation& loc, std::string message);

  bool declareGlobalFunction(ast::FunctionDecl& fn);
  bool checkTopLevel(ast::ASTNode& node);
  bool checkFunction(ast::FunctionDecl& fn);
  bool checkBlock(ast::BlockStmt& block);
  bool checkStatement(ast::Statement& stmt);
  bool checkDeclaration(ast::DeclarationStmt& stmt);
  bool checkAssignment(ast::AssignmentStmt& stmt);
  bool checkIf(ast::IfStmt& stmt);
  bool checkWhile(ast::WhileStmt& stmt);
  bool checkFor(ast::ForStmt& stmt);
  bool checkReturn(ast::ReturnStmt& stmt);
  bool checkExpressionStmt(ast::ExpressionStmt& stmt);

  TypePtr checkExpression(ast::Expression& expr);
  TypePtr checkBinary(ast::BinaryExpr& expr);
  TypePtr checkUnary(ast::UnaryExpr& expr);
  TypePtr checkLiteral(ast::LiteralExpr& expr);
  TypePtr checkIdentifier(ast::IdentifierExpr& expr);
  TypePtr checkCall(ast::CallExpr& expr);

  TypePtr requireBoolean(ast::Expression& expr, TypePtr type);
  TypePtr requireNumeric(ast::Expression& expr, TypePtr type);
  bool expectAssignable(const SourceLocation& loc, const TypePtr& target, const TypePtr& value,
                        std::string_view context);

  TypePtr resolveTypeName(const std::string& name);
  [[nodiscard]] bool typeEquals(const TypePtr& lhs, const TypePtr& rhs) const;
  [[nodiscard]] bool isNumericKind(TypeKind kind) const;
  [[nodiscard]] bool isArithmeticOperandKind(TypeKind kind) const;
  [[nodiscard]] bool isPointerLike(TypeKind kind) const;
  [[nodiscard]] bool canImplicitlyConvert(const TypePtr& from, const TypePtr& to) const;
  TypePtr arithmeticResultType(const TypePtr& lhs, const TypePtr& rhs) const;
};

std::string formatTypeError(const TypeError& error, std::string_view sourceName);

} // namespace dhad::typing
