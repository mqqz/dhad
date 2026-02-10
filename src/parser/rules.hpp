#pragma once

#include "../lexer/tokens.hpp"
#include "tables.hpp"

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace dhad::parser {

#define DHAD_PARSER_FOR_EACH_ACTION(F)                                                             \
  F(AugmentedStartProgram)                                                                         \
  F(ProgramFinish)                                                                                 \
  F(TopLevelListAppend)                                                                            \
  F(TopLevelListEmpty)                                                                             \
  F(TopLevelImport)                                                                                \
  F(TopLevelTypeDecl)                                                                              \
  F(TopLevelFunction)                                                                              \
  F(TopLevelStatement)                                                                             \
  F(ImportDecl)                                                                                    \
  F(TypeDeclStruct)                                                                                \
  F(TypeDeclEnum)                                                                                  \
  F(StructDecl)                                                                                    \
  F(StructFieldListSome)                                                                           \
  F(StructFieldListNone)                                                                           \
  F(StructFieldListBuild)                                                                          \
  F(StructFieldTailAppend)                                                                          \
  F(StructFieldTailEmpty)                                                                           \
  F(StructField)                                                                                   \
  F(EnumDecl)                                                                                      \
  F(EnumVariantListSome)                                                                           \
  F(EnumVariantListNone)                                                                           \
  F(EnumVariantListBuild)                                                                          \
  F(EnumVariantTailAppend)                                                                          \
  F(EnumVariantTailEmpty)                                                                           \
  F(EnumVariant)                                                                                   \
  F(EnumVariantPayloadSome)                                                                        \
  F(EnumVariantPayloadNone)                                                                        \
  F(FunctionDecl)                                                                                  \
  F(FunctionReturnTypeSome)                                                                        \
  F(FunctionReturnTypeNone)                                                                        \
  F(ParameterListSome)                                                                             \
  F(ParameterListNone)                                                                             \
  F(ParameterListBuild)                                                                            \
  F(ParameterTailAppend)                                                                           \
  F(ParameterTailEmpty)                                                                            \
  F(ParameterDecl)                                                                                 \
  F(TypeNameKeyword)                                                                               \
  F(BlockBuild)                                                                                    \
  F(StatementListAppend)                                                                           \
  F(StatementListEmpty)                                                                            \
  F(StatementFromMatched)                                                                          \
  F(StatementFromUnmatched)                                                                        \
  F(PassStatementSemicolon)                                                                        \
  F(PassStatement)                                                                                 \
  F(ExpressionStatement)                                                                           \
  F(BlockStatement)                                                                                \
  F(DeclarationStmt)                                                                               \
  F(TypeAnnotationSome)                                                                            \
  F(TypeAnnotationNone)                                                                            \
  F(AssignmentStmt)                                                                                \
  F(IfMatchedFull)                                                                                 \
  F(IfUnmatchedNoElse)                                                                             \
  F(IfUnmatchedWithElse)                                                                           \
  F(WhileStmt)                                                                                     \
  F(ForStmt)                                                                                       \
  F(ReturnStmt)                                                                                    \
  F(ReturnExprSome)                                                                                \
  F(ReturnExprNone)                                                                                \
  F(BreakStmt)                                                                                     \
  F(ContinueStmt)                                                                                  \
  F(CallExpression)                                                                                \
  F(ArgumentListSome)                                                                              \
  F(ArgumentListNone)                                                                              \
  F(ArgumentListBuild)                                                                             \
  F(ArgumentTailAppend)                                                                            \
  F(ArgumentTailEmpty)                                                                             \
  F(ExpressionPass)                                                                                \
  F(FieldAccess)                                                                                   \
  F(BinaryExpr)                                                                                    \
  F(UnaryPrefix)                                                                                   \
  F(PrimaryLiteral)                                                                                \
  F(PrimaryIdentifier)                                                                             \
  F(PrimaryGrouping)                                                                               \
  F(ArrayLiteral)                                                                                  \
  F(StructLiteral)                                                                                 \
  F(StructFieldInitListSome)                                                                       \
  F(StructFieldInitListNone)                                                                       \
  F(StructFieldInitListBuild)                                                                      \
  F(StructFieldInitTailAppend)                                                                     \
  F(StructFieldInitTailEmpty)                                                                      \
  F(StructFieldInit)                                                                               \
  F(TypeNameUnion)                                                                                 \
  F(TypeNameFromPrimary)                                                                           \
  F(TypeNameIdentifier)                                                                            \
  F(TypePrimaryArray)                                                                              \
  F(TypePrimaryTuple)                                                                              \
  F(TypePrimaryGrouped)                                                                            \
  F(TypeTupleBuild)                                                                                \
  F(TypeTupleTailAppend)                                                                           \
  F(TypeTupleTailEmpty)

enum class ActionId : std::uint8_t {
#define DECLARE_ACTION(name) name,
  DHAD_PARSER_FOR_EACH_ACTION(DECLARE_ACTION)
#undef DECLARE_ACTION
      Count
};

struct NonTerminal {
  constexpr NonTerminal() = default;
  constexpr explicit NonTerminal(std::string_view identifier) : name(identifier) {}

  std::string_view name;
};

struct Terminal {
  constexpr Terminal() = default;
  constexpr explicit Terminal(TokenType token) : kind(token) {}

  TokenType kind{TokenType::INVALID};
};

using Symbol = std::variant<Terminal, NonTerminal>;

inline Symbol makeSymbol(TokenType token) { return Symbol{Terminal{token}}; }
inline Symbol makeSymbol(const NonTerminal& nt) { return Symbol{nt}; }

struct ProductionRule {
  NonTerminal lhs;
  std::vector<Symbol> rhs;

  ProductionRule() = default;
  ProductionRule(NonTerminal left, std::vector<Symbol> symbols)
      : lhs(left), rhs(std::move(symbols)) {}
  ProductionRule(NonTerminal left, std::initializer_list<Symbol> symbols)
      : lhs(left), rhs(symbols) {}
};

const std::vector<ProductionRule>& getGrammar();
const std::vector<ActionId>& getRuleActions();
const std::vector<NonTerminalId>& getRuleLHSIds();

} // namespace dhad::parser
