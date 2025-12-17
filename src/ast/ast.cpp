#include "ast.hpp"

#include <llvm/Support/raw_ostream.h>

namespace dhad::ast {

namespace {

class DumpVisitor : public ASTVisitor {
public:
  DumpVisitor(llvm::raw_ostream& os, unsigned indent) : os(os), currentIndent(indent) {}

  void visit(const Program& node) override {
    indent();
    os << "Program\n";
    visitNodeVec(node.topLevel);
  }

  void visit(const ImportDecl& node) override {
    indent();
    os << "ImportDecl module=\"" << node.module << "\"\n";
  }

  void visit(const FunctionDecl& node) override {
    indent();
    os << "FunctionDecl " << node.name;
    if (node.returnType) os << " -> " << *node.returnType;
    os << '\n';
    visitNodeVec(node.params, "Parameters");
    visitNode(node.body, "Body");
  }

  void visit(const Parameter& node) override {
    indent();
    os << "Parameter " << node.name;
    if (!node.typeName.empty()) os << ": " << node.typeName;
    os << '\n';
  }

  void visit(const BlockStmt& node) override {
    indent();
    os << "BlockStmt\n";
    visitNodeVec(node.statements);
  }

  void visit(const DeclarationStmt& node) override {
    indent();
    os << (node.isConst ? "ConstDecl " : "VarDecl ") << node.name;
    if (node.typeName) os << ": " << *node.typeName;
    os << '\n';
    visitNode(node.initializer, "Initializer");
  }

  void visit(const AssignmentStmt& node) override {
    indent();
    os << "Assignment target=" << node.target << '\n';
    visitNode(node.value, "Value");
  }

  void visit(const IfStmt& node) override {
    indent();
    os << "IfStmt\n";
    visitNode(node.condition, "Condition");
    visitNode(node.thenBranch, "Then");
    visitNode(node.elseBranch, "Else");
  }

  void visit(const WhileStmt& node) override {
    indent();
    os << "WhileStmt\n";
    visitNode(node.condition, "Condition");
    visitNode(node.body, "Body");
  }

  void visit(const ForStmt& node) override {
    indent();
    os << "ForStmt\n";
    visitNode(node.condition, "Condition");
    visitNode(node.body, "Body");
  }

  void visit(const ReturnStmt& node) override {
    indent();
    os << "ReturnStmt\n";
    visitNode(node.value, "Value");
  }

  void visit(const BreakStmt&) override {
    indent();
    os << "BreakStmt\n";
  }

  void visit(const ContinueStmt&) override {
    indent();
    os << "ContinueStmt\n";
  }

  void visit(const ExpressionStmt& node) override {
    indent();
    os << "ExpressionStmt\n";
    visitNode(node.expr, "Value");
  }

  void visit(const BinaryExpr& node) override {
    indent();
    os << "BinaryExpr op=\"" << node.op << "\"\n";
    visitNode(node.lhs, "LHS");
    visitNode(node.rhs, "RHS");
  }

  void visit(const UnaryExpr& node) override {
    indent();
    os << "UnaryExpr op=\"" << node.op << "\"\n";
    visitNode(node.operand, "Operand");
  }

  void visit(const LiteralExpr& node) override {
    indent();
    os << "Literal \"" << node.value << "\"\n";
  }

  void visit(const IdentifierExpr& node) override {
    indent();
    os << "Identifier " << node.name << '\n';
  }

  void visit(const CallExpr& node) override {
    indent();
    os << "CallExpr callee=" << node.callee << '\n';
    visitNodeVec(node.args, "Args");
  }

  void visit(const ArrayLiteralExpr& node) override {
    indent();
    os << "ArrayLiteral\n";
    visitNodeVec(node.elements, "Elements");
  }

private:
  llvm::raw_ostream& os;
  unsigned currentIndent;

  void indent() { os.indent(currentIndent); }

  struct IndentGuard {
    DumpVisitor& visitor;
    explicit IndentGuard(DumpVisitor& v) : visitor(v) { visitor.currentIndent += 2; }
    ~IndentGuard() { visitor.currentIndent -= 2; }
  };

  template <typename PtrT> void visitNode(const PtrT& node, const char* label = nullptr) {
    if (!node) return;
    if (label) {
      indent();
      os << label << ":\n";
    }
    IndentGuard guard(*this);
    node->accept(*this);
  }

