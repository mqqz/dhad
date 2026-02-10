#pragma once

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dhad::typing {

enum class TypeKind : uint8_t {
  Int,
  Float,
  Bool,
  Char,
  String,
  Null,
  Array,
  Sum,
  Product,
  Function,
};

struct Type;
using TypePtr = std::shared_ptr<const Type>;

struct ArrayTypeInfo {
  TypePtr element;
};

struct SumTypeInfo {
  std::vector<TypePtr> variants;
  std::string name;
};

struct ProductTypeInfo {
  std::vector<TypePtr> members;
  std::string name;
};

struct FunctionTypeInfo {
  std::vector<TypePtr> params;
  TypePtr result;
};

using TypePayload =
    std::variant<std::monostate, ArrayTypeInfo, SumTypeInfo, ProductTypeInfo, FunctionTypeInfo>;

struct Type {
  TypeKind kind;
  TypePayload payload;

  explicit Type(TypeKind k, TypePayload payload = TypePayload{})
      : kind(k), payload(std::move(payload)) {}
};

TypePtr makePrimitive(TypeKind kind);
TypePtr makeArray(TypePtr element);
TypePtr makeSum(std::vector<TypePtr> variants, std::string name = {});
TypePtr makeProduct(std::vector<TypePtr> members, std::string name = {});
TypePtr makeFunction(std::vector<TypePtr> params, TypePtr result);

bool isPrimitive(TypeKind kind);
bool isComposite(TypeKind kind);

} // namespace dhad::typing
