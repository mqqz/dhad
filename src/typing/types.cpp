#include "typing/types.hpp"

namespace dhad::typing {

TypePtr makePrimitive(TypeKind kind) {
  switch (kind) {
  case TypeKind::Int:
  case TypeKind::Float:
  case TypeKind::Bool:
  case TypeKind::Char:
  case TypeKind::String:
  case TypeKind::Null:
    return std::make_shared<Type>(kind);
  default:
    assert(false && "Invalid primitive type kind");
    return nullptr;
  }
}

TypePtr makeArray(TypePtr element) {
  return std::make_shared<Type>(TypeKind::Array, ArrayTypeInfo{std::move(element)});
}

TypePtr makeSum(std::vector<TypePtr> variants, std::string name) {
  return std::make_shared<Type>(TypeKind::Sum, SumTypeInfo{std::move(variants), std::move(name)});
}

TypePtr makeProduct(std::vector<TypePtr> members, std::string name) {
  return std::make_shared<Type>(TypeKind::Product,
                                ProductTypeInfo{std::move(members), std::move(name)});
}

TypePtr makeFunction(std::vector<TypePtr> params, TypePtr result) {
  return std::make_shared<Type>(TypeKind::Function,
                                FunctionTypeInfo{std::move(params), std::move(result)});
}

bool isPrimitive(TypeKind kind) {
  switch (kind) {
  case TypeKind::Int:
  case TypeKind::Float:
  case TypeKind::Bool:
  case TypeKind::Char:
  case TypeKind::String:
  case TypeKind::Null:
    return true;
  default:
    return false;
  }
}

bool isComposite(TypeKind kind) { return !isPrimitive(kind); }

} // namespace dhad::typing
