#include "ast.hpp"

#include <iostream>

namespace dhad::ast {

namespace {

const char* typeKindName(typing::TypeKind kind) {
  switch (kind) {
  case typing::TypeKind::Int:
    return "int";
  case typing::TypeKind::Float:
    return "float";
  case typing::TypeKind::Bool:
    return "bool";
  case typing::TypeKind::Char:
    return "char";
  case typing::TypeKind::String:
    return "string";
  case typing::TypeKind::Null:
    return "null";
  case typing::TypeKind::Array:
    return "array";
  case typing::TypeKind::Sum:
    return "sum";
  case typing::TypeKind::Product:
    return "product";
  case typing::TypeKind::Function:
    return "function";
  }
  return "unknown";
}

const char* binaryOpDisplayName(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:
    return "+";
  case BinaryOp::Sub:
    return "-";
  case BinaryOp::Mul:
    return "*";
  case BinaryOp::Div:
    return "/";
  case BinaryOp::And:
    return "and";
  case BinaryOp::Or:
    return "or";
  case BinaryOp::Eq:
    return "==";
  case BinaryOp::Ne:
    return "!=";
  case BinaryOp::Lt:
    return "<";
  case BinaryOp::Le:
    return "<=";
  case BinaryOp::Gt:
    return ">";
  case BinaryOp::Ge:
    return ">=";
  }
  return "?";
}

const char* unaryOpDisplayName(UnaryOp op) {
  switch (op) {
  case UnaryOp::Negate:
    return "-";
  case UnaryOp::Not:
    return "not";
  }
  return "?";
}

class DumpVisitor : public ASTVisitor {
public:
  DumpVisitor(std::ostream& os, unsigned indent) : os(os), currentIndent(indent) {}

  void visit(const Program& node) override {
    indent();
    os << "Program\n";
    visitNodeVec(node.topLevel);
  }

  void visit(const ImportDecl& node) override {
    indent();
    os << "ImportDecl module=\"" << node.module << "\"\n";
  }

  void visit(const StructDecl& node) override {
    indent();
    os << "StructDecl " << node.name << '\n';
    visitNodeVec(node.fields, "Fields");
  }

  void visit(const EnumDecl& node) override {
    indent();
    os << "EnumDecl " << node.name << '\n';
    visitNodeVec(node.variants, "Variants");
  }

  void visit(const StructField& node) override {
    indent();
    os << "StructField " << node.name << '\n';
    if (node.type) {
      indent();
      os << "Type:\n";
      IndentGuard guard(*this);
      node.type->accept(*this);
    }
  }

  void visit(const EnumVariant& node) override {
    indent();
    os << "EnumVariant " << node.name << '\n';
    if (node.payload) {
      indent();
      os << "Payload:\n";
      IndentGuard guard(*this);
      node.payload->accept(*this);
    }
  }

  void visit(const FunctionDecl& node) override {
    indent();
    os << "FunctionDecl " << node.name;
    os << '\n';
    if (node.returnType) {
      indent();
      os << "ReturnType:\n";
      {
        IndentGuard guard(*this);
        node.returnType->accept(*this);
      }
    }
    visitNodeVec(node.params, "Parameters");
    visitNode(node.body, "Body");
  }

  void visit(const Parameter& node) override {
    indent();
    os << "Parameter " << node.name << '\n';
    if (node.type) {
      indent();
      os << "Type:\n";
      IndentGuard guard(*this);
      node.type->accept(*this);
    }
  }

  void visit(const BlockStmt& node) override {
    indent();
    os << "BlockStmt\n";
    visitNodeVec(node.statements);
  }

  void visit(const DeclarationStmt& node) override {
    indent();
    os << (node.isConst ? "ConstDecl " : "VarDecl ") << node.name << '\n';
    if (node.typeName) {
      indent();
      os << "Type:\n";
      IndentGuard guard(*this);
      node.typeName->accept(*this);
    }
    visitNode(node.initializer, "Initializer");
  }

  void visit(const AssignmentStmt& node) override {
    indent();
    os << "Assignment target=" << node.target << '\n';
    visitNode(node.value, "Value");
  }

  void visit(const IndexAssignmentStmt& node) override {
    indent();
    os << "IndexAssignment target=" << node.target << '\n';
    visitNode(node.index, "Index");
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
    os << "BinaryExpr op=\"" << binaryOpDisplayName(node.op) << "\"\n";
    visitNode(node.lhs, "LHS");
    visitNode(node.rhs, "RHS");
  }

