#pragma once

#include "../lexer/tokens.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <llvm/Support/Casting.h>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace dhad::ast {

struct Program;
struct ImportDecl;
struct FunctionDecl;
struct Parameter;
struct BlockStmt;
struct DeclarationStmt;
struct AssignmentStmt;
struct IfStmt;
struct WhileStmt;
struct ForStmt;
struct ReturnStmt;
struct BreakStmt;
struct ContinueStmt;
struct ExpressionStmt;
struct BinaryExpr;
struct UnaryExpr;
struct LiteralExpr;
struct IdentifierExpr;
struct CallExpr;

class ASTVisitor {
public:
  virtual ~ASTVisitor() = default;
  virtual void visit(const Program&) {}
  virtual void visit(const ImportDecl&) {}
  virtual void visit(const FunctionDecl&) {}
  virtual void visit(const Parameter&) {}
  virtual void visit(const BlockStmt&) {}
  virtual void visit(const DeclarationStmt&) {}
  virtual void visit(const AssignmentStmt&) {}
  virtual void visit(const IfStmt&) {}
  virtual void visit(const WhileStmt&) {}
  virtual void visit(const ForStmt&) {}
  virtual void visit(const ReturnStmt&) {}
  virtual void visit(const BreakStmt&) {}
  virtual void visit(const ContinueStmt&) {}
  virtual void visit(const ExpressionStmt&) {}
  virtual void visit(const BinaryExpr&) {}
  virtual void visit(const UnaryExpr&) {}
  virtual void visit(const LiteralExpr&) {}
  virtual void visit(const IdentifierExpr&) {}
  virtual void visit(const CallExpr&) {}
};

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

class ASTNode {
public:
  enum class Kind {
    Program,
    ImportDecl,
    FunctionDecl,
    Parameter,
    BlockStmt,
    StatementList,
    DeclarationStmt,
    AssignmentStmt,
    FlowStmt,
    IfStmt,
    WhileStmt,
    ForStmt,
    ReturnStmt,
    BreakStmt,
    ContinueStmt,
    ExpressionStmt,
    Expression,
    BinaryExpr,
    UnaryExpr,
    LiteralExpr,
    IdentifierExpr,
    CallExpr
  };

  virtual ~ASTNode();

  [[nodiscard]] Kind getKind() const { return kind; }
  [[nodiscard]] const SourceLocation& getLocation() const { return location; }
  [[nodiscard]] const char* kindName() const { return kindName(kind); }
  static const char* kindName(Kind kind);
  void dump(llvm::raw_ostream& os, unsigned indent = 0) const;
  void dump() const;
  virtual void accept(ASTVisitor& visitor) const = 0;

protected:
  ASTNode(Kind kind, SourceLocation loc);

private:
  Kind kind;
  SourceLocation location;
};

template <typename T> using NodePtr = std::unique_ptr<T>;

struct Statement : ASTNode {
protected:
  Statement(Kind kind, SourceLocation loc) : ASTNode(kind, loc) {}
};

struct Expression : Statement {
protected:
  Expression(Kind kind, SourceLocation loc) : Statement(kind, loc) {}
};

struct Program : ASTNode {
  std::vector<NodePtr<ASTNode>> topLevel;

  Program();
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::Program; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ImportDecl : ASTNode {
  std::string module;

  explicit ImportDecl(std::string moduleName, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::ImportDecl; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Parameter : ASTNode {
  std::string name;
  std::string typeName;

  Parameter(std::string name, std::string typeName, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::Parameter; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct BlockStmt : Statement {
  std::vector<NodePtr<Statement>> statements;

  explicit BlockStmt(SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::BlockStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct FunctionDecl : ASTNode {
  std::string name;
  std::vector<NodePtr<Parameter>> params;
  std::optional<std::string> returnType;
  NodePtr<BlockStmt> body;

  FunctionDecl(std::string fnName, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::FunctionDecl; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct DeclarationStmt : Statement {
  bool isConst{false};
  std::string name;
  std::optional<std::string> typeName;
  NodePtr<Expression> initializer;

  DeclarationStmt(bool isConst, std::string identifier, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::DeclarationStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct AssignmentStmt : Statement {
  std::string target;
  NodePtr<Expression> value;

  AssignmentStmt(std::string identifier, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::AssignmentStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct FlowStmt : Statement {
protected:
  FlowStmt(Kind kind, SourceLocation loc) : Statement(kind, loc) {}
};

struct IfStmt : FlowStmt {
  NodePtr<Expression> condition;
  NodePtr<Statement> thenBranch;
  NodePtr<Statement> elseBranch;

  explicit IfStmt(SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::IfStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct WhileStmt : FlowStmt {
  NodePtr<Expression> condition;
  NodePtr<Statement> body;

  explicit WhileStmt(SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::WhileStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ForStmt : FlowStmt {
  NodePtr<Expression> condition;
  NodePtr<Statement> body;

  explicit ForStmt(SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::ForStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ReturnStmt : FlowStmt {
  NodePtr<Expression> value;

  explicit ReturnStmt(SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::ReturnStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct BreakStmt : FlowStmt {
  explicit BreakStmt(SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::BreakStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ContinueStmt : FlowStmt {
  explicit ContinueStmt(SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::ContinueStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ExpressionStmt : Statement {
  NodePtr<Expression> expr;

  explicit ExpressionStmt(SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::ExpressionStmt; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct BinaryExpr : Expression {
  std::string op;
  NodePtr<Expression> lhs;
  NodePtr<Expression> rhs;

  BinaryExpr(std::string op, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::BinaryExpr; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct UnaryExpr : Expression {
  std::string op;
  NodePtr<Expression> operand;

  UnaryExpr(std::string op, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::UnaryExpr; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct LiteralExpr : Expression {
  std::string value;

  LiteralExpr(std::string literal, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::LiteralExpr; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct IdentifierExpr : Expression {
  std::string name;

  IdentifierExpr(std::string identifier, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::IdentifierExpr; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct CallExpr : Expression {
  std::string callee;
  std::vector<NodePtr<Expression>> args;

  CallExpr(std::string callee, SourceLocation loc);
  static bool classof(const ASTNode* node) { return node->getKind() == Kind::CallExpr; }
  void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

} // namespace dhad::ast
