#include "parser.hpp"

#include "../ast/builders.hpp"

#include <array>

namespace dhad::parser {

namespace {

using SV = SemanticValue;
using namespace dhad::ast::build;

struct ReductionContext {
  std::vector<SemanticValue> values;

  explicit ReductionContext(std::vector<SemanticValue>&& input) : values(std::move(input)) {}

  template <typename T> T take(std::size_t idx) { return values[idx].take<T>(); }
  [[nodiscard]] std::size_t size() const { return values.size(); }
};

#define DECLARE_ACTION_HANDLER(name) static SemanticValue action##name(ReductionContext&);
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

static SemanticValue actionAugmentedStartProgram(ReductionContext& ctx) {
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

static SemanticValue actionProgramFinish(ReductionContext& ctx) {
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

static SemanticValue actionTopLevelListAppend(ReductionContext& ctx) {
  auto list = ctx.take<SV::TopLevelList>(0);
  list = appendNode(std::move(list), ctx.values[1].takeNode());
  return SemanticValue(std::move(list));
}

static SemanticValue actionTopLevelListEmpty(ReductionContext&) {
  return SemanticValue(SV::TopLevelList{});
}

static SemanticValue actionTopLevelImport(ReductionContext& ctx) {
  return SemanticValue(ctx.values[0].takeNode());
}
static SemanticValue actionTopLevelFunction(ReductionContext& ctx) {
  return SemanticValue(ctx.values[0].takeNode());
}

static SemanticValue actionTopLevelStatement(ReductionContext& ctx) {
  SV::ASTNodePtr node = ctx.take<SV::StatementPtr>(0);
  return SemanticValue(std::move(node));
}

static SemanticValue actionImportDecl(ReductionContext& ctx) {
  auto kw = ctx.take<Token>(0);
  auto ident = ctx.take<Token>(1);
  (void)ctx.take<Token>(2);
  auto node = std::make_unique<ast::ImportDecl>(tokenLexeme(ident), kw.loc);
  return SemanticValue(SV::ASTNodePtr(std::move(node)));
}

static SemanticValue actionFunctionDecl(ReductionContext& ctx) {
  auto fnTok = ctx.take<Token>(0);
  auto nameTok = ctx.take<Token>(1);
  (void)ctx.take<Token>(2);
  auto params = ctx.take<SV::ParameterList>(3);
  (void)ctx.take<Token>(4);
  auto retType = ctx.take<std::optional<std::string>>(5);
  auto body = ctx.take<SV::BlockPtr>(6);
  auto fn = std::make_unique<ast::FunctionDecl>(tokenLexeme(nameTok), fnTok.loc);
  fn->params = std::move(params);
  fn->returnType = std::move(retType);
  fn->body = std::move(body);
  return SemanticValue(SV::ASTNodePtr(std::move(fn)));
}

static SemanticValue actionFunctionReturnTypeSome(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto typeName = ctx.take<std::string>(1);
  return SemanticValue(std::optional<std::string>{std::move(typeName)});
}

static SemanticValue actionFunctionReturnTypeNone(ReductionContext&) {
  return SemanticValue(std::optional<std::string>{});
}

static SemanticValue actionParameterListSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::ParameterList>(0));
}

static SemanticValue actionParameterListNone(ReductionContext&) {
  return SemanticValue(SV::ParameterList{});
}

static SemanticValue actionParameterListBuild(ReductionContext& ctx) {
  auto first = ctx.take<SV::ParameterPtr>(0);
  auto tail = ctx.take<SV::ParameterList>(1);
  SV::ParameterList list;
  list.push_back(std::move(first));
  for (auto& param : tail) {
    list.push_back(std::move(param));
  }
  return SemanticValue(std::move(list));
}

static SemanticValue actionParameterTailAppend(ReductionContext& ctx) {
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

static SemanticValue actionParameterTailEmpty(ReductionContext&) {
  return SemanticValue(SV::ParameterList{});
}

static SemanticValue actionParameterDecl(ReductionContext& ctx) {
  auto nameTok = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto typeName = ctx.take<std::string>(2);
  auto param =
      std::make_unique<ast::Parameter>(tokenLexeme(nameTok), std::move(typeName), nameTok.loc);
  return SemanticValue(SV::ParameterPtr(std::move(param)));
}

static SemanticValue actionTypeNameKeyword(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  return SemanticValue(tokenText(tok));
}

static SemanticValue actionBlockBuild(ReductionContext& ctx) {
  auto lbrace = ctx.take<Token>(0);
  auto statements = ctx.take<SV::StatementList>(1);
  (void)ctx.take<Token>(2);
  auto block = std::make_unique<ast::BlockStmt>(lbrace.loc);
  for (auto& stmt : statements) {
    block->statements.push_back(std::move(stmt));
  }
  return SemanticValue(SV::BlockPtr(std::move(block)));
}

static SemanticValue actionStatementListAppend(ReductionContext& ctx) {
  auto list = ctx.take<SV::StatementList>(0);
  list.push_back(ctx.take<SV::StatementPtr>(1));
  return SemanticValue(std::move(list));
}

static SemanticValue actionStatementListEmpty(ReductionContext&) {
  return SemanticValue(SV::StatementList{});
}

static SemanticValue actionStatementFromMatched(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::StatementPtr>(0));
}

static SemanticValue actionStatementFromUnmatched(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::StatementPtr>(0));
}

static SemanticValue actionPassStatementSemicolon(ReductionContext& ctx) {
  auto stmt = ctx.take<SV::StatementPtr>(0);
  if (ctx.size() > 1) {
    (void)ctx.take<Token>(1);
  }
  return SemanticValue(std::move(stmt));
}

static SemanticValue actionPassStatement(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::StatementPtr>(0));
}

