#pragma once

#include "../ast/ast.hpp"
#include "../lexer/tokens.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dhad::interp {

struct RuntimeError {
  SourceLocation location{0, 0};
  std::string message;
};

struct Value;
struct ArrayValue;
struct StructValue;
using ArrayHandle = std::shared_ptr<ArrayValue>;
using StructHandle = std::shared_ptr<StructValue>;

struct Value {
  enum class Kind {
    Null,
    Int,
    Float,
    Bool,
    String,
    Array,
    Struct,
  };

  Kind kind{Kind::Null};
  int64_t intValue{0};
  double floatValue{0.0};
  bool boolValue{false};
  std::string stringValue;
  ArrayHandle arrayValue;
  StructHandle structValue;

  static Value makeNull();
  static Value makeInt(int64_t value);
  static Value makeFloat(double value);
  static Value makeBool(bool value);
  static Value makeString(std::string value);
  static Value makeArray(std::vector<Value> values);
  static Value makeStruct(std::string typeName, std::unordered_map<std::string, Value> fields);
};

struct ArrayValue {
  std::vector<Value> elements;
};

struct StructValue {
  std::string typeName;
  std::unordered_map<std::string, Value> fields;
};

struct RunResult {
  bool success{false};
  int exitCode{1};
  std::vector<RuntimeError> errors;
  std::string stdoutBuffer;
};

class Interpreter {
public:
  Interpreter() = default;
  RunResult run(const ast::Program& program);
};

} // namespace dhad::interp
