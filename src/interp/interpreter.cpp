#include "interp/interpreter.hpp"

#include "../std/identifiers.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>

namespace dhad::interp {

namespace {

constexpr std::string_view kEntryFunctionName = u8"بداية";

struct Binding {
  Value value;
  bool isConst{false};
};

struct NumericValue {
  bool isFloat{false};
  double floatValue{0.0};
  int64_t intValue{0};
};

struct ExecSignal {
  enum class Kind {
    None,
    Return,
    Break,
    Continue,
    Error,
  };

  Kind kind{Kind::None};
  Value value = Value::makeNull();

  static ExecSignal none() { return ExecSignal{}; }
  static ExecSignal makeReturn(Value value) {
    ExecSignal signal;
    signal.kind = Kind::Return;
    signal.value = std::move(value);
    return signal;
  }
  static ExecSignal makeBreak() {
    ExecSignal signal;
    signal.kind = Kind::Break;
    return signal;
  }
  static ExecSignal makeContinue() {
    ExecSignal signal;
    signal.kind = Kind::Continue;
    return signal;
  }
  static ExecSignal makeError() {
    ExecSignal signal;
    signal.kind = Kind::Error;
    return signal;
  }
};

class InterpreterImpl {
public:
  RunResult run(const ast::Program& program);

private:
  using Scope = std::unordered_map<std::string, Binding>;

  Value evalExpression(const ast::Expression& expr);
  ExecSignal executeStatement(const ast::Statement& stmt);
  ExecSignal executeBlock(const ast::BlockStmt& block);
  Value callFunction(std::string_view name, const std::vector<Value>& args,
                     const SourceLocation& callLoc);

  Value evalBinary(const ast::BinaryExpr& expr);
  Value evalUnary(const ast::UnaryExpr& expr);
  Value evalLiteral(const ast::LiteralExpr& expr);
  Value evalIdentifier(const ast::IdentifierExpr& expr);
  Value evalFieldAccess(const ast::FieldAccessExpr& expr);
  Value evalIndex(const ast::IndexExpr& expr);
  Value evalCall(const ast::CallExpr& expr);
  Value evalArrayLiteral(const ast::ArrayLiteralExpr& expr);
  Value evalStructLiteral(const ast::StructLiteralExpr& expr);

  std::optional<NumericValue> asNumeric(const Value& value, const SourceLocation& loc);
  std::optional<int64_t> asIntIndex(const Value& value, const SourceLocation& loc);
  std::optional<bool> asBool(const Value& value, const SourceLocation& loc);
  std::string asPrintableString(const Value& value);

  void pushScope();
  void popScope();
  bool declareBinding(const std::string& name, Value value, bool isConst, const SourceLocation& loc);
  Binding* lookupBinding(const std::string& name);
  const Binding* lookupBinding(const std::string& name) const;

  bool valuesEqual(const Value& lhs, const Value& rhs) const;
  std::optional<Value> promoteNumeric(const NumericValue& lhs, const NumericValue& rhs,
                                      ast::BinaryOp op, const SourceLocation& loc);

  void reportError(const SourceLocation& loc, std::string message);
  [[nodiscard]] bool hasError() const { return !errors_.empty(); }
  static bool normalizeNumericLiteral(std::string_view in, std::string& out);
  static std::string stripQuotes(std::string text);

