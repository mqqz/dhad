#pragma once

#include "../lexer/tokens.hpp"
#include "../typing/types.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <llvm/Support/Casting.h>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace dhad::ast {

#define DHAD_AST_FOR_EACH_CONCRETE_NODE(M)                                                         \
  M(Program)                                                                                       \
  M(ImportDecl)                                                                                    \
  M(FunctionDecl)                                                                                  \
  M(Parameter)                                                                                     \
  M(BlockStmt)                                                                                     \
  M(DeclarationStmt)                                                                               \
  M(AssignmentStmt)                                                                                \
  M(IfStmt)                                                                                        \
  M(WhileStmt)                                                                                     \
  M(ForStmt)                                                                                       \
  M(ReturnStmt)                                                                                    \
  M(BreakStmt)                                                                                     \
  M(ContinueStmt)                                                                                  \
  M(ExpressionStmt)                                                                                \
  M(BinaryExpr)                                                                                    \
  M(UnaryExpr)                                                                                     \
  M(LiteralExpr)                                                                                   \
  M(IdentifierExpr)                                                                                \
  M(CallExpr)                                                                                      \
  M(ArrayLiteralExpr)

#define DHAD_AST_FWD_DECL(NodeName) struct NodeName;
DHAD_AST_FOR_EACH_CONCRETE_NODE(DHAD_AST_FWD_DECL)
#undef DHAD_AST_FWD_DECL

class ASTVisitor {
public:
  virtual ~ASTVisitor() = default;
#define DHAD_AST_VISITOR_METHOD(NodeName)                                                          \
  virtual void visit(const NodeName&) {}
  DHAD_AST_FOR_EACH_CONCRETE_NODE(DHAD_AST_VISITOR_METHOD)
#undef DHAD_AST_VISITOR_METHOD
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
    CallExpr,
    ArrayLiteralExpr
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

// Provides classof/accept + kind-safe construction for derived nodes.
template <typename Derived, ASTNode::Kind NodeKind, typename BaseT = ASTNode>
struct NodeWithKind : BaseT {
protected:
private:
  explicit NodeWithKind(SourceLocation loc) : BaseT(NodeKind, loc) {}

protected:
private:
  NodeWithKind() : BaseT(NodeKind, SourceLocation{0, 0}) {}

protected:
public:
  static bool classof(const ASTNode* node) { return node->getKind() == NodeKind; }
  void accept(ASTVisitor& visitor) const override {
    visitor.visit(static_cast<const Derived&>(*this));
  }
  friend Derived;
};

struct Statement : ASTNode {
  static bool classof(const ASTNode* node) {
    switch (node->getKind()) {
    case Kind::Program:
    case Kind::ImportDecl:
    case Kind::FunctionDecl:
    case Kind::Parameter:
      return false;
    default:
      return true;
    }
  }

protected:
  Statement(Kind kind, SourceLocation loc) : ASTNode(kind, loc) {}
};

struct Expression : Statement {
  static bool classof(const ASTNode* node) {
    switch (node->getKind()) {
    case Kind::Expression:
    case Kind::BinaryExpr:
    case Kind::UnaryExpr:
    case Kind::LiteralExpr:
    case Kind::IdentifierExpr:
    case Kind::CallExpr:
    case Kind::ArrayLiteralExpr:
      return true;
    default:
      return false;
    }
  }

  [[nodiscard]] typing::TypePtr resolvedType() const { return type_; }
  void setResolvedType(typing::TypePtr type) { type_ = std::move(type); }

protected:
  Expression(Kind kind, SourceLocation loc) : Statement(kind, loc) {}

private:
  typing::TypePtr type_;
};

struct Program : NodeWithKind<Program, ASTNode::Kind::Program> {
  std::vector<NodePtr<ASTNode>> topLevel;

  Program();
};

struct ImportDecl : NodeWithKind<ImportDecl, ASTNode::Kind::ImportDecl> {
  std::string module;

  explicit ImportDecl(std::string moduleName, SourceLocation loc);
};

struct Parameter : NodeWithKind<Parameter, ASTNode::Kind::Parameter> {
  std::string name;
  std::string typeName;

  Parameter(std::string name, std::string typeName, SourceLocation loc);
};

struct BlockStmt : NodeWithKind<BlockStmt, ASTNode::Kind::BlockStmt, Statement> {
  std::vector<NodePtr<Statement>> statements;

