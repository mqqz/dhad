#include "builders.hpp"

#include "../lexer/tokens.hpp"

#include <optional>

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

std::optional<BinaryOp> tokenToBinaryOp(TokenType kind) {
  switch (kind) {
  case TokenType::PLUS:
    return BinaryOp::Add;
  case TokenType::MINUS:
    return BinaryOp::Sub;
  case TokenType::MUL:
    return BinaryOp::Mul;
  case TokenType::DIV:
    return BinaryOp::Div;
  case TokenType::KW_AND:
    return BinaryOp::And;
  case TokenType::KW_OR:
    return BinaryOp::Or;
  case TokenType::EQUAL:
    return BinaryOp::Eq;
  case TokenType::NOT_EQUAL:
    return BinaryOp::Ne;
  case TokenType::LESS:
    return BinaryOp::Lt;
  case TokenType::LESS_EQUAL:
    return BinaryOp::Le;
  case TokenType::GREATER:
    return BinaryOp::Gt;
  case TokenType::GREATER_EQUAL:
    return BinaryOp::Ge;
  default:
    return std::nullopt;
  }
}

std::optional<UnaryOp> tokenToUnaryOp(TokenType kind) {
  switch (kind) {
  case TokenType::MINUS:
    return UnaryOp::Negate;
  case TokenType::KW_NOT:
    return UnaryOp::Not;
  default:
    return std::nullopt;
  }
}

NodePtr<Expression> makeBinaryExpr(const Token& opTok, NodePtr<Expression> lhs,
                                   NodePtr<Expression> rhs) {
  auto op = tokenToBinaryOp(opTok.kind).value_or(BinaryOp::Add);
  auto node = std::make_unique<BinaryExpr>(op, opTok.loc);
  node->lhs = std::move(lhs);
  node->rhs = std::move(rhs);
  return node;
}

NodePtr<Expression> makeUnaryExpr(const Token& opTok, NodePtr<Expression> operand) {
  auto op = tokenToUnaryOp(opTok.kind).value_or(UnaryOp::Not);
  auto node = std::make_unique<UnaryExpr>(op, opTok.loc);
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