  std::unordered_map<std::string, const ast::FunctionDecl*> functions_;
  std::unordered_map<std::string, const ast::StructDecl*> structDecls_;
  std::vector<Scope> scopes_;
  std::vector<RuntimeError> errors_;
  std::string stdoutBuffer_;
};

Value InterpreterImpl::evalExpression(const ast::Expression& expr) {
  if (const auto* node = llvm::dyn_cast<ast::BinaryExpr>(&expr)) {
    return evalBinary(*node);
  }
  if (const auto* node = llvm::dyn_cast<ast::UnaryExpr>(&expr)) {
    return evalUnary(*node);
  }
  if (const auto* node = llvm::dyn_cast<ast::LiteralExpr>(&expr)) {
    return evalLiteral(*node);
  }
  if (const auto* node = llvm::dyn_cast<ast::IdentifierExpr>(&expr)) {
    return evalIdentifier(*node);
  }
  if (const auto* node = llvm::dyn_cast<ast::FieldAccessExpr>(&expr)) {
    return evalFieldAccess(*node);
  }
  if (const auto* node = llvm::dyn_cast<ast::IndexExpr>(&expr)) {
    return evalIndex(*node);
  }
  if (const auto* node = llvm::dyn_cast<ast::CallExpr>(&expr)) {
    return evalCall(*node);
  }
  if (const auto* node = llvm::dyn_cast<ast::ArrayLiteralExpr>(&expr)) {
    return evalArrayLiteral(*node);
  }
  if (const auto* node = llvm::dyn_cast<ast::StructLiteralExpr>(&expr)) {
    return evalStructLiteral(*node);
  }

  reportError(expr.getLocation(), "Unsupported expression");
  return Value::makeNull();
}

ExecSignal InterpreterImpl::executeBlock(const ast::BlockStmt& block) {
  pushScope();
  for (const auto& stmt : block.statements) {
    if (!stmt) {
      continue;
    }
    auto signal = executeStatement(*stmt);
    if (signal.kind != ExecSignal::Kind::None) {
      popScope();
      return signal;
    }
  }
  popScope();
  return ExecSignal::none();
}

ExecSignal InterpreterImpl::executeStatement(const ast::Statement& stmt) {
  if (const auto* block = llvm::dyn_cast<ast::BlockStmt>(&stmt)) {
    return executeBlock(*block);
  }

  if (const auto* decl = llvm::dyn_cast<ast::DeclarationStmt>(&stmt)) {
    if (!decl->initializer) {
      reportError(decl->getLocation(), "Declaration missing initializer");
      return ExecSignal::makeError();
    }
    Value init = evalExpression(*decl->initializer);
    if (hasError()) {
      return ExecSignal::makeError();
    }
    if (!declareBinding(decl->name, std::move(init), decl->isConst, decl->getLocation())) {
      return ExecSignal::makeError();
    }
    return ExecSignal::none();
  }

  if (const auto* assign = llvm::dyn_cast<ast::AssignmentStmt>(&stmt)) {
    if (!assign->value) {
      reportError(assign->getLocation(), "Assignment missing value");
      return ExecSignal::makeError();
    }
    Binding* binding = lookupBinding(assign->target);
    if (!binding) {
      reportError(assign->getLocation(), "Unknown identifier '" + assign->target + "'");
      return ExecSignal::makeError();
    }
    if (binding->isConst) {
      reportError(assign->getLocation(), "Cannot assign to constant '" + assign->target + "'");
      return ExecSignal::makeError();
    }
    binding->value = evalExpression(*assign->value);
    if (hasError()) {
      return ExecSignal::makeError();
    }
    return ExecSignal::none();
  }

  if (const auto* assign = llvm::dyn_cast<ast::IndexAssignmentStmt>(&stmt)) {
    if (!assign->index || !assign->value) {
      reportError(assign->getLocation(), "Index assignment requires index and value");
      return ExecSignal::makeError();
    }
    Binding* binding = lookupBinding(assign->target);
    if (!binding) {
      reportError(assign->getLocation(), "Unknown identifier '" + assign->target + "'");
      return ExecSignal::makeError();
    }
    if (binding->isConst) {
      reportError(assign->getLocation(), "Cannot assign to constant '" + assign->target + "'");
      return ExecSignal::makeError();
    }
    if (binding->value.kind != Value::Kind::Array || !binding->value.arrayValue) {
      reportError(assign->getLocation(), "Index assignment target must be an array");
      return ExecSignal::makeError();
    }
    Value indexValue = evalExpression(*assign->index);
    auto index = asIntIndex(indexValue, assign->index->getLocation());
    if (!index) {
      return ExecSignal::makeError();
    }
    auto& elements = binding->value.arrayValue->elements;
    if (*index < 0 || static_cast<std::size_t>(*index) >= elements.size()) {
      reportError(assign->index->getLocation(), "Array index out of bounds");
      return ExecSignal::makeError();
    }
    elements[static_cast<std::size_t>(*index)] = evalExpression(*assign->value);
    if (hasError()) {
      return ExecSignal::makeError();
    }
    return ExecSignal::none();
  }

  if (const auto* ifStmt = llvm::dyn_cast<ast::IfStmt>(&stmt)) {
    if (!ifStmt->condition || !ifStmt->thenBranch) {
      reportError(ifStmt->getLocation(), "Malformed if statement");
      return ExecSignal::makeError();
    }
    Value cond = evalExpression(*ifStmt->condition);
    auto condition = asBool(cond, ifStmt->condition->getLocation());
    if (!condition) {
      return ExecSignal::makeError();
    }
    if (*condition) {
      return executeStatement(*ifStmt->thenBranch);
    }
    if (ifStmt->elseBranch) {
      return executeStatement(*ifStmt->elseBranch);
    }
    return ExecSignal::none();
  }

  if (const auto* whileStmt = llvm::dyn_cast<ast::WhileStmt>(&stmt)) {
    if (!whileStmt->condition || !whileStmt->body) {
      reportError(whileStmt->getLocation(), "Malformed while statement");
      return ExecSignal::makeError();
    }
    while (true) {
      Value cond = evalExpression(*whileStmt->condition);
      auto condition = asBool(cond, whileStmt->condition->getLocation());
      if (!condition) {
        return ExecSignal::makeError();
      }
      if (!*condition) {
        break;
      }
      auto signal = executeStatement(*whileStmt->body);
      if (signal.kind == ExecSignal::Kind::None) {
        continue;
      }
      if (signal.kind == ExecSignal::Kind::Continue) {
        continue;
      }
      if (signal.kind == ExecSignal::Kind::Break) {
        break;
      }
      return signal;
    }
    return ExecSignal::none();
  }

  if (const auto* forStmt = llvm::dyn_cast<ast::ForStmt>(&stmt)) {
    if (!forStmt->condition || !forStmt->body) {
      reportError(forStmt->getLocation(), "Malformed for statement");
      return ExecSignal::makeError();
    }
    while (true) {
      Value cond = evalExpression(*forStmt->condition);
      auto condition = asBool(cond, forStmt->condition->getLocation());
      if (!condition) {
        return ExecSignal::makeError();
      }
      if (!*condition) {
        break;
      }
      auto signal = executeStatement(*forStmt->body);
      if (signal.kind == ExecSignal::Kind::None) {
        continue;
      }
      if (signal.kind == ExecSignal::Kind::Continue) {
        continue;
      }
      if (signal.kind == ExecSignal::Kind::Break) {
        break;
      }
      return signal;
    }
    return ExecSignal::none();
  }

  if (const auto* returnStmt = llvm::dyn_cast<ast::ReturnStmt>(&stmt)) {
    if (!returnStmt->value) {
      return ExecSignal::makeReturn(Value::makeNull());
    }
    return ExecSignal::makeReturn(evalExpression(*returnStmt->value));
  }

  if (llvm::isa<ast::BreakStmt>(&stmt)) {
    return ExecSignal::makeBreak();
  }
  if (llvm::isa<ast::ContinueStmt>(&stmt)) {
    return ExecSignal::makeContinue();
  }

  if (const auto* exprStmt = llvm::dyn_cast<ast::ExpressionStmt>(&stmt)) {
    if (exprStmt->expr) {
      (void)evalExpression(*exprStmt->expr);
    }
    if (hasError()) {
      return ExecSignal::makeError();
    }
    return ExecSignal::none();
  }

  reportError(stmt.getLocation(), "Unsupported statement");
  return ExecSignal::makeError();
}

Value InterpreterImpl::callFunction(std::string_view name, const std::vector<Value>& args,
                                    const SourceLocation& callLoc) {
  if (name == identifiers::kPrint || name == identifiers::kStdPrint) {
    if (args.empty()) {
      reportError(callLoc, "Function 'اطبع' expects 1 argument but got 0");
      return Value::makeNull();
    }
    stdoutBuffer_ += asPrintableString(args.front());
    stdoutBuffer_ += "\n";
    return Value::makeInt(0);
  }

  auto it = functions_.find(std::string(name));
  if (it == functions_.end()) {
    reportError(callLoc, "Unknown function '" + std::string(name) + "'");
    return Value::makeNull();
  }

  const ast::FunctionDecl* fn = it->second;
  if (args.size() != fn->params.size()) {
    reportError(callLoc, "Function '" + fn->name + "' expects " + std::to_string(fn->params.size()) +
                             " arguments but got " + std::to_string(args.size()));
    return Value::makeNull();
  }

  pushScope();
  for (std::size_t i = 0; i < fn->params.size(); ++i) {
    if (!declareBinding(fn->params[i]->name, args[i], false, callLoc)) {
      popScope();
      return Value::makeNull();
    }
  }

  if (!fn->body) {
    popScope();
    return Value::makeNull();
  }
  ExecSignal signal = executeBlock(*fn->body);
  popScope();

  if (signal.kind == ExecSignal::Kind::Return) {
    return signal.value;
  }
  if (signal.kind == ExecSignal::Kind::None) {
    return Value::makeNull();
  }
  if (signal.kind == ExecSignal::Kind::Break || signal.kind == ExecSignal::Kind::Continue) {
    reportError(callLoc, "Loop control used outside of loop");
    return Value::makeNull();
  }
  return Value::makeNull();
}

Value InterpreterImpl::evalBinary(const ast::BinaryExpr& expr) {
  if (!expr.lhs || !expr.rhs) {
    reportError(expr.getLocation(), "Binary expression is missing operands");
    return Value::makeNull();
  }

  if (expr.op == ast::BinaryOp::And || expr.op == ast::BinaryOp::Or) {
    Value lhs = evalExpression(*expr.lhs);
    auto lhsBool = asBool(lhs, expr.lhs->getLocation());
    if (!lhsBool) {
      return Value::makeNull();
    }
    if (expr.op == ast::BinaryOp::And && !*lhsBool) {
      return Value::makeBool(false);
    }
    if (expr.op == ast::BinaryOp::Or && *lhsBool) {
      return Value::makeBool(true);
    }
    Value rhs = evalExpression(*expr.rhs);
    auto rhsBool = asBool(rhs, expr.rhs->getLocation());
    if (!rhsBool) {
      return Value::makeNull();
    }
    return Value::makeBool(*rhsBool);
  }

  Value lhs = evalExpression(*expr.lhs);
  Value rhs = evalExpression(*expr.rhs);
  if (hasError()) {
    return Value::makeNull();
  }

  if (expr.op == ast::BinaryOp::Eq) {
    return Value::makeBool(valuesEqual(lhs, rhs));
  }
  if (expr.op == ast::BinaryOp::Ne) {
    return Value::makeBool(!valuesEqual(lhs, rhs));
  }

  auto lhsNumeric = asNumeric(lhs, expr.lhs->getLocation());
  auto rhsNumeric = asNumeric(rhs, expr.rhs->getLocation());
  if (!lhsNumeric || !rhsNumeric) {
    return Value::makeNull();
  }

  switch (expr.op) {
  case ast::BinaryOp::Add:
  case ast::BinaryOp::Sub:
  case ast::BinaryOp::Mul:
  case ast::BinaryOp::Div:
    if (auto result = promoteNumeric(*lhsNumeric, *rhsNumeric, expr.op, expr.getLocation())) {
      return *result;
    }
    return Value::makeNull();
  case ast::BinaryOp::Lt:
  case ast::BinaryOp::Le:
  case ast::BinaryOp::Gt:
  case ast::BinaryOp::Ge: {
    if (lhsNumeric->isFloat || rhsNumeric->isFloat) {
      const double l = lhsNumeric->isFloat ? lhsNumeric->floatValue
                                           : static_cast<double>(lhsNumeric->intValue);
      const double r = rhsNumeric->isFloat ? rhsNumeric->floatValue
                                           : static_cast<double>(rhsNumeric->intValue);
      switch (expr.op) {
      case ast::BinaryOp::Lt:
        return Value::makeBool(l < r);
      case ast::BinaryOp::Le:
        return Value::makeBool(l <= r);
      case ast::BinaryOp::Gt:
        return Value::makeBool(l > r);
      case ast::BinaryOp::Ge:
        return Value::makeBool(l >= r);
      default:
        break;
      }
    } else {
      const int64_t l = lhsNumeric->intValue;
      const int64_t r = rhsNumeric->intValue;
      switch (expr.op) {
      case ast::BinaryOp::Lt:
        return Value::makeBool(l < r);
      case ast::BinaryOp::Le:
        return Value::makeBool(l <= r);
      case ast::BinaryOp::Gt:
        return Value::makeBool(l > r);
      case ast::BinaryOp::Ge:
        return Value::makeBool(l >= r);
      default:
        break;
      }
    }
    break;
  }
  default:
    break;
  }

  reportError(expr.getLocation(), "Unsupported binary operator");
  return Value::makeNull();
}

Value InterpreterImpl::evalUnary(const ast::UnaryExpr& expr) {
  if (!expr.operand) {
    reportError(expr.getLocation(), "Unary expression is missing operand");
    return Value::makeNull();
  }
  Value operand = evalExpression(*expr.operand);
  if (expr.op == ast::UnaryOp::Not) {
    auto value = asBool(operand, expr.operand->getLocation());
    if (!value) {
      return Value::makeNull();
    }
    return Value::makeBool(!*value);
  }
  if (expr.op == ast::UnaryOp::Negate) {
    auto numeric = asNumeric(operand, expr.operand->getLocation());
    if (!numeric) {
      return Value::makeNull();
    }
    if (numeric->isFloat) {
      return Value::makeFloat(-numeric->floatValue);
    }
    return Value::makeInt(-numeric->intValue);
  }
  reportError(expr.getLocation(), "Unsupported unary operator");
  return Value::makeNull();
}

Value InterpreterImpl::evalLiteral(const ast::LiteralExpr& expr) {
  const std::string& text = expr.value;
  if (text == "true") {
    return Value::makeBool(true);
  }
  if (text == "false") {
    return Value::makeBool(false);
  }
  if (text == "null") {
    return Value::makeNull();
  }
  if (!text.empty() && text.front() == '"') {
    return Value::makeString(stripQuotes(text));
  }

  std::string normalized;
  if (!normalizeNumericLiteral(text, normalized)) {
    reportError(expr.getLocation(), "Invalid numeric literal '" + text + "'");
    return Value::makeNull();
  }
  if (normalized.find('.') != std::string::npos) {
    char* end = nullptr;
    const double v = std::strtod(normalized.c_str(), &end);
    if (end != normalized.c_str() + normalized.size()) {
      reportError(expr.getLocation(), "Invalid float literal '" + text + "'");
      return Value::makeNull();
    }
    return Value::makeFloat(v);
  }

  int64_t value = 0;
  const auto parsed =
      std::from_chars(normalized.data(), normalized.data() + normalized.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != normalized.data() + normalized.size()) {
    reportError(expr.getLocation(), "Invalid integer literal '" + text + "'");
    return Value::makeNull();
  }
  return Value::makeInt(value);
}

Value InterpreterImpl::evalIdentifier(const ast::IdentifierExpr& expr) {
  const Binding* binding = lookupBinding(expr.name);
  if (!binding) {
    reportError(expr.getLocation(), "Unknown identifier '" + expr.name + "'");
    return Value::makeNull();
  }
  return binding->value;
}

Value InterpreterImpl::evalFieldAccess(const ast::FieldAccessExpr& expr) {
  if (!expr.base) {
    reportError(expr.getLocation(), "Field access missing base expression");
    return Value::makeNull();
  }
  Value base = evalExpression(*expr.base);
  if (base.kind == Value::Kind::Array) {
    if (expr.field == u8"طول") {
      if (!base.arrayValue) {
        return Value::makeInt(0);
      }
      return Value::makeInt(static_cast<int64_t>(base.arrayValue->elements.size()));
    }
    reportError(expr.getLocation(), "Unknown field '" + expr.field + "' on array");
    return Value::makeNull();
  }
  if (base.kind != Value::Kind::Struct || !base.structValue) {
    reportError(expr.getLocation(), "Field access requires a struct value");
    return Value::makeNull();
  }
  auto it = base.structValue->fields.find(expr.field);
  if (it == base.structValue->fields.end()) {
    reportError(expr.getLocation(), "Unknown field '" + expr.field + "' on struct");
    return Value::makeNull();
  }
  return it->second;
}

Value InterpreterImpl::evalIndex(const ast::IndexExpr& expr) {
  if (!expr.base || !expr.index) {
    reportError(expr.getLocation(), "Index expression is missing base/index");
    return Value::makeNull();
  }
  Value base = evalExpression(*expr.base);
  if (base.kind != Value::Kind::Array || !base.arrayValue) {
    reportError(expr.getLocation(), "Index access requires an array value");
    return Value::makeNull();
  }
  Value indexValue = evalExpression(*expr.index);
  auto index = asIntIndex(indexValue, expr.index->getLocation());
  if (!index) {
    return Value::makeNull();
  }
  if (*index < 0 || static_cast<std::size_t>(*index) >= base.arrayValue->elements.size()) {
    reportError(expr.index->getLocation(), "Array index out of bounds");
    return Value::makeNull();
  }
  return base.arrayValue->elements[static_cast<std::size_t>(*index)];
}

Value InterpreterImpl::evalCall(const ast::CallExpr& expr) {
  std::vector<Value> args;
  args.reserve(expr.args.size());
  for (const auto& arg : expr.args) {
    if (!arg) {
      reportError(expr.getLocation(), "Call argument is missing");
      return Value::makeNull();
    }
    args.push_back(evalExpression(*arg));
    if (hasError()) {
      return Value::makeNull();
    }
  }
  return callFunction(expr.callee, args, expr.getLocation());
}

Value InterpreterImpl::evalArrayLiteral(const ast::ArrayLiteralExpr& expr) {
  std::vector<Value> values;
  values.reserve(expr.elements.size());
  for (const auto& element : expr.elements) {
    if (!element) {
      reportError(expr.getLocation(), "Array literal element is missing");
      return Value::makeNull();
    }
    values.push_back(evalExpression(*element));
    if (hasError()) {
      return Value::makeNull();
    }
  }
  return Value::makeArray(std::move(values));
}

Value InterpreterImpl::evalStructLiteral(const ast::StructLiteralExpr& expr) {
  auto structIt = structDecls_.find(expr.typeName);
  if (structIt == structDecls_.end()) {
    reportError(expr.getLocation(), "Unknown struct type '" + expr.typeName + "'");
    return Value::makeNull();
  }

  std::unordered_map<std::string, Value> fields;
  for (const auto& fieldDecl : structIt->second->fields) {
    fields.emplace(fieldDecl->name, Value::makeNull());
  }

  for (const auto& init : expr.fields) {
    if (!init || !init->value) {
      reportError(expr.getLocation(), "Struct field initializer is missing");
      return Value::makeNull();
    }
    auto fit = fields.find(init->name);
    if (fit == fields.end()) {
      reportError(init->getLocation(), "Unknown field '" + init->name + "' in struct literal");
      return Value::makeNull();
    }
    fit->second = evalExpression(*init->value);
    if (hasError()) {
      return Value::makeNull();
    }
  }

  return Value::makeStruct(expr.typeName, std::move(fields));
}

std::optional<NumericValue> InterpreterImpl::asNumeric(const Value& value, const SourceLocation& loc) {
  NumericValue out;
  switch (value.kind) {
  case Value::Kind::Int:
    out.isFloat = false;
    out.intValue = value.intValue;
    return out;
  case Value::Kind::Float:
    out.isFloat = true;
    out.floatValue = value.floatValue;
    return out;
  case Value::Kind::Bool:
    out.isFloat = false;
    out.intValue = value.boolValue ? 1 : 0;
    return out;
  default:
    reportError(loc, "Numeric value expected");
    return std::nullopt;
  }
}

std::optional<int64_t> InterpreterImpl::asIntIndex(const Value& value, const SourceLocation& loc) {
  auto numeric = asNumeric(value, loc);
  if (!numeric) {
    return std::nullopt;
  }
  if (numeric->isFloat) {
    reportError(loc, "Array index must be an integer");
    return std::nullopt;
  }
  return numeric->intValue;
}

std::optional<bool> InterpreterImpl::asBool(const Value& value, const SourceLocation& loc) {
  if (value.kind != Value::Kind::Bool) {
    reportError(loc, "Boolean value expected");
    return std::nullopt;
  }
  return value.boolValue;
}

std::string InterpreterImpl::asPrintableString(const Value& value) {
  switch (value.kind) {
  case Value::Kind::String:
    return value.stringValue;
  case Value::Kind::Int:
    return std::to_string(value.intValue);
  case Value::Kind::Float: {
    std::string text = std::to_string(value.floatValue);
    while (!text.empty() && text.back() == '0') {
      text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
      text.push_back('0');
    }
    return text;
  }
  case Value::Kind::Bool:
    return value.boolValue ? "true" : "false";
  case Value::Kind::Null:
    return "null";
  case Value::Kind::Array:
    return "[array]";
  case Value::Kind::Struct:
    return value.structValue ? ("[struct " + value.structValue->typeName + "]") : "[struct]";
  }
  return "";
}

void InterpreterImpl::pushScope() { scopes_.emplace_back(); }

void InterpreterImpl::popScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

bool InterpreterImpl::declareBinding(const std::string& name, Value value, bool isConst,
                                     const SourceLocation& loc) {
  if (scopes_.empty()) {
    pushScope();
  }
  auto& current = scopes_.back();
  if (current.find(name) != current.end()) {
    reportError(loc, "Duplicate declaration of '" + name + "'");
    return false;
  }
  current.emplace(name, Binding{std::move(value), isConst});
  return true;
}

Binding* InterpreterImpl::lookupBinding(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

const Binding* InterpreterImpl::lookupBinding(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

bool InterpreterImpl::valuesEqual(const Value& lhs, const Value& rhs) const {
  if (lhs.kind == Value::Kind::Int && rhs.kind == Value::Kind::Float) {
    return std::fabs(static_cast<double>(lhs.intValue) - rhs.floatValue) < 1e-9;
  }
  if (lhs.kind == Value::Kind::Float && rhs.kind == Value::Kind::Int) {
    return std::fabs(lhs.floatValue - static_cast<double>(rhs.intValue)) < 1e-9;
  }
  if (lhs.kind != rhs.kind) {
    return false;
  }
  switch (lhs.kind) {
  case Value::Kind::Null:
    return true;
  case Value::Kind::Int:
    return lhs.intValue == rhs.intValue;
  case Value::Kind::Float:
    return std::fabs(lhs.floatValue - rhs.floatValue) < 1e-9;
  case Value::Kind::Bool:
    return lhs.boolValue == rhs.boolValue;
  case Value::Kind::String:
    return lhs.stringValue == rhs.stringValue;
  case Value::Kind::Array:
    return lhs.arrayValue == rhs.arrayValue;
  case Value::Kind::Struct:
    return lhs.structValue == rhs.structValue;
  }
  return false;
}

std::optional<Value> InterpreterImpl::promoteNumeric(const NumericValue& lhs, const NumericValue& rhs,
                                                      ast::BinaryOp op, const SourceLocation& loc) {
  if (lhs.isFloat || rhs.isFloat) {
    const double l = lhs.isFloat ? lhs.floatValue : static_cast<double>(lhs.intValue);
    const double r = rhs.isFloat ? rhs.floatValue : static_cast<double>(rhs.intValue);
    switch (op) {
    case ast::BinaryOp::Add:
      return Value::makeFloat(l + r);
    case ast::BinaryOp::Sub:
      return Value::makeFloat(l - r);
    case ast::BinaryOp::Mul:
      return Value::makeFloat(l * r);
    case ast::BinaryOp::Div:
      if (std::fabs(r) < 1e-12) {
        reportError(loc, "Division by zero");
        return std::nullopt;
      }
      return Value::makeFloat(l / r);
    default:
      return std::nullopt;
    }
  }

  const int64_t l = lhs.intValue;
  const int64_t r = rhs.intValue;
  switch (op) {
  case ast::BinaryOp::Add:
    return Value::makeInt(l + r);
  case ast::BinaryOp::Sub:
    return Value::makeInt(l - r);
  case ast::BinaryOp::Mul:
    return Value::makeInt(l * r);
  case ast::BinaryOp::Div:
    if (r == 0) {
      reportError(loc, "Division by zero");
      return std::nullopt;
    }
    return Value::makeInt(l / r);
  default:
    return std::nullopt;
  }
}

void InterpreterImpl::reportError(const SourceLocation& loc, std::string message) {
  errors_.push_back(RuntimeError{loc, std::move(message)});
}

bool InterpreterImpl::normalizeNumericLiteral(std::string_view in, std::string& out) {
  out.clear();
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size();) {
    const unsigned char lead = static_cast<unsigned char>(in[i]);
    if (lead < 0x80) {
      out.push_back(static_cast<char>(lead));
      ++i;
      continue;
    }
    if (i + 1 < in.size() && lead == 0xD9) {
      const unsigned char b2 = static_cast<unsigned char>(in[i + 1]);
      if (b2 >= 0xA0 && b2 <= 0xA9) {
        out.push_back(static_cast<char>('0' + (b2 - 0xA0)));
        i += 2;
        continue;
      }
    }
    if (i + 1 < in.size() && lead == 0xDB) {
      const unsigned char b2 = static_cast<unsigned char>(in[i + 1]);
      if (b2 >= 0xB0 && b2 <= 0xB9) {
        out.push_back(static_cast<char>('0' + (b2 - 0xB0)));
        i += 2;
        continue;
      }
    }
    return false;
  }
  return !out.empty();
}

std::string InterpreterImpl::stripQuotes(std::string text) {
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    return text.substr(1, text.size() - 2);
  }
  return text;
}

RunResult InterpreterImpl::run(const ast::Program& program) {
  functions_.clear();
  structDecls_.clear();
  scopes_.clear();
  errors_.clear();
  stdoutBuffer_.clear();

  for (const auto& node : program.topLevel) {
    if (const auto* fn = llvm::dyn_cast<ast::FunctionDecl>(node.get())) {
      functions_.emplace(fn->name, fn);
    } else if (const auto* decl = llvm::dyn_cast<ast::StructDecl>(node.get())) {
      structDecls_.emplace(decl->name, decl);
    }
  }

  if (functions_.find(std::string(kEntryFunctionName)) == functions_.end()) {
    reportError(SourceLocation{0, 0}, "Missing entry function 'بداية'");
  } else {
    Value exitValue = callFunction(kEntryFunctionName, {}, SourceLocation{0, 0});
    if (!hasError()) {
      if (exitValue.kind == Value::Kind::Int) {
        return RunResult{true, static_cast<int>(exitValue.intValue), {}, stdoutBuffer_};
      }
      if (exitValue.kind == Value::Kind::Bool) {
        return RunResult{true, exitValue.boolValue ? 1 : 0, {}, stdoutBuffer_};
      }
      if (exitValue.kind == Value::Kind::Null) {
        return RunResult{true, 0, {}, stdoutBuffer_};
      }
      reportError(SourceLocation{0, 0}, "Entry function must return int/bool/null");
    }
  }

  return RunResult{false, 1, errors_, stdoutBuffer_};
}

} // namespace

Value Value::makeNull() { return Value{}; }

Value Value::makeInt(int64_t value) {
  Value out;
  out.kind = Kind::Int;
  out.intValue = value;
  return out;
}

Value Value::makeFloat(double value) {
  Value out;
  out.kind = Kind::Float;
  out.floatValue = value;
  return out;
}

Value Value::makeBool(bool value) {
  Value out;
  out.kind = Kind::Bool;
  out.boolValue = value;
  return out;
}

Value Value::makeString(std::string value) {
  Value out;
  out.kind = Kind::String;
  out.stringValue = std::move(value);
  return out;
}

Value Value::makeArray(std::vector<Value> values) {
  Value out;
  out.kind = Kind::Array;
  auto array = std::make_shared<ArrayValue>();
  array->elements = std::move(values);
  out.arrayValue = std::move(array);
  return out;
}

Value Value::makeStruct(std::string typeName, std::unordered_map<std::string, Value> fields) {
  Value out;
  out.kind = Kind::Struct;
  auto value = std::make_shared<StructValue>();
  value->typeName = std::move(typeName);
  value->fields = std::move(fields);
  out.structValue = std::move(value);
  return out;
}

RunResult Interpreter::run(const ast::Program& program) {
  InterpreterImpl impl;
  return impl.run(program);
}

} // namespace dhad::interp
