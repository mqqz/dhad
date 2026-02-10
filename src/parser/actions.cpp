#include "parser.hpp"

#include "../ast/builders.hpp"

#include <array>

namespace dhad::parser {

namespace {

using SV = SemanticValue;
using namespace dhad::ast::build;
using TypeExprPtr = SV::TypeExprPtr;

typing::TypeKind tokenToTypeKind(const Token& tok) {
  switch (tok.kind) {
  case TokenType::KW_INT:
    return typing::TypeKind::Int;
  case TokenType::KW_FLOAT:
    return typing::TypeKind::Float;
  case TokenType::KW_BOOL:
    return typing::TypeKind::Bool;
  case TokenType::KW_CHAR:
    return typing::TypeKind::Char;
  case TokenType::KW_STRING:
    return typing::TypeKind::String;
  case TokenType::KW_NULL:
    return typing::TypeKind::Null;
  default:
    return typing::TypeKind::Null;
  }
}

TypeExprPtr appendSumVariant(TypeExprPtr sum, TypeExprPtr variant, SourceLocation loc) {
  if (!sum) {
    auto node = std::make_unique<ast::TypeSumExpr>(loc);
    node->variants.push_back(std::move(variant));
    return node;
  }
  if (auto* sumNode = llvm::dyn_cast<ast::TypeSumExpr>(sum.get())) {
    sumNode->variants.push_back(std::move(variant));
    return sum;
  }
  auto node = std::make_unique<ast::TypeSumExpr>(loc);
  node->variants.push_back(std::move(sum));
  node->variants.push_back(std::move(variant));
  return node;
}

struct ReductionContext {
  std::vector<SemanticValue> values;

  explicit ReductionContext(std::vector<SemanticValue>&& input) : values(std::move(input)) {}

  template <typename T> T take(std::size_t idx) { return values[idx].take<T>(); }
  [[nodiscard]] std::size_t size() const { return values.size(); }
};

#define DECLARE_ACTION_HANDLER(name) SemanticValue action##name(ReductionContext&);
DHAD_PARSER_FOR_EACH_ACTION(DECLARE_ACTION_HANDLER)
#undef DECLARE_ACTION_HANDLER

using ActionHandler = SemanticValue (*)(ReductionContext&);

constexpr auto makeActionTable() {
  std::array<ActionHandler, static_cast<std::size_t>(ActionId::Count)> table{};
#define REGISTER_ACTION(name) table[static_cast<std::size_t>(ActionId::name)] = &action##name;
  DHAD_PARSER_FOR_EACH_ACTION(REGISTER_ACTION)
#undef REGISTER_ACTION
  return table;
}

const auto kActionHandlers = makeActionTable();

SV::TopLevelList appendNode(SV::TopLevelList list, SV::ASTNodePtr node) {
  list.push_back(std::move(node));
  return list;
}

// ----------------------------------------------------------------------------- //
// Action implementations
// ----------------------------------------------------------------------------- //

SemanticValue actionAugmentedStartProgram(ReductionContext& ctx) {
  if (ctx.size() == 1) {
    if (ctx.values[0].holds<SV::ProgramPtr>()) {
      return SemanticValue(ctx.take<SV::ProgramPtr>(0));
    }
    if (ctx.values[0].hasNode()) {
      return SemanticValue(ctx.values[0].takeNode());
    }
  }
  return SemanticValue{};
}

SemanticValue actionProgramFinish(ReductionContext& ctx) {
  auto topLevels = ctx.take<SV::TopLevelList>(0);
  if (ctx.size() > 1) {
    (void)ctx.take<Token>(1);
  }
  auto program = std::make_unique<ast::Program>();
  program->topLevel.reserve(topLevels.size());
  for (auto& node : topLevels) {
    program->topLevel.push_back(std::move(node));
  }
  return SemanticValue(std::move(program));
}

SemanticValue actionTopLevelListAppend(ReductionContext& ctx) {
  auto list = ctx.take<SV::TopLevelList>(0);
  list = appendNode(std::move(list), ctx.values[1].takeNode());
  return SemanticValue(std::move(list));
}

SemanticValue actionTopLevelListEmpty(ReductionContext&) {
  return SemanticValue(SV::TopLevelList{});
}

SemanticValue actionTopLevelImport(ReductionContext& ctx) {
  return SemanticValue(ctx.values[0].takeNode());
}
SemanticValue actionTopLevelFunction(ReductionContext& ctx) {
  return SemanticValue(ctx.values[0].takeNode());
}

SemanticValue actionTopLevelTypeDecl(ReductionContext& ctx) {
  return SemanticValue(ctx.values[0].takeNode());
}

SemanticValue actionTopLevelStatement(ReductionContext& ctx) {
  SV::ASTNodePtr node = ctx.take<SV::StatementPtr>(0);
  return SemanticValue(std::move(node));
}

SemanticValue actionImportDecl(ReductionContext& ctx) {
  auto kw = ctx.take<Token>(0);
  auto ident = ctx.take<Token>(1);
  (void)ctx.take<Token>(2);
  auto node = std::make_unique<ast::ImportDecl>(tokenLexeme(ident), kw.loc);
  return SemanticValue(SV::ASTNodePtr(std::move(node)));
}

SemanticValue actionTypeDeclStruct(ReductionContext& ctx) {
  return SemanticValue(ctx.values[0].takeNode());
}

SemanticValue actionTypeDeclEnum(ReductionContext& ctx) {
  return SemanticValue(ctx.values[0].takeNode());
}

SemanticValue actionStructDecl(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto nameTok = ctx.take<Token>(2);
  (void)ctx.take<Token>(3);
  auto fields = ctx.take<SV::StructFieldList>(4);
  (void)ctx.take<Token>(5);
  (void)ctx.take<Token>(6);
  auto node = std::make_unique<ast::StructDecl>(tokenLexeme(nameTok), nameTok.loc);
  for (auto& field : fields) {
    node->fields.push_back(std::move(field));
  }
  return SemanticValue(SV::ASTNodePtr(std::move(node)));
}

SemanticValue actionStructFieldListSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::StructFieldList>(0));
}

