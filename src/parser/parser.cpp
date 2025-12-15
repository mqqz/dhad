#include "parser.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#ifdef DHAD_GENERATED_PARSER_TABLE_HEADER
#include DHAD_GENERATED_PARSER_TABLE_HEADER
#else
#include "generated/parser_tables.gen.hpp"
#endif

namespace dhad::parser {

namespace {

struct NonTerminalLookup {
  std::string_view name;
  NonTerminalId id;
};

constexpr std::array<NonTerminalLookup, static_cast<std::size_t>(NonTerminalId::Count)>
    kNonTerminalLookup = {{
#define NONTERM(name) {#name, NonTerminalId::name},
#include "nonterminals.def"
#undef NONTERM
    }};

NonTerminalId findNonTerminalId(std::string_view name) {
  for (const auto& entry : kNonTerminalLookup) {
    if (entry.name == name) {
      return entry.id;
    }
  }
  return NonTerminalId::Count;
}

std::string tokenLexeme(const Token& tok, std::string_view fallback = "") {
  if (tok.lexeme) {
    return *tok.lexeme;
  }
  return std::string(fallback);
}

template <typename Table> std::string lookupTokenString(TokenType type, const Table& table) {
  const auto it = std::find_if(table.begin(), table.end(),
                               [type](const auto& entry) { return entry.first == type; });
  if (it == table.end()) {
    return "";
  }
  return std::string(it->second);
}

constexpr auto kTypeTokenMap = std::array{
    std::pair{TokenType::KW_INT, std::string_view{"int"}},
    std::pair{TokenType::KW_FLOAT, std::string_view{"float"}},
    std::pair{TokenType::KW_BOOL, std::string_view{"bool"}},
    std::pair{TokenType::KW_CHAR, std::string_view{"char"}},
    std::pair{TokenType::KW_STRING, std::string_view{"string"}},
    std::pair{TokenType::KW_NULL, std::string_view{"null"}},
};

constexpr auto kLiteralTokenMap = std::array{
    std::pair{TokenType::KW_TRUE, std::string_view{"true"}},
    std::pair{TokenType::KW_FALSE, std::string_view{"false"}},
    std::pair{TokenType::KW_NULL, std::string_view{"null"}},
};

constexpr auto kOperatorTokenMap = std::array{
    std::pair{TokenType::PLUS, std::string_view{"+"}},
    std::pair{TokenType::MINUS, std::string_view{"-"}},
    std::pair{TokenType::MUL, std::string_view{"*"}},
    std::pair{TokenType::DIV, std::string_view{"/"}},
    std::pair{TokenType::EQUAL, std::string_view{"=="}},
    std::pair{TokenType::NOT_EQUAL, std::string_view{"!="}},
    std::pair{TokenType::LESS, std::string_view{"<"}},
    std::pair{TokenType::LESS_EQUAL, std::string_view{"<="}},
    std::pair{TokenType::GREATER, std::string_view{">"}},
    std::pair{TokenType::GREATER_EQUAL, std::string_view{">="}},
    std::pair{TokenType::KW_AND, std::string_view{"and"}},
    std::pair{TokenType::KW_OR, std::string_view{"or"}},
    std::pair{TokenType::KW_NOT, std::string_view{"not"}},
};

std::string typeNameFromToken(TokenType type) { return lookupTokenString(type, kTypeTokenMap); }

std::string literalFromToken(const Token& tok) {
  auto literal = lookupTokenString(tok.kind, kLiteralTokenMap);
  if (!literal.empty()) {
    return literal;
  }
  return tokenLexeme(tok);
}

std::string opName(TokenType type) { return lookupTokenString(type, kOperatorTokenMap); }

ast::NodePtr<ast::Expression> makeBinaryExpr(Token opTok, ast::NodePtr<ast::Expression> lhs,
                                             ast::NodePtr<ast::Expression> rhs) {
  auto node = std::make_unique<ast::BinaryExpr>(opName(opTok.kind), opTok.loc);
  node->lhs = std::move(lhs);
  node->rhs = std::move(rhs);
  return node;
}

ast::NodePtr<ast::Expression> makeUnaryExpr(Token opTok, ast::NodePtr<ast::Expression> operand) {
  auto node = std::make_unique<ast::UnaryExpr>(opName(opTok.kind), opTok.loc);
  node->operand = std::move(operand);
  return node;
}

ast::NodePtr<ast::Expression> makeLiteralExpr(const Token& tok, std::string valueOverride = "") {
  auto node = std::make_unique<ast::LiteralExpr>(
      valueOverride.empty() ? literalFromToken(tok) : valueOverride, tok.loc);
  return node;
}

ast::NodePtr<ast::Expression> makeIdentifierExpr(const Token& tok, std::string name) {
  auto node = std::make_unique<ast::IdentifierExpr>(std::move(name), tok.loc);
  return node;
}

} // namespace

Parser::Parser(Lexer& lexerRef) : lexer(lexerRef), grammar(getGrammar()) {
  stack.push_back(StackEntry{0, SemanticValue{}});
}

Token Parser::nextToken() {
  while (true) {
    Token token = lexer.getNextToken();
    if (token.kind == TokenType::WHITESPACE || token.kind == TokenType::COMMENT) {
      continue;
    }
    return token;
  }
}

void Parser::ensureLookahead() {
  if (!lookahead) {
    lookahead = nextToken();
  }
}

Action Parser::actionFor(int state, TokenType terminal) const {
  if (state < 0 || static_cast<std::size_t>(state) >= kGeneratedActionTable.size()) {
    return makeError();
  }
  const auto& row = kGeneratedActionTable[static_cast<std::size_t>(state)];
  return row[static_cast<std::size_t>(terminal)];
}

int Parser::gotoFor(int state, NonTerminalId symbol) const {
  if (state < 0 || static_cast<std::size_t>(state) >= kGeneratedGotoTable.size()) {
    return -1;
  }
  const auto& row = kGeneratedGotoTable[static_cast<std::size_t>(state)];
  return row[static_cast<std::size_t>(symbol)];
}

SemanticValue Parser::performReduction(int ruleIndex, std::vector<SemanticValue>&& children) {
  const auto index = static_cast<std::size_t>(ruleIndex);
  if (index >= grammar.size()) {
    return SemanticValue{};
  }

  const auto& rule = grammar[index];
  const auto lhs = rule.lhs.name;
  const auto count = children.size();

  using SV = SemanticValue;

  auto takeTokenAt = [&](std::size_t idx) { return children[idx].take<Token>(); };
  auto takeASTNodeAt = [&](std::size_t idx) { return children[idx].take<SV::ASTNodePtr>(); };
  auto takeProgramAt = [&](std::size_t idx) { return children[idx].take<SV::ProgramPtr>(); };
  auto takeStatementAt = [&](std::size_t idx) { return children[idx].take<SV::StatementPtr>(); };
  auto takeExpressionAt = [&](std::size_t idx) { return children[idx].take<SV::ExpressionPtr>(); };
  auto takeBlockAt = [&](std::size_t idx) { return children[idx].take<SV::BlockPtr>(); };
  auto takeParameterAt = [&](std::size_t idx) { return children[idx].take<SV::ParameterPtr>(); };
  auto takeTopLevelListAt = [&](std::size_t idx) { return children[idx].take<SV::TopLevelList>(); };
  auto takeStatementListAt = [&](std::size_t idx) {
    return children[idx].take<SV::StatementList>();
  };
  auto takeParameterListAt = [&](std::size_t idx) {
    return children[idx].take<SV::ParameterList>();
  };
  auto takeExpressionListAt = [&](std::size_t idx) {
    return children[idx].take<SV::ExpressionList>();
  };
  auto takeStringAt = [&](std::size_t idx) { return children[idx].take<std::string>(); };
  auto takeOptionalStringAt = [&](std::size_t idx) {
    return children[idx].take<std::optional<std::string>>();
  };

  if (lhs == "AugmentedStart" && count == 1) {
    if (children[0].holds<SV::ProgramPtr>()) {
      return SemanticValue(takeProgramAt(0));
    }
    if (children[0].hasNode()) {
      return SemanticValue(children[0].takeNode());
    }
  }

  if (lhs == "Program" && count >= 1) {
    auto topLevels = count > 0 ? takeTopLevelListAt(0) : SV::TopLevelList{};
    auto program = std::make_unique<ast::Program>();
    program->topLevel.reserve(topLevels.size());
    for (auto& node : topLevels) {
      program->topLevel.push_back(std::move(node));
    }
    return SemanticValue(std::move(program));
  }

  if (lhs == "TopLevelList") {
    if (count == 0) {
      return SemanticValue(SV::TopLevelList{});
    }
    if (count == 2) {
      auto list = takeTopLevelListAt(0);
      if (children[1].holds<SV::ASTNodePtr>()) {
        list.push_back(takeASTNodeAt(1));
      } else if (children[1].holds<SV::StatementPtr>()) {
        SV::ASTNodePtr node = takeStatementAt(1);
        list.push_back(std::move(node));
      }
      return SemanticValue(std::move(list));
    }
  }

  if (lhs == "TopLevel" && count == 1) {
    if (children[0].holds<SV::ASTNodePtr>()) {
      return SemanticValue(takeASTNodeAt(0));
    }
    if (children[0].holds<SV::StatementPtr>()) {
      SV::ASTNodePtr node = takeStatementAt(0);
      return SemanticValue(std::move(node));
    }
  }

  if (lhs == "Import" && count == 3) {
    auto kw = takeTokenAt(0);
    auto ident = takeTokenAt(1);
    (void)takeTokenAt(2); // semicolon
    auto node = std::make_unique<ast::ImportDecl>(tokenLexeme(ident), kw.loc);
    return SemanticValue(SV::ASTNodePtr(std::move(node)));
  }

  if (lhs == "Function" && count == 7) {
    auto fnTok = takeTokenAt(0);
    auto nameTok = takeTokenAt(1);
    (void)takeTokenAt(2); // LPAREN
    auto params = takeParameterListAt(3);
    (void)takeTokenAt(4); // RPAREN
    auto retType = takeOptionalStringAt(5);
    auto body = takeBlockAt(6);

    auto fn = std::make_unique<ast::FunctionDecl>(tokenLexeme(nameTok), fnTok.loc);
    fn->params = std::move(params);
    fn->returnType = std::move(retType);
    fn->body = std::move(body);
    return SemanticValue(SV::ASTNodePtr(std::move(fn)));
  }

  if (lhs == "FunctionReturnTypeOpt") {
    if (count == 0) {
      return SemanticValue(std::optional<std::string>{});
    }
    if (count == 2) {
      (void)takeTokenAt(0);
      auto typeName = takeStringAt(1);
      return SemanticValue(std::optional<std::string>{std::move(typeName)});
    }
  }

  if (lhs == "ParameterListOpt") {
    if (count == 0) {
      return SemanticValue(SV::ParameterList{});
    }
    return SemanticValue(takeParameterListAt(0));
  }

  if (lhs == "ParameterList" && count == 2) {
    auto first = takeParameterAt(0);
    auto tail = takeParameterListAt(1);
    SV::ParameterList list;
    list.push_back(std::move(first));
    for (auto& param : tail) {
      list.push_back(std::move(param));
    }
    return SemanticValue(std::move(list));
  }

  if (lhs == "ParameterTail") {
    if (count == 0) {
      return SemanticValue(SV::ParameterList{});
    }
    if (count == 3) {
      (void)takeTokenAt(0);
      auto param = takeParameterAt(1);
      auto tail = takeParameterListAt(2);
      SV::ParameterList list;
      list.push_back(std::move(param));
      for (auto& entry : tail) {
        list.push_back(std::move(entry));
      }
      return SemanticValue(std::move(list));
    }
  }

  if (lhs == "Parameter" && count == 3) {
    auto nameTok = takeTokenAt(0);
    (void)takeTokenAt(1);
    auto typeName = takeStringAt(2);
    auto param = std::make_unique<ast::Parameter>(tokenLexeme(nameTok), typeName, nameTok.loc);
    return SemanticValue(SV::ParameterPtr(std::move(param)));
  }

  if (lhs == "TypeName" && count == 1) {
    auto tok = takeTokenAt(0);
    auto text = typeNameFromToken(tok.kind);
    if (text.empty()) {
      text = tokenLexeme(tok);
    }
    return SemanticValue(std::move(text));
  }

  if (lhs == "TypeAnnotationOpt") {
    if (count == 0) {
      return SemanticValue(std::optional<std::string>{});
    }
    if (count == 2) {
      (void)takeTokenAt(0);
      auto typeName = takeStringAt(1);
      return SemanticValue(std::optional<std::string>{std::move(typeName)});
    }
  }

  if (lhs == "Block" && count == 3) {
    auto lbrace = takeTokenAt(0);
    auto statements = takeStatementListAt(1);
    (void)takeTokenAt(2);
    auto block = std::make_unique<ast::BlockStmt>(lbrace.loc);
    for (auto& stmt : statements) {
      block->statements.push_back(std::move(stmt));
    }
    return SemanticValue(SV::BlockPtr(std::move(block)));
  }

  if (lhs == "StatementList") {
    if (count == 0) {
      return SemanticValue(SV::StatementList{});
    }
    if (count == 2) {
      auto list = takeStatementListAt(0);
      list.push_back(takeStatementAt(1));
      return SemanticValue(std::move(list));
    }
  }

  if (lhs == "Statement" && count == 1) {
    return SemanticValue(takeStatementAt(0));
  }

  if (lhs == "MatchedStatement") {
    if (count == 1) {
      if (children[0].holds<SV::StatementPtr>()) {
        return SemanticValue(takeStatementAt(0));
      }
      if (children[0].holds<SV::BlockPtr>()) {
        SV::StatementPtr stmt = takeBlockAt(0);
        return SemanticValue(std::move(stmt));
      }
    } else if (count == 2) {
      if (children[0].holds<SV::StatementPtr>()) {
        auto stmt = takeStatementAt(0);
        (void)takeTokenAt(1);
        return SemanticValue(std::move(stmt));
      }
      if (children[0].holds<SV::ExpressionPtr>()) {
        auto expr = takeExpressionAt(0);
        auto semi = takeTokenAt(1);
        auto stmt = std::make_unique<ast::ExpressionStmt>(semi.loc);
        stmt->expr = std::move(expr);
        return SemanticValue(SV::StatementPtr(std::move(stmt)));
      }
    }
  }

  if (lhs == "UnmatchedStatement" && count == 1) {
    return SemanticValue(takeStatementAt(0));
  }

  if (lhs == "Declaration" && count == 5) {
    auto keyword = takeTokenAt(0);
    auto ident = takeTokenAt(1);
    auto typeOpt = takeOptionalStringAt(2);
    (void)takeTokenAt(3);
    auto expr = takeExpressionAt(4);
    auto decl = std::make_unique<ast::DeclarationStmt>(keyword.kind == TokenType::KW_CONST,
                                                       tokenLexeme(ident), keyword.loc);
    decl->typeName = std::move(typeOpt);
    decl->initializer = std::move(expr);
    return SemanticValue(SV::StatementPtr(std::move(decl)));
  }

  if (lhs == "Assignment" && count == 3) {
    auto ident = takeTokenAt(0);
    (void)takeTokenAt(1);
    auto value = takeExpressionAt(2);
    auto stmt = std::make_unique<ast::AssignmentStmt>(tokenLexeme(ident), ident.loc);
    stmt->value = std::move(value);
    return SemanticValue(SV::StatementPtr(std::move(stmt)));
  }

  if (lhs == "IfMatched" && count == 7) {
    auto ifTok = takeTokenAt(0);
    (void)takeTokenAt(1);
    auto condition = takeExpressionAt(2);
    (void)takeTokenAt(3);
    auto thenBranch = takeStatementAt(4);
    (void)takeTokenAt(5);
    auto elseBranch = takeStatementAt(6);
    auto stmt = std::make_unique<ast::IfStmt>(ifTok.loc);
    stmt->condition = std::move(condition);
    stmt->thenBranch = std::move(thenBranch);
    stmt->elseBranch = std::move(elseBranch);
    return SemanticValue(SV::StatementPtr(std::move(stmt)));
  }

  if (lhs == "IfUnmatched") {
    if (count == 5) {
      auto ifTok = takeTokenAt(0);
      (void)takeTokenAt(1);
      auto condition = takeExpressionAt(2);
      (void)takeTokenAt(3);
      auto thenBranch = takeStatementAt(4);
      auto stmt = std::make_unique<ast::IfStmt>(ifTok.loc);
      stmt->condition = std::move(condition);
      stmt->thenBranch = std::move(thenBranch);
      return SemanticValue(SV::StatementPtr(std::move(stmt)));
    }
    if (count == 7) {
      auto ifTok = takeTokenAt(0);
      (void)takeTokenAt(1);
      auto condition = takeExpressionAt(2);
      (void)takeTokenAt(3);
      auto thenBranch = takeStatementAt(4);
      (void)takeTokenAt(5);
      auto elseBranch = takeStatementAt(6);
      auto stmt = std::make_unique<ast::IfStmt>(ifTok.loc);
      stmt->condition = std::move(condition);
      stmt->thenBranch = std::move(thenBranch);
      stmt->elseBranch = std::move(elseBranch);
      return SemanticValue(SV::StatementPtr(std::move(stmt)));
    }
  }

  if (lhs == "WhileStatement" && count == 5) {
    auto kw = takeTokenAt(0);
    (void)takeTokenAt(1);
    auto condition = takeExpressionAt(2);
    (void)takeTokenAt(3);
    auto body = takeStatementAt(4);
    auto stmt = std::make_unique<ast::WhileStmt>(kw.loc);
    stmt->condition = std::move(condition);
    stmt->body = std::move(body);
    return SemanticValue(SV::StatementPtr(std::move(stmt)));
  }

  if (lhs == "ForStatement" && count == 5) {
    auto kw = takeTokenAt(0);
    (void)takeTokenAt(1);
    auto condition = takeExpressionAt(2);
    (void)takeTokenAt(3);
    auto body = takeStatementAt(4);
    auto stmt = std::make_unique<ast::ForStmt>(kw.loc);
    stmt->condition = std::move(condition);
    stmt->body = std::move(body);
    return SemanticValue(SV::StatementPtr(std::move(stmt)));
  }

  if (lhs == "ReturnStatement" && count == 2) {
    auto kw = takeTokenAt(0);
    auto value = takeExpressionAt(1);
    auto stmt = std::make_unique<ast::ReturnStmt>(kw.loc);
    stmt->value = std::move(value);
    return SemanticValue(SV::StatementPtr(std::move(stmt)));
  }

  if (lhs == "ReturnExpressionOpt") {
    if (count == 0) {
      return SemanticValue(SV::ExpressionPtr{});
    }
    if (count == 1) {
      return SemanticValue(takeExpressionAt(0));
    }
  }

  if (lhs == "BreakStatement" && count == 1) {
    auto tok = takeTokenAt(0);
    auto stmt = std::make_unique<ast::BreakStmt>(tok.loc);
    return SemanticValue(SV::StatementPtr(std::move(stmt)));
  }

  if (lhs == "ContinueStatement" && count == 1) {
    auto tok = takeTokenAt(0);
    auto stmt = std::make_unique<ast::ContinueStmt>(tok.loc);
    return SemanticValue(SV::StatementPtr(std::move(stmt)));
  }

  if (lhs == "CallExpression" && count == 4) {
    auto ident = takeTokenAt(0);
    (void)takeTokenAt(1);
    auto args = takeExpressionListAt(2);
    (void)takeTokenAt(3);
    auto call = std::make_unique<ast::CallExpr>(tokenLexeme(ident), ident.loc);
    for (auto& arg : args) {
      call->args.push_back(std::move(arg));
    }
    return SemanticValue(SV::ExpressionPtr(std::move(call)));
  }

  if (lhs == "ArgumentListOpt") {
    if (count == 0) {
      return SemanticValue(SV::ExpressionList{});
    }
    return SemanticValue(takeExpressionListAt(0));
  }

  if (lhs == "ArgumentList" && count == 2) {
    auto first = takeExpressionAt(0);
    auto tail = takeExpressionListAt(1);
    SV::ExpressionList list;
    list.push_back(std::move(first));
    for (auto& expr : tail) {
      list.push_back(std::move(expr));
    }
    return SemanticValue(std::move(list));
  }

  if (lhs == "ArgumentTail") {
    if (count == 0) {
      return SemanticValue(SV::ExpressionList{});
    }
    if (count == 3) {
      (void)takeTokenAt(0);
      auto expr = takeExpressionAt(1);
      auto tail = takeExpressionListAt(2);
      SV::ExpressionList list;
      list.push_back(std::move(expr));
      for (auto& entry : tail) {
        list.push_back(std::move(entry));
      }
      return SemanticValue(std::move(list));
    }
  }

  if (lhs == "Expression" && count == 1) {
    return SemanticValue(takeExpressionAt(0));
  }

  auto handleBinary = [&](std::size_t lhsIdx, std::size_t opIdx, std::size_t rhsIdx) {
    auto left = takeExpressionAt(lhsIdx);
    auto op = takeTokenAt(opIdx);
    auto right = takeExpressionAt(rhsIdx);
    return SemanticValue(makeBinaryExpr(op, std::move(left), std::move(right)));
  };

  if (lhs == "LogicalOr") {
    if (count == 1) {
      return SemanticValue(takeExpressionAt(0));
    }
    if (count == 3) {
      return handleBinary(0, 1, 2);
    }
  }

  if (lhs == "LogicalAnd") {
    if (count == 1) {
      return SemanticValue(takeExpressionAt(0));
    }
    if (count == 3) {
      return handleBinary(0, 1, 2);
    }
  }

  if (lhs == "Equality") {
    if (count == 1) {
      return SemanticValue(takeExpressionAt(0));
    }
    if (count == 3) {
      return handleBinary(0, 1, 2);
    }
  }

  if (lhs == "Comparison") {
    if (count == 1) {
      return SemanticValue(takeExpressionAt(0));
    }
    if (count == 3) {
      return handleBinary(0, 1, 2);
    }
  }

  if (lhs == "Term") {
    if (count == 1) {
      return SemanticValue(takeExpressionAt(0));
    }
    if (count == 3) {
      return handleBinary(0, 1, 2);
    }
  }

  if (lhs == "Factor") {
    if (count == 1) {
      return SemanticValue(takeExpressionAt(0));
    }
    if (count == 3) {
      return handleBinary(0, 1, 2);
    }
  }

  if (lhs == "Unary") {
    if (count == 1) {
      return SemanticValue(takeExpressionAt(0));
    }
    if (count == 2) {
      auto op = takeTokenAt(0);
      auto operand = takeExpressionAt(1);
      return SemanticValue(makeUnaryExpr(op, std::move(operand)));
    }
  }

  if (lhs == "Primary") {
    if (count == 1) {
      if (children[0].holds<Token>()) {
        auto tok = takeTokenAt(0);
        switch (tok.kind) {
        case TokenType::LIT_NUM:
        case TokenType::LIT_STRING:
        case TokenType::KW_TRUE:
        case TokenType::KW_FALSE:
        case TokenType::KW_NULL:
          return SemanticValue(makeLiteralExpr(tok));
        case TokenType::IDENTIFIER:
          return SemanticValue(makeIdentifierExpr(tok, tokenLexeme(tok)));
        default:
          break;
        }
      }
      if (children[0].holds<SV::ExpressionPtr>()) {
        return SemanticValue(takeExpressionAt(0));
      }
    }
    if (count == 3) {
      (void)takeTokenAt(0);
      auto expr = takeExpressionAt(1);
      (void)takeTokenAt(2);
      return SemanticValue(std::move(expr));
    }
  }

  return SemanticValue{};
}

ParseResult Parser::makeFailure() const { return ParseResult{}; }

ParseResult Parser::parse() {
  ParseResult result{};
  while (true) {
    ensureLookahead();
    const int state = stack.back().state;
    const TokenType lookaheadKind = lookahead ? lookahead->kind : TokenType::ENDF;
    Action action = actionFor(state, lookaheadKind);

    switch (action.type) {
    case ActionType::Shift: {
      if (!lookahead) {
        return makeFailure();
      }
      stack.push_back(StackEntry{action.value, SemanticValue(std::move(*lookahead))});
      lookahead.reset();
      break;
    }
    case ActionType::Reduce: {
      if (action.value < 0 || static_cast<std::size_t>(action.value) >= grammar.size()) {
        return makeFailure();
      }
      const auto& rule = grammar[static_cast<std::size_t>(action.value)];
      std::vector<SemanticValue> children;
      const std::size_t rhsSize = rule.rhs.size();
      children.reserve(rhsSize);
      for (std::size_t i = 0; i < rhsSize; ++i) {
        children.push_back(std::move(stack.back().value));
        stack.pop_back();
        if (stack.empty()) {
          return makeFailure();
        }
      }
      std::reverse(children.begin(), children.end());
      const NonTerminalId lhsId = findNonTerminalId(rule.lhs.name);
      if (lhsId == NonTerminalId::Count || stack.empty()) {
        return makeFailure();
      }
      SemanticValue reduced = performReduction(static_cast<int>(action.value), std::move(children));
      const int gotoState = gotoFor(stack.back().state, lhsId);
      if (gotoState < 0) {
        return makeFailure();
      }
      stack.push_back(StackEntry{gotoState, std::move(reduced)});
      break;
    }
    case ActionType::Accept: {
      result.success = true;
      if (!stack.empty() && stack.back().value.hasNode()) {
        result.root = stack.back().value.takeNode();
      }
      return result;
    }
    case ActionType::Error:
    default:
      return makeFailure();
    }
  }
}

ParseResult parseString(std::string source) {
  Lexer lexer(std::move(source));
  Parser parser(lexer);
  return parser.parse();
}

ParseResult parseFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return ParseResult{};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parseString(buffer.str());
}

} // namespace dhad::parser
