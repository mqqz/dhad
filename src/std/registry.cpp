#include "std/registry.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "std/identifiers.hpp"
#include "std/native.hpp"

namespace dhad::stdlib {
namespace {

using dhad::typing::TypeKind;
using dhad::typing::TypePtr;

class StdSignatureBuilder {
public:
  StdSignatureBuilder() = default;

  TypePtr primitive(TypeKind kind) {
    const auto index = static_cast<std::size_t>(kind);
    auto& slot = cache_[index];
    if (!slot) {
      slot = dhad::typing::makePrimitive(kind);
    }
    return slot;
  }

  TypePtr intType() { return primitive(TypeKind::Int); }
  TypePtr stringType() { return primitive(TypeKind::String); }
  TypePtr nullType() { return primitive(TypeKind::Null); }

  static TypePtr array(TypePtr element) { return dhad::typing::makeArray(std::move(element)); }
  static TypePtr function(std::vector<TypePtr> params, TypePtr result) {
    return dhad::typing::makeFunction(std::move(params), std::move(result));
  }

private:
  static constexpr std::size_t kTypeKindCount = static_cast<std::size_t>(TypeKind::Function) + 1;
  std::array<TypePtr, kTypeKindCount> cache_{};
};

TypePtr buildPrintSignature(StdSignatureBuilder& builder) {
  return dhad::stdlib::StdSignatureBuilder::function({builder.stringType()}, builder.intType());
}

TypePtr buildArrayCreateSignature(StdSignatureBuilder& builder) {
  return dhad::stdlib::StdSignatureBuilder::function(
      {builder.intType()}, dhad::stdlib::StdSignatureBuilder::array(builder.nullType()));
}

TypePtr buildArrayLengthSignature(StdSignatureBuilder& builder) {
  return dhad::stdlib::StdSignatureBuilder::function(
      {dhad::stdlib::StdSignatureBuilder::array(builder.nullType())}, builder.intType());
}

struct BuiltinDef {
  std::string_view arabicName;
  std::string_view asciiName;
  NativeFunction native;
  TypePtr (*signature)(StdSignatureBuilder& builder);
};

std::vector<StdFunctionDescriptor> buildRegistry() {
  std::vector<StdFunctionDescriptor> functions;
  functions.reserve(4);

  const std::array<BuiltinDef, 4> builtinDefs = {{
      {identifiers::kPrint, "std_print", &nativePrint, buildPrintSignature},
      {identifiers::kStdPrint, "std_print", &nativePrint, buildPrintSignature},
      {identifiers::kStdArrayCreate, "std_array_create", &nativeArrayCreate,
       buildArrayCreateSignature},
      {identifiers::kStdArrayLength, "std_array_length", &nativeArrayLength,
       buildArrayLengthSignature},
  }};

  StdSignatureBuilder builder;
  for (const auto& def : builtinDefs) {
    auto type = def.signature(builder);
    functions.push_back(StdFunctionDescriptor{
        std::string(def.arabicName), std::string(def.asciiName), def.native, std::move(type)});
  }
  return functions;
}

const std::vector<StdFunctionDescriptor>& registry() {
  static const auto functions = buildRegistry();
  return functions;
}

const std::unordered_map<std::string_view, const StdFunctionDescriptor*>& functionMap() {
  static const auto map = [] {
    std::unordered_map<std::string_view, const StdFunctionDescriptor*> lookup;
    lookup.reserve(registry().size());
    for (const auto& entry : registry()) {
      lookup.emplace(entry.arabicName, &entry);
    }
    return lookup;
  }();
  return map;
}

} // namespace

const std::vector<StdFunctionDescriptor>& stdFunctionRegistry() { return registry(); }

const StdFunctionDescriptor* lookupStdFunction(std::string_view arabicName) {
  const auto& map = functionMap();
  auto it = map.find(arabicName);
  if (it == map.end()) {
    return nullptr;
  }
  return it->second;
}

} // namespace dhad::stdlib