SemanticValue actionStructFieldListNone(ReductionContext&) {
  return SemanticValue(SV::StructFieldList{});
}

SemanticValue actionStructFieldListBuild(ReductionContext& ctx) {
  auto first = ctx.take<SV::StructFieldPtr>(0);
  auto tail = ctx.take<SV::StructFieldList>(1);
  SV::StructFieldList list;
  list.push_back(std::move(first));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionStructFieldTailAppend(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto field = ctx.take<SV::StructFieldPtr>(1);
  auto tail = ctx.take<SV::StructFieldList>(2);
  SV::StructFieldList list;
  list.push_back(std::move(field));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionStructFieldTailEmpty(ReductionContext&) {
  return SemanticValue(SV::StructFieldList{});
}

SemanticValue actionStructField(ReductionContext& ctx) {
  auto nameTok = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto typeName = ctx.take<TypeExprPtr>(2);
  auto field =
      std::make_unique<ast::StructField>(tokenLexeme(nameTok), std::move(typeName), nameTok.loc);
  return SemanticValue(SV::StructFieldPtr(std::move(field)));
}

SemanticValue actionEnumDecl(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto nameTok = ctx.take<Token>(2);
  (void)ctx.take<Token>(3);
  auto variants = ctx.take<SV::EnumVariantList>(4);
  (void)ctx.take<Token>(5);
  (void)ctx.take<Token>(6);
  auto node = std::make_unique<ast::EnumDecl>(tokenLexeme(nameTok), nameTok.loc);
  for (auto& variant : variants) {
    node->variants.push_back(std::move(variant));
  }
  return SemanticValue(SV::ASTNodePtr(std::move(node)));
}

SemanticValue actionEnumVariantListSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::EnumVariantList>(0));
}

SemanticValue actionEnumVariantListNone(ReductionContext&) {
  return SemanticValue(SV::EnumVariantList{});
}