  void visit(const UnaryExpr& node) override {
    indent();
    os << "UnaryExpr op=\"" << unaryOpDisplayName(node.op) << "\"\n";
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

  void visit(const FieldAccessExpr& node) override {
    indent();
    os << "FieldAccess " << node.field << '\n';
    visitNode(node.base, "Base");
  }

  void visit(const IndexExpr& node) override {
    indent();
    os << "IndexExpr\n";
    visitNode(node.base, "Base");
    visitNode(node.index, "Index");
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

  void visit(const StructFieldInit& node) override {
    indent();
    os << "StructFieldInit " << node.name << '\n';
    visitNode(node.value, "Value");
  }

  void visit(const StructLiteralExpr& node) override {
    indent();
    os << "StructLiteral " << node.typeName << '\n';
    visitNodeVec(node.fields, "Fields");
  }

  void visit(const TypePrimitiveExpr& node) override {
    indent();
    os << "TypePrimitive " << typeKindName(node.kind) << '\n';
  }

  void visit(const NamedTypeExpr& node) override {
    indent();
    os << "TypeName " << node.name << '\n';
  }

  void visit(const TypeArrayExpr& node) override {
    indent();
    os << "TypeArray\n";
    visitNode(node.element, "Element");
  }

  void visit(const TypeSumExpr& node) override {
    indent();
    os << "TypeSum\n";
    visitNodeVec(node.variants, "Variants");
  }

  void visit(const TypeProductExpr& node) override {
    indent();
    os << "TypeProduct\n";
    visitNodeVec(node.members, "Members");
  }

private:
  std::ostream& os;
  unsigned currentIndent;

  void indent() { os << std::string(currentIndent, ' '); }

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
  case Kind::StructDecl:
    return "StructDecl";
  case Kind::EnumDecl:
    return "EnumDecl";
  case Kind::StructField:
    return "StructField";
  case Kind::EnumVariant:
    return "EnumVariant";
  case Kind::StructFieldInit:
    return "StructFieldInit";
  case Kind::FieldAccessExpr:
    return "FieldAccessExpr";
  case Kind::BlockStmt:
    return "BlockStmt";
  case Kind::StatementList:
    return "StatementList";
  case Kind::DeclarationStmt:
    return "DeclarationStmt";
  case Kind::AssignmentStmt:
    return "AssignmentStmt";
  case Kind::IndexAssignmentStmt:
    return "IndexAssignmentStmt";
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
  case Kind::TypeExpr:
    return "TypeExpr";
  case Kind::TypePrimitiveExpr:
    return "TypePrimitiveExpr";
  case Kind::TypeArrayExpr:
    return "TypeArrayExpr";
  case Kind::TypeSumExpr:
    return "TypeSumExpr";
  case Kind::TypeProductExpr:
    return "TypeProductExpr";
  case Kind::NamedTypeExpr:
    return "NamedTypeExpr";
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
  case Kind::IndexExpr:
    return "IndexExpr";
  case Kind::CallExpr:
    return "CallExpr";
  case Kind::ArrayLiteralExpr:
    return "ArrayLiteralExpr";
  case Kind::StructLiteralExpr:
    return "StructLiteralExpr";
  }
  return "Unknown";
}

void ASTNode::dump(std::ostream& os, unsigned indent) const {
  DumpVisitor visitor(os, indent);
  accept(visitor);
}

void ASTNode::dump() const {
  DumpVisitor visitor(std::cerr, 0);
  accept(visitor);
}

Program::Program() : NodeWithKind<Program, ASTNode::Kind::Program>(SourceLocation{0, 0}) {}

ImportDecl::ImportDecl(std::string moduleName, SourceLocation loc)
    : NodeWithKind<ImportDecl, ASTNode::Kind::ImportDecl>(loc), module(std::move(moduleName)) {}

StructDecl::StructDecl(std::string name, SourceLocation loc)
    : NodeWithKind<StructDecl, ASTNode::Kind::StructDecl>(loc), name(std::move(name)) {}

EnumDecl::EnumDecl(std::string name, SourceLocation loc)
    : NodeWithKind<EnumDecl, ASTNode::Kind::EnumDecl>(loc), name(std::move(name)) {}

StructField::StructField(std::string name, NodePtr<TypeExpr> type, SourceLocation loc)
    : NodeWithKind<StructField, ASTNode::Kind::StructField>(loc), name(std::move(name)),
      type(std::move(type)) {}

EnumVariant::EnumVariant(std::string name, NodePtr<TypeExpr> payload, SourceLocation loc)
    : NodeWithKind<EnumVariant, ASTNode::Kind::EnumVariant>(loc), name(std::move(name)),
      payload(std::move(payload)) {}

Parameter::Parameter(std::string name, NodePtr<TypeExpr> type, SourceLocation loc)
    : NodeWithKind<Parameter, ASTNode::Kind::Parameter>(loc), name(std::move(name)),
      type(std::move(type)) {}

BlockStmt::BlockStmt(SourceLocation loc)
    : NodeWithKind<BlockStmt, ASTNode::Kind::BlockStmt, Statement>(loc) {}

FunctionDecl::FunctionDecl(std::string fnName, SourceLocation loc)
    : NodeWithKind<FunctionDecl, ASTNode::Kind::FunctionDecl>(loc), name(std::move(fnName)) {}

DeclarationStmt::DeclarationStmt(bool isConst, std::string identifier, SourceLocation loc)
    : NodeWithKind<DeclarationStmt, ASTNode::Kind::DeclarationStmt, Statement>(loc),
      isConst(isConst), name(std::move(identifier)) {}

TypePrimitiveExpr::TypePrimitiveExpr(typing::TypeKind kind, SourceLocation loc)
    : NodeWithKind<TypePrimitiveExpr, ASTNode::Kind::TypePrimitiveExpr, TypeExpr>(loc),
      kind(kind) {}

NamedTypeExpr::NamedTypeExpr(std::string name, SourceLocation loc)
    : NodeWithKind<NamedTypeExpr, ASTNode::Kind::NamedTypeExpr, TypeExpr>(loc),
      name(std::move(name)) {}

TypeArrayExpr::TypeArrayExpr(SourceLocation loc)
    : NodeWithKind<TypeArrayExpr, ASTNode::Kind::TypeArrayExpr, TypeExpr>(loc) {}

TypeSumExpr::TypeSumExpr(SourceLocation loc)
    : NodeWithKind<TypeSumExpr, ASTNode::Kind::TypeSumExpr, TypeExpr>(loc) {}

TypeProductExpr::TypeProductExpr(SourceLocation loc)
    : NodeWithKind<TypeProductExpr, ASTNode::Kind::TypeProductExpr, TypeExpr>(loc) {}

AssignmentStmt::AssignmentStmt(std::string identifier, SourceLocation loc)
    : NodeWithKind<AssignmentStmt, ASTNode::Kind::AssignmentStmt, Statement>(loc),
      target(std::move(identifier)) {}

IndexAssignmentStmt::IndexAssignmentStmt(std::string identifier, SourceLocation loc)
    : NodeWithKind<IndexAssignmentStmt, ASTNode::Kind::IndexAssignmentStmt, Statement>(loc),
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

BinaryExpr::BinaryExpr(BinaryOp op, SourceLocation loc)
    : NodeWithKind<BinaryExpr, ASTNode::Kind::BinaryExpr, Expression>(loc), op(op) {}

UnaryExpr::UnaryExpr(UnaryOp op, SourceLocation loc)
    : NodeWithKind<UnaryExpr, ASTNode::Kind::UnaryExpr, Expression>(loc), op(op) {}

LiteralExpr::LiteralExpr(std::string literal, SourceLocation loc)
    : NodeWithKind<LiteralExpr, ASTNode::Kind::LiteralExpr, Expression>(loc),
      value(std::move(literal)) {}

IdentifierExpr::IdentifierExpr(std::string identifier, SourceLocation loc)
    : NodeWithKind<IdentifierExpr, ASTNode::Kind::IdentifierExpr, Expression>(loc),
      name(std::move(identifier)) {}

FieldAccessExpr::FieldAccessExpr(std::string field, SourceLocation loc)
    : NodeWithKind<FieldAccessExpr, ASTNode::Kind::FieldAccessExpr, Expression>(loc),
      field(std::move(field)) {}

IndexExpr::IndexExpr(SourceLocation loc)
    : NodeWithKind<IndexExpr, ASTNode::Kind::IndexExpr, Expression>(loc) {}

CallExpr::CallExpr(std::string callee, SourceLocation loc)
    : NodeWithKind<CallExpr, ASTNode::Kind::CallExpr, Expression>(loc), callee(std::move(callee)) {}

ArrayLiteralExpr::ArrayLiteralExpr(SourceLocation loc)
    : NodeWithKind<ArrayLiteralExpr, ASTNode::Kind::ArrayLiteralExpr, Expression>(loc) {}

StructFieldInit::StructFieldInit(std::string name, SourceLocation loc)
    : NodeWithKind<StructFieldInit, ASTNode::Kind::StructFieldInit>(loc),
      name(std::move(name)) {}

StructLiteralExpr::StructLiteralExpr(std::string typeName, SourceLocation loc)
    : NodeWithKind<StructLiteralExpr, ASTNode::Kind::StructLiteralExpr, Expression>(loc),
      typeName(std::move(typeName)) {}

const char* binaryOpName(BinaryOp op) { return binaryOpDisplayName(op); }

const char* unaryOpName(UnaryOp op) { return unaryOpDisplayName(op); }

} // namespace dhad::ast