  template <typename VecT> void visitNodeVec(const VecT& nodes, const char* label = nullptr) {
    if (nodes.empty()) return;
    if (label) {
      indent();
      os << label << ":\n";
    }
    IndentGuard guard(*this);
    for (const auto& child : nodes) {
      if (child) child->accept(*this);
    }
  }
};

} // namespace

ASTNode::ASTNode(Kind kind, SourceLocation loc) : kind(kind), location(loc) {}

ASTNode::~ASTNode() = default;

const char* ASTNode::kindName(Kind kind) {
  switch (kind) {
  case Kind::Program:
    return "Program";
  case Kind::ImportDecl:
    return "ImportDecl";
  case Kind::FunctionDecl:
    return "FunctionDecl";
  case Kind::Parameter:
    return "Parameter";
  case Kind::BlockStmt:
    return "BlockStmt";
  case Kind::StatementList:
    return "StatementList";
  case Kind::DeclarationStmt:
    return "DeclarationStmt";
  case Kind::AssignmentStmt:
    return "AssignmentStmt";
  case Kind::FlowStmt:
    return "FlowStmt";
  case Kind::IfStmt:
    return "IfStmt";
  case Kind::WhileStmt:
    return "WhileStmt";
  case Kind::ForStmt:
    return "ForStmt";
  case Kind::ReturnStmt:
    return "ReturnStmt";
  case Kind::BreakStmt:
    return "BreakStmt";
  case Kind::ContinueStmt:
    return "ContinueStmt";
  case Kind::ExpressionStmt:
    return "ExpressionStmt";
  case Kind::Expression:
    return "Expression";
  case Kind::BinaryExpr:
    return "BinaryExpr";
  case Kind::UnaryExpr:
    return "UnaryExpr";
  case Kind::LiteralExpr:
    return "LiteralExpr";
  case Kind::IdentifierExpr:
    return "IdentifierExpr";
  case Kind::CallExpr:
    return "CallExpr";
  case Kind::ArrayLiteralExpr:
    return "ArrayLiteralExpr";
  }
  return "Unknown";
}

void ASTNode::dump(llvm::raw_ostream& os, unsigned indent) const {
  DumpVisitor visitor(os, indent);
  accept(visitor);
}

void ASTNode::dump() const {
  DumpVisitor visitor(llvm::errs(), 0);
  accept(visitor);
}

Program::Program() : NodeWithKind<Program, ASTNode::Kind::Program>(SourceLocation{0, 0}) {}

ImportDecl::ImportDecl(std::string moduleName, SourceLocation loc)
    : NodeWithKind<ImportDecl, ASTNode::Kind::ImportDecl>(loc), module(std::move(moduleName)) {}

Parameter::Parameter(std::string name, std::string typeName, SourceLocation loc)
    : NodeWithKind<Parameter, ASTNode::Kind::Parameter>(loc), name(std::move(name)),
      typeName(std::move(typeName)) {}

BlockStmt::BlockStmt(SourceLocation loc)
    : NodeWithKind<BlockStmt, ASTNode::Kind::BlockStmt, Statement>(loc) {}

FunctionDecl::FunctionDecl(std::string fnName, SourceLocation loc)
    : NodeWithKind<FunctionDecl, ASTNode::Kind::FunctionDecl>(loc), name(std::move(fnName)) {}

DeclarationStmt::DeclarationStmt(bool isConst, std::string identifier, SourceLocation loc)
    : NodeWithKind<DeclarationStmt, ASTNode::Kind::DeclarationStmt, Statement>(loc),
      isConst(isConst), name(std::move(identifier)) {}

AssignmentStmt::AssignmentStmt(std::string identifier, SourceLocation loc)
    : NodeWithKind<AssignmentStmt, ASTNode::Kind::AssignmentStmt, Statement>(loc),
      target(std::move(identifier)) {}

IfStmt::IfStmt(SourceLocation loc) : NodeWithKind<IfStmt, ASTNode::Kind::IfStmt, FlowStmt>(loc) {}

WhileStmt::WhileStmt(SourceLocation loc)
    : NodeWithKind<WhileStmt, ASTNode::Kind::WhileStmt, FlowStmt>(loc) {}

ForStmt::ForStmt(SourceLocation loc)
    : NodeWithKind<ForStmt, ASTNode::Kind::ForStmt, FlowStmt>(loc) {}

ReturnStmt::ReturnStmt(SourceLocation loc)
    : NodeWithKind<ReturnStmt, ASTNode::Kind::ReturnStmt, FlowStmt>(loc) {}

BreakStmt::BreakStmt(SourceLocation loc)
    : NodeWithKind<BreakStmt, ASTNode::Kind::BreakStmt, FlowStmt>(loc) {}

ContinueStmt::ContinueStmt(SourceLocation loc)
    : NodeWithKind<ContinueStmt, ASTNode::Kind::ContinueStmt, FlowStmt>(loc) {}

ExpressionStmt::ExpressionStmt(SourceLocation loc)
    : NodeWithKind<ExpressionStmt, ASTNode::Kind::ExpressionStmt, Statement>(loc) {}

BinaryExpr::BinaryExpr(std::string op, SourceLocation loc)
    : NodeWithKind<BinaryExpr, ASTNode::Kind::BinaryExpr, Expression>(loc), op(std::move(op)) {}

UnaryExpr::UnaryExpr(std::string op, SourceLocation loc)
    : NodeWithKind<UnaryExpr, ASTNode::Kind::UnaryExpr, Expression>(loc), op(std::move(op)) {}

LiteralExpr::LiteralExpr(std::string literal, SourceLocation loc)
    : NodeWithKind<LiteralExpr, ASTNode::Kind::LiteralExpr, Expression>(loc),
      value(std::move(literal)) {}

IdentifierExpr::IdentifierExpr(std::string identifier, SourceLocation loc)
    : NodeWithKind<IdentifierExpr, ASTNode::Kind::IdentifierExpr, Expression>(loc),
      name(std::move(identifier)) {}

CallExpr::CallExpr(std::string callee, SourceLocation loc)
    : NodeWithKind<CallExpr, ASTNode::Kind::CallExpr, Expression>(loc), callee(std::move(callee)) {}

ArrayLiteralExpr::ArrayLiteralExpr(SourceLocation loc)
    : NodeWithKind<ArrayLiteralExpr, ASTNode::Kind::ArrayLiteralExpr, Expression>(loc) {}

} // namespace dhad::ast
