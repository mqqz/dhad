#pragma once

#include "ast.hpp"

#include <string>
#include <string_view>

namespace dhad::ast::build {

std::string tokenLexeme(const Token& tok, std::string_view fallback = {});
std::string tokenText(const Token& tok);

NodePtr<Expression> makeBinaryExpr(const Token& opTok, NodePtr<Expression> lhs,
                                   NodePtr<Expression> rhs);
NodePtr<Expression> makeUnaryExpr(const Token& opTok, NodePtr<Expression> operand);
NodePtr<Expression> makeLiteralExpr(const Token& tok);
NodePtr<Expression> makeLiteralExpr(const Token& tok, std::string valueOverride);
NodePtr<Expression> makeIdentifierExpr(const Token& tok, std::string name);

} // namespace dhad::ast::build
