#pragma once

#include "../ast/ast.hpp"
#include "../lexer/lexer.hpp"

#include "rules.hpp"
#include "tables.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dhad::parser {

struct SemanticValue {
  using ASTNodePtr = ast::NodePtr<ast::ASTNode>;
  using ProgramPtr = ast::NodePtr<ast::Program>;
  using StatementPtr = ast::NodePtr<ast::Statement>;
  using ExpressionPtr = ast::NodePtr<ast::Expression>;
  using BlockPtr = ast::NodePtr<ast::BlockStmt>;
  using ParameterPtr = ast::NodePtr<ast::Parameter>;

  using TopLevelList = std::vector<ASTNodePtr>;
  using StatementList = std::vector<StatementPtr>;
  using ParameterList = std::vector<ParameterPtr>;
  using ExpressionList = std::vector<ExpressionPtr>;

  using Storage =
      std::variant<std::monostate, Token, std::string, std::optional<std::string>, ASTNodePtr,
                   ProgramPtr, StatementPtr, ExpressionPtr, BlockPtr, ParameterPtr, TopLevelList,
                   StatementList, ParameterList, ExpressionList>;

  Storage storage;

  SemanticValue() = default;
  explicit SemanticValue(Token token) : storage(std::move(token)) {}
  explicit SemanticValue(std::string text) : storage(std::move(text)) {}
  explicit SemanticValue(std::optional<std::string> opt) : storage(std::move(opt)) {}
  explicit SemanticValue(ASTNodePtr node) : storage(std::move(node)) {}
  explicit SemanticValue(ProgramPtr node) : storage(std::move(node)) {}
  explicit SemanticValue(StatementPtr node) : storage(std::move(node)) {}
  explicit SemanticValue(ExpressionPtr node) : storage(std::move(node)) {}
  explicit SemanticValue(BlockPtr node) : storage(std::move(node)) {}
  explicit SemanticValue(ParameterPtr node) : storage(std::move(node)) {}
  explicit SemanticValue(TopLevelList list) : storage(std::move(list)) {}
  explicit SemanticValue(StatementList list) : storage(std::move(list)) {}
  explicit SemanticValue(ParameterList list) : storage(std::move(list)) {}
  explicit SemanticValue(ExpressionList list) : storage(std::move(list)) {}

  template <typename T> [[nodiscard]] bool holds() const {
    return std::holds_alternative<T>(storage);
  }

  template <typename T> T take() {
    if (!std::holds_alternative<T>(storage)) {
      return T{};
    }
    T value = std::move(std::get<T>(storage));
    storage.emplace<std::monostate>();
    return value;
  }

  [[nodiscard]] bool hasNode() const {
    return holds<ASTNodePtr>() || holds<ProgramPtr>() || holds<StatementPtr>() ||
           holds<ExpressionPtr>() || holds<BlockPtr>() || holds<ParameterPtr>();
  }

  ASTNodePtr takeNode();
};

inline SemanticValue::ASTNodePtr SemanticValue::takeNode() {
  if (holds<ASTNodePtr>()) {
    return take<ASTNodePtr>();
  }
  if (holds<ProgramPtr>()) {
    return take<ProgramPtr>();
  }
  if (holds<StatementPtr>()) {
    return take<StatementPtr>();
  }
  if (holds<ExpressionPtr>()) {
    return take<ExpressionPtr>();
  }
  if (holds<BlockPtr>()) {
    return take<BlockPtr>();
  }
  if (holds<ParameterPtr>()) {
    return take<ParameterPtr>();
  }
  return nullptr;
}

struct ParseResult {
  bool success{false};
  ast::NodePtr<ast::ASTNode> root;
};

class Parser {
public:
  explicit Parser(Lexer& lexer);

  ParseResult parse();

private:
  struct StackEntry {
    int state;
    SemanticValue value;
  };

  Token nextToken();
  void ensureLookahead();
  [[nodiscard]] Action actionFor(int state, TokenType terminal) const;
  [[nodiscard]] int gotoFor(int state, NonTerminalId symbol) const;
  [[nodiscard]] ParseResult makeFailure() const;
  SemanticValue performReduction(int ruleIndex, std::vector<SemanticValue>&& children);

  Lexer& lexer;
  const std::vector<ProductionRule>& grammar;
  const std::vector<ActionId>& ruleActions;
  const std::vector<NonTerminalId>& ruleLHSIds;
  std::vector<StackEntry> stack;
  std::optional<Token> lookahead;
};

SemanticValue applyAction(ActionId action, std::vector<SemanticValue>&& children);

ParseResult parseString(std::string source);
ParseResult parseFile(const std::string& path);

} // namespace dhad::parser