  explicit BlockStmt(SourceLocation loc);
};

struct FunctionDecl : NodeWithKind<FunctionDecl, ASTNode::Kind::FunctionDecl> {
  std::string name;
  std::vector<NodePtr<Parameter>> params;
  std::optional<std::string> returnType;
  NodePtr<BlockStmt> body;

  FunctionDecl(std::string fnName, SourceLocation loc);
};

struct DeclarationStmt : NodeWithKind<DeclarationStmt, ASTNode::Kind::DeclarationStmt, Statement> {
  bool isConst{false};
  std::string name;
  std::optional<std::string> typeName;
  NodePtr<Expression> initializer;

  DeclarationStmt(bool isConst, std::string identifier, SourceLocation loc);
};

struct AssignmentStmt : NodeWithKind<AssignmentStmt, ASTNode::Kind::AssignmentStmt, Statement> {
  std::string target;
  NodePtr<Expression> value;

  AssignmentStmt(std::string identifier, SourceLocation loc);
};

struct FlowStmt : Statement {
protected:
  FlowStmt(Kind kind, SourceLocation loc) : Statement(kind, loc) {}
};

struct IfStmt : NodeWithKind<IfStmt, ASTNode::Kind::IfStmt, FlowStmt> {
  NodePtr<Expression> condition;
  NodePtr<Statement> thenBranch;
  NodePtr<Statement> elseBranch;

  explicit IfStmt(SourceLocation loc);
};

struct WhileStmt : NodeWithKind<WhileStmt, ASTNode::Kind::WhileStmt, FlowStmt> {
  NodePtr<Expression> condition;
  NodePtr<Statement> body;

  explicit WhileStmt(SourceLocation loc);
};

struct ForStmt : NodeWithKind<ForStmt, ASTNode::Kind::ForStmt, FlowStmt> {
  NodePtr<Expression> condition;
  NodePtr<Statement> body;

  explicit ForStmt(SourceLocation loc);
};

struct ReturnStmt : NodeWithKind<ReturnStmt, ASTNode::Kind::ReturnStmt, FlowStmt> {
  NodePtr<Expression> value;

  explicit ReturnStmt(SourceLocation loc);
};

struct BreakStmt : NodeWithKind<BreakStmt, ASTNode::Kind::BreakStmt, FlowStmt> {
  explicit BreakStmt(SourceLocation loc);
};

struct ContinueStmt : NodeWithKind<ContinueStmt, ASTNode::Kind::ContinueStmt, FlowStmt> {
  explicit ContinueStmt(SourceLocation loc);
};

struct ExpressionStmt : NodeWithKind<ExpressionStmt, ASTNode::Kind::ExpressionStmt, Statement> {
  NodePtr<Expression> expr;

  explicit ExpressionStmt(SourceLocation loc);
};

struct BinaryExpr : NodeWithKind<BinaryExpr, ASTNode::Kind::BinaryExpr, Expression> {
  std::string op;
  NodePtr<Expression> lhs;
  NodePtr<Expression> rhs;

  BinaryExpr(std::string op, SourceLocation loc);
};

struct UnaryExpr : NodeWithKind<UnaryExpr, ASTNode::Kind::UnaryExpr, Expression> {
  std::string op;
  NodePtr<Expression> operand;

  UnaryExpr(std::string op, SourceLocation loc);
};

struct LiteralExpr : NodeWithKind<LiteralExpr, ASTNode::Kind::LiteralExpr, Expression> {
  std::string value;

  LiteralExpr(std::string literal, SourceLocation loc);
};

struct IdentifierExpr : NodeWithKind<IdentifierExpr, ASTNode::Kind::IdentifierExpr, Expression> {
  std::string name;

  IdentifierExpr(std::string identifier, SourceLocation loc);
};

struct CallExpr : NodeWithKind<CallExpr, ASTNode::Kind::CallExpr, Expression> {
  std::string callee;
  std::vector<NodePtr<Expression>> args;

  CallExpr(std::string callee, SourceLocation loc);
};

struct ArrayLiteralExpr
    : NodeWithKind<ArrayLiteralExpr, ASTNode::Kind::ArrayLiteralExpr, Expression> {
  std::vector<NodePtr<Expression>> elements;

  explicit ArrayLiteralExpr(SourceLocation loc);
};

} // namespace dhad::ast