SemanticValue actionEnumVariantListBuild(ReductionContext& ctx) {
  auto first = ctx.take<SV::EnumVariantPtr>(0);
  auto tail = ctx.take<SV::EnumVariantList>(1);
  SV::EnumVariantList list;
  list.push_back(std::move(first));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionEnumVariantTailAppend(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto variant = ctx.take<SV::EnumVariantPtr>(1);
  auto tail = ctx.take<SV::EnumVariantList>(2);
  SV::EnumVariantList list;
  list.push_back(std::move(variant));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionEnumVariantTailEmpty(ReductionContext&) {
  return SemanticValue(SV::EnumVariantList{});
}

SemanticValue actionEnumVariantPayloadSome(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  return SemanticValue(ctx.take<TypeExprPtr>(1));
}

SemanticValue actionEnumVariantPayloadNone(ReductionContext&) {
  return SemanticValue(TypeExprPtr{});
}

SemanticValue actionEnumVariant(ReductionContext& ctx) {
  auto nameTok = ctx.take<Token>(0);
  auto payload = ctx.take<TypeExprPtr>(1);
  auto variant =
      std::make_unique<ast::EnumVariant>(tokenLexeme(nameTok), std::move(payload), nameTok.loc);
  return SemanticValue(SV::EnumVariantPtr(std::move(variant)));
}

SemanticValue actionFunctionDecl(ReductionContext& ctx) {
  auto fnTok = ctx.take<Token>(0);
  auto nameTok = ctx.take<Token>(1);
  (void)ctx.take<Token>(2);
  auto params = ctx.take<SV::ParameterList>(3);
  (void)ctx.take<Token>(4);
  auto retType = ctx.take<TypeExprPtr>(5);
  auto body = ctx.take<SV::BlockPtr>(6);
  auto fn = std::make_unique<ast::FunctionDecl>(tokenLexeme(nameTok), fnTok.loc);
  fn->params = std::move(params);
  fn->returnType = std::move(retType);
  fn->body = std::move(body);
  return SemanticValue(SV::ASTNodePtr(std::move(fn)));
}

SemanticValue actionFunctionReturnTypeSome(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto typeName = ctx.take<TypeExprPtr>(1);
  return SemanticValue(std::move(typeName));
}

SemanticValue actionFunctionReturnTypeNone(ReductionContext&) {
  return SemanticValue(TypeExprPtr{});
}

SemanticValue actionParameterListSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::ParameterList>(0));
}

SemanticValue actionParameterListNone(ReductionContext&) {
  return SemanticValue(SV::ParameterList{});
}

SemanticValue actionParameterListBuild(ReductionContext& ctx) {
  auto first = ctx.take<SV::ParameterPtr>(0);
  auto tail = ctx.take<SV::ParameterList>(1);
  SV::ParameterList list;
  list.push_back(std::move(first));
  for (auto& param : tail) {
    list.push_back(std::move(param));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionParameterTailAppend(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto param = ctx.take<SV::ParameterPtr>(1);
  auto tail = ctx.take<SV::ParameterList>(2);
  SV::ParameterList list;
  list.push_back(std::move(param));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionParameterTailEmpty(ReductionContext&) {
  return SemanticValue(SV::ParameterList{});
}

SemanticValue actionParameterDecl(ReductionContext& ctx) {
  auto nameTok = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto typeName = ctx.take<TypeExprPtr>(2);
  auto param =
      std::make_unique<ast::Parameter>(tokenLexeme(nameTok), std::move(typeName), nameTok.loc);
  return SemanticValue(SV::ParameterPtr(std::move(param)));
}

SemanticValue actionTypeNameKeyword(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  auto type = std::make_unique<ast::TypePrimitiveExpr>(tokenToTypeKind(tok), tok.loc);
  return SemanticValue(TypeExprPtr(std::move(type)));
}

SemanticValue actionTypeNameIdentifier(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  auto type = std::make_unique<ast::NamedTypeExpr>(tokenLexeme(tok), tok.loc);
  return SemanticValue(TypeExprPtr(std::move(type)));
}

SemanticValue actionBlockBuild(ReductionContext& ctx) {
  auto lbrace = ctx.take<Token>(0);
  auto statements = ctx.take<SV::StatementList>(1);
  (void)ctx.take<Token>(2);
  auto block = std::make_unique<ast::BlockStmt>(lbrace.loc);
  for (auto& stmt : statements) {
    block->statements.push_back(std::move(stmt));
  }
  return SemanticValue(SV::BlockPtr(std::move(block)));
}

SemanticValue actionStatementListAppend(ReductionContext& ctx) {
  auto list = ctx.take<SV::StatementList>(0);
  list.push_back(ctx.take<SV::StatementPtr>(1));
  return SemanticValue(std::move(list));
}

SemanticValue actionStatementListEmpty(ReductionContext&) {
  return SemanticValue(SV::StatementList{});
}

SemanticValue actionStatementFromMatched(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::StatementPtr>(0));
}

SemanticValue actionStatementFromUnmatched(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::StatementPtr>(0));
}

SemanticValue actionPassStatementSemicolon(ReductionContext& ctx) {
  auto stmt = ctx.take<SV::StatementPtr>(0);
  if (ctx.size() > 1) {
    (void)ctx.take<Token>(1);
  }
  return SemanticValue(std::move(stmt));
}

SemanticValue actionPassStatement(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::StatementPtr>(0));
}

