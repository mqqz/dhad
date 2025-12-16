#include "builders.hpp"

#include "../lexer/tokens.hpp"

namespace dhad::ast::build {

std::string tokenLexeme(const Token& tok, std::string_view fallback) {
  if (tok.lexeme) {
    return *tok.lexeme;
  }
  return std::string(fallback);
}

std::string tokenText(const Token& tok) {
  const auto canonical = dhad::lexer::tokenCanonicalLexeme(tok.kind);
  if (!canonical.empty()) {
    return std::string(canonical);
  }
  return tokenLexeme(tok);
}

NodePtr<Expression> makeBinaryExpr(const Token& opTok, NodePtr<Expression> lhs,
                                   NodePtr<Expression> rhs) {
  auto node = std::make_unique<BinaryExpr>(tokenText(opTok), opTok.loc);
  node->lhs = std::move(lhs);
  node->rhs = std::move(rhs);
  return node;
}

NodePtr<Expression> makeUnaryExpr(const Token& opTok, NodePtr<Expression> operand) {
  auto node = std::make_unique<UnaryExpr>(tokenText(opTok), opTok.loc);
  node->operand = std::move(operand);
  return node;
}

NodePtr<Expression> makeLiteralExpr(const Token& tok) { return makeLiteralExpr(tok, ""); }

NodePtr<Expression> makeLiteralExpr(const Token& tok, std::string valueOverride) {
  auto value = valueOverride.empty() ? tokenText(tok) : std::move(valueOverride);
  return std::make_unique<LiteralExpr>(std::move(value), tok.loc);
}

NodePtr<Expression> makeIdentifierExpr(const Token& tok, std::string name) {
  return std::make_unique<IdentifierExpr>(std::move(name), tok.loc);
}

} // namespace dhad::ast::build