static SemanticValue actionExpressionStatement(ReductionContext& ctx) {
  auto expr = ctx.take<SV::ExpressionPtr>(0);
  auto semi = ctx.take<Token>(1);
  auto stmt = std::make_unique<ast::ExpressionStmt>(semi.loc);
  stmt->expr = std::move(expr);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

static SemanticValue actionBlockStatement(ReductionContext& ctx) {
  SV::StatementPtr stmt = ctx.take<SV::BlockPtr>(0);
  return SemanticValue(std::move(stmt));
}

static SemanticValue actionDeclarationStmt(ReductionContext& ctx) {
  auto keyword = ctx.take<Token>(0);
  auto ident = ctx.take<Token>(1);
  auto typeOpt = ctx.take<std::optional<std::string>>(2);
  (void)ctx.take<Token>(3);
  auto expr = ctx.take<SV::ExpressionPtr>(4);
  auto decl = std::make_unique<ast::DeclarationStmt>(keyword.kind == TokenType::KW_CONST,
                                                     tokenLexeme(ident), keyword.loc);
  decl->typeName = std::move(typeOpt);
  decl->initializer = std::move(expr);
  return SemanticValue(SV::StatementPtr(std::move(decl)));
}

static SemanticValue actionTypeAnnotationSome(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto typeName = ctx.take<std::string>(1);
  return SemanticValue(std::optional<std::string>{std::move(typeName)});
}

static SemanticValue actionTypeAnnotationNone(ReductionContext&) {
  return SemanticValue(std::optional<std::string>{});
}

static SemanticValue actionAssignmentStmt(ReductionContext& ctx) {
  auto ident = ctx.take<Token>(0);
  (void)ctx.take<Token>(1);
  auto value = ctx.take<SV::ExpressionPtr>(2);
  auto stmt = std::make_unique<ast::AssignmentStmt>(tokenLexeme(ident), ident.loc);
  stmt->value = std::move(value);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

static SemanticValue actionIfMatchedFull(ReductionContext& ctx) {
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

static SemanticValue actionIfUnmatchedNoElse(ReductionContext& ctx) {
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

static SemanticValue actionIfUnmatchedWithElse(ReductionContext& ctx) {
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

static SemanticValue actionWhileStmt(ReductionContext& ctx) {
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

static SemanticValue actionForStmt(ReductionContext& ctx) {
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

static SemanticValue actionReturnStmt(ReductionContext& ctx) {
  auto kw = ctx.take<Token>(0);
  auto value = ctx.take<SV::ExpressionPtr>(1);
  auto stmt = std::make_unique<ast::ReturnStmt>(kw.loc);
  stmt->value = std::move(value);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

static SemanticValue actionReturnExprSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::ExpressionPtr>(0));
}

static SemanticValue actionReturnExprNone(ReductionContext&) {
  return SemanticValue(SV::ExpressionPtr{});
}

static SemanticValue actionBreakStmt(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  auto stmt = std::make_unique<ast::BreakStmt>(tok.loc);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

static SemanticValue actionContinueStmt(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  auto stmt = std::make_unique<ast::ContinueStmt>(tok.loc);
  return SemanticValue(SV::StatementPtr(std::move(stmt)));
}

static SemanticValue actionCallExpression(ReductionContext& ctx) {
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

static SemanticValue actionArgumentListSome(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::ExpressionList>(0));
}

static SemanticValue actionArgumentListNone(ReductionContext&) {
  return SemanticValue(SV::ExpressionList{});
}

static SemanticValue actionArgumentListBuild(ReductionContext& ctx) {
  auto first = ctx.take<SV::ExpressionPtr>(0);
  auto tail = ctx.take<SV::ExpressionList>(1);
  SV::ExpressionList list;
  list.push_back(std::move(first));
  for (auto& expr : tail) {
    list.push_back(std::move(expr));
  }
  return SemanticValue(std::move(list));
}

static SemanticValue actionArgumentTailAppend(ReductionContext& ctx) {
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

static SemanticValue actionArgumentTailEmpty(ReductionContext&) {
  return SemanticValue(SV::ExpressionList{});
}

static SemanticValue actionExpressionPass(ReductionContext& ctx) {
  return SemanticValue(ctx.take<SV::ExpressionPtr>(0));
}

static SemanticValue actionBinaryExpr(ReductionContext& ctx) {
  auto lhs = ctx.take<SV::ExpressionPtr>(0);
  auto op = ctx.take<Token>(1);
  auto rhs = ctx.take<SV::ExpressionPtr>(2);
  return SemanticValue(makeBinaryExpr(op, std::move(lhs), std::move(rhs)));
}

static SemanticValue actionUnaryPrefix(ReductionContext& ctx) {
  auto op = ctx.take<Token>(0);
  auto operand = ctx.take<SV::ExpressionPtr>(1);
  return SemanticValue(makeUnaryExpr(op, std::move(operand)));
}

static SemanticValue actionPrimaryLiteral(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  return SemanticValue(makeLiteralExpr(tok));
}

static SemanticValue actionPrimaryIdentifier(ReductionContext& ctx) {
  auto tok = ctx.take<Token>(0);
  return SemanticValue(makeIdentifierExpr(tok, tokenLexeme(tok)));
}

static SemanticValue actionPrimaryGrouping(ReductionContext& ctx) {
  (void)ctx.take<Token>(0);
  auto expr = ctx.take<SV::ExpressionPtr>(1);
  (void)ctx.take<Token>(2);
  return SemanticValue(std::move(expr));
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