SemanticValue actionExpressionStatement(ReductionContext& ctx) {
  auto expr = ctx.take<SV::ExpressionPtr>(0);
  auto semi = ctx.take<Token>(1);
  auto stmt = std::make_unique<ast::ExpressionStmt>(semi.loc);
  stmt->expr = std::move(expr);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionBlockStatement(ReductionContext& ctx) {
  SV::StatementPtr stmt = ctx.take<SV::BlockPtr>(0);
  return SemanticValue(std::move(stmt));
}

SemanticValue actionDeclarationStmt(ReductionContext& ctx) {
  auto keyword = ctx.take<Token>(0);
  auto ident = ctx.take<Token>(1);
  auto typeOpt = ctx.take<TypeExprPtr>(2);
  (void)ctx.take<Token>(3);
  auto expr = ctx.take<SV::ExpressionPtr>(4);
  auto decl = std::make_unique<ast::DeclarationStmt>(keyword.kind == TokenType::KW_CONST,
                                                     tokenLexeme(ident), keyword.loc);
  decl->typeName = std::move(typeOpt);
  decl->initializer = std::move(expr);
  return SemanticValue(SV::StatementPtr(std::move(decl)));
}

SemanticValue actionTypeAnnotationSome(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto typeName = ctx.take<TypeExprPtr>(1);
  return SemanticValue(std::move(typeName));
}

SemanticValue actionTypeAnnotationNone(ReductionContext&) {
  return SemanticValue(TypeExprPtr{});
}

SemanticValue actionTypeNameUnion(ReductionContext& ctx) {
  auto lhs = ctx.take<TypeExprPtr>(0);
  auto pipeTok = ctx.take<Token>(1);
  auto rhs = ctx.take<TypeExprPtr>(2);
  auto sum = appendSumVariant(std::move(lhs), std::move(rhs), pipeTok.loc);
  return SemanticValue(std::move(sum));
}

SemanticValue actionTypeNameFromPrimary(ReductionContext& ctx) {
  return SemanticValue(ctx.take<TypeExprPtr>(0));
}

SemanticValue actionTypePrimaryArray(ReductionContext& ctx) {
  auto lbracket = ctx.take<Token>(0);
  auto inner = ctx.take<TypeExprPtr>(1);
  (void)ctx.take<Token>(2);
  auto type = std::make_unique<ast::TypeArrayExpr>(lbracket.loc);
  type->element = std::move(inner);
  return SemanticValue(TypeExprPtr(std::move(type)));
}

SemanticValue actionTypePrimaryTuple(ReductionContext& ctx) {
  return SemanticValue(ctx.take<TypeExprPtr>(0));
}

SemanticValue actionTypePrimaryGrouped(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto inner = ctx.take<TypeExprPtr>(1);
  (void)ctx.take<Token>(2);
  return SemanticValue(std::move(inner));
}

SemanticValue actionTypeTupleBuild(ReductionContext& ctx) {
  auto lparen = ctx.take<Token>(0);
  auto first = ctx.take<TypeExprPtr>(1);
  (void)ctx.take<Token>(2);
  auto second = ctx.take<TypeExprPtr>(3);
  auto tail = ctx.take<SV::TypeExprList>(4);
  (void)ctx.take<Token>(5);
  auto type = std::make_unique<ast::TypeProductExpr>(lparen.loc);
  type->members.push_back(std::move(first));
  type->members.push_back(std::move(second));
  for (auto& entry : tail) {
    type->members.push_back(std::move(entry));
  }
  return SemanticValue(TypeExprPtr(std::move(type)));
}

SemanticValue actionTypeTupleTailAppend(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto type = ctx.take<TypeExprPtr>(1);
  auto tail = ctx.take<SV::TypeExprList>(2);
  SV::TypeExprList list;
  list.push_back(std::move(type));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionTypeTupleTailEmpty(ReductionContext&) {
  return SemanticValue(SV::TypeExprList{});
}

SemanticValue actionAssignmentStmt(ReductionContext& ctx) {
  auto ident = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto value = ctx.take<SV::ExpressionPtr>(2);
  auto stmt = std::make_unique<ast::AssignmentStmt>(tokenLexeme(ident), ident.loc);
  stmt->value = std::move(value);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionIfMatchedFull(ReductionContext& ctx) {
  auto ifTok = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto condition = ctx.take<SV::ExpressionPtr>(2);
  (void)ctx.take<Token>(3);
  auto thenBranch = ctx.take<SV::StatementPtr>(4);
  (void)ctx.take<Token>(5);
  auto elseBranch = ctx.take<SV::StatementPtr>(6);
  auto stmt = std::make_unique<ast::IfStmt>(ifTok.loc);
  stmt->condition = std::move(condition);
  stmt->thenBranch = std::move(thenBranch);
  stmt->elseBranch = std::move(elseBranch);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionIfUnmatchedNoElse(ReductionContext& ctx) {
  auto ifTok = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto condition = ctx.take<SV::ExpressionPtr>(2);
  (void)ctx.take<Token>(3);
  auto thenBranch = ctx.take<SV::StatementPtr>(4);
  auto stmt = std::make_unique<ast::IfStmt>(ifTok.loc);
  stmt->condition = std::move(condition);
  stmt->thenBranch = std::move(thenBranch);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionIfUnmatchedWithElse(ReductionContext& ctx) {
  auto ifTok = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto condition = ctx.take<SV::ExpressionPtr>(2);
  (void)ctx.take<Token>(3);
  auto thenBranch = ctx.take<SV::StatementPtr>(4);
  (void)ctx.take<Token>(5);
  auto elseBranch = ctx.take<SV::StatementPtr>(6);
  auto stmt = std::make_unique<ast::IfStmt>(ifTok.loc);
  stmt->condition = std::move(condition);
  stmt->thenBranch = std::move(thenBranch);
  stmt->elseBranch = std::move(elseBranch);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionWhileStmt(ReductionContext& ctx) {
  auto kw = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto condition = ctx.take<SV::ExpressionPtr>(2);
  (void)ctx.take<Token>(3);
  auto body = ctx.take<SV::StatementPtr>(4);
  auto stmt = std::make_unique<ast::WhileStmt>(kw.loc);
  stmt->condition = std::move(condition);
  stmt->body = std::move(body);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionForStmt(ReductionContext& ctx) {
  auto kw = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto condition = ctx.take<SV::ExpressionPtr>(2);
  (void)ctx.take<Token>(3);
  auto body = ctx.take<SV::StatementPtr>(4);
  auto stmt = std::make_unique<ast::ForStmt>(kw.loc);
  stmt->condition = std::move(condition);
  stmt->body = std::move(body);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionReturnStmt(ReductionContext& ctx) {
  auto kw = ctx.take<Token>(0);
  auto value = ctx.take<SV::ExpressionPtr>(1);
  auto stmt = std::make_unique<ast::ReturnStmt>(kw.loc);
  stmt->value = std::move(value);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionReturnExprSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::ExpressionPtr>(0));
}

SemanticValue actionReturnExprNone(ReductionContext&) { return SemanticValue(SV::ExpressionPtr{}); }

SemanticValue actionBreakStmt(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  auto stmt = std::make_unique<ast::BreakStmt>(tok.loc);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionContinueStmt(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  auto stmt = std::make_unique<ast::ContinueStmt>(tok.loc);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

SemanticValue actionCallExpression(ReductionContext& ctx) {
  auto ident = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto args = ctx.take<SV::ExpressionList>(2);
  (void)ctx.take<Token>(3);
  auto call = std::make_unique<ast::CallExpr>(tokenLexeme(ident), ident.loc);
  for (auto& arg : args) {
    call->args.push_back(std::move(arg));
  }
  return SemanticValue(SV::ExpressionPtr(std::move(call)));
}

SemanticValue actionArgumentListSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::ExpressionList>(0));
}

SemanticValue actionArgumentListNone(ReductionContext&) {
  return SemanticValue(SV::ExpressionList{});
}

SemanticValue actionArgumentListBuild(ReductionContext& ctx) {
  auto first = ctx.take<SV::ExpressionPtr>(0);
  auto tail = ctx.take<SV::ExpressionList>(1);
  SV::ExpressionList list;
  list.push_back(std::move(first));
  for (auto& expr : tail) {
    list.push_back(std::move(expr));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionArgumentTailAppend(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto expr = ctx.take<SV::ExpressionPtr>(1);
  auto tail = ctx.take<SV::ExpressionList>(2);
  SV::ExpressionList list;
  list.push_back(std::move(expr));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionArgumentTailEmpty(ReductionContext&) {
  return SemanticValue(SV::ExpressionList{});
}

SemanticValue actionExpressionPass(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::ExpressionPtr>(0));
}

SemanticValue actionFieldAccess(ReductionContext& ctx) {
  auto base = ctx.take<SV::ExpressionPtr>(0);
  (void)ctx.take<Token>(1);
  auto fieldTok = ctx.take<Token>(2);
  auto node = std::make_unique<ast::FieldAccessExpr>(tokenLexeme(fieldTok), fieldTok.loc);
  node->base = std::move(base);
  return SemanticValue(SV::ExpressionPtr(std::move(node)));
}

SemanticValue actionBinaryExpr(ReductionContext& ctx) {
  auto lhs = ctx.take<SV::ExpressionPtr>(0);
  auto op = ctx.take<Token>(1);
  auto rhs = ctx.take<SV::ExpressionPtr>(2);
  return SemanticValue(makeBinaryExpr(op, std::move(lhs), std::move(rhs)));
}

SemanticValue actionUnaryPrefix(ReductionContext& ctx) {
  auto op = ctx.take<Token>(0);
  auto operand = ctx.take<SV::ExpressionPtr>(1);
  return SemanticValue(makeUnaryExpr(op, std::move(operand)));
}

SemanticValue actionPrimaryLiteral(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  return SemanticValue(makeLiteralExpr(tok));
}

SemanticValue actionPrimaryIdentifier(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  return SemanticValue(makeIdentifierExpr(tok, tokenLexeme(tok)));
}

SemanticValue actionPrimaryGrouping(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto expr = ctx.take<SV::ExpressionPtr>(1);
  (void)ctx.take<Token>(2);
  return SemanticValue(std::move(expr));
}

SemanticValue actionArrayLiteral(ReductionContext& ctx) {
  auto lbracket = ctx.take<Token>(0);
  auto elements = ctx.take<SV::ExpressionList>(1);
  (void)ctx.take<Token>(2);
  auto node = std::make_unique<ast::ArrayLiteralExpr>(lbracket.loc);
  for (auto& element : elements) {
    node->elements.push_back(std::move(element));
  }
  return SemanticValue(SV::ExpressionPtr(std::move(node)));
}

SemanticValue actionStructLiteral(ReductionContext& ctx) {
  auto nameTok = ctx.take<Token>(0);
  auto lbrace = ctx.take<Token>(1);
  auto fields = ctx.take<SV::StructFieldInitList>(2);
  (void)ctx.take<Token>(3);
  auto node = std::make_unique<ast::StructLiteralExpr>(tokenLexeme(nameTok), lbrace.loc);
  for (auto& field : fields) {
    node->fields.push_back(std::move(field));
  }
  return SemanticValue(SV::ExpressionPtr(std::move(node)));
}

SemanticValue actionStructFieldInitListSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::StructFieldInitList>(0));
}

SemanticValue actionStructFieldInitListNone(ReductionContext&) {
  return SemanticValue(SV::StructFieldInitList{});
}

SemanticValue actionStructFieldInitListBuild(ReductionContext& ctx) {
  auto first = ctx.take<SV::StructFieldInitPtr>(0);
  auto tail = ctx.take<SV::StructFieldInitList>(1);
  SV::StructFieldInitList list;
  list.push_back(std::move(first));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionStructFieldInitTailAppend(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto field = ctx.take<SV::StructFieldInitPtr>(1);
  auto tail = ctx.take<SV::StructFieldInitList>(2);
  SV::StructFieldInitList list;
  list.push_back(std::move(field));
  for (auto& entry : tail) {
    list.push_back(std::move(entry));
  }
  return SemanticValue(std::move(list));
}

SemanticValue actionStructFieldInitTailEmpty(ReductionContext&) {
  return SemanticValue(SV::StructFieldInitList{});
}

SemanticValue actionStructFieldInit(ReductionContext& ctx) {
  auto nameTok = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto value = ctx.take<SV::ExpressionPtr>(2);
  auto node = std::make_unique<ast::StructFieldInit>(tokenLexeme(nameTok), nameTok.loc);
  node->value = std::move(value);
  return SemanticValue(SV::StructFieldInitPtr(std::move(node)));
}

} // namespace

SemanticValue applyAction(ActionId action, std::vector<SemanticValue>&& children) {
  const auto index = static_cast<std::size_t>(action);
  if (index >= kActionHandlers.size()) {
    return SemanticValue{};
  }
  auto handler = kActionHandlers[index];
  if (!handler) {
    return SemanticValue{};
  }
  ReductionContext ctx(std::move(children));
  return handler(ctx);
}

} // namespace dhad::parser
