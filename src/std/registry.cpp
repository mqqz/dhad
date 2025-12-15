#include "std/registry.hpp"

#include <unordered_map>

#include "std/native.hpp"
#include "std/identifiers.hpp"

namespace dhad::stdlib {
namespace {

using dhad::typing::TypeKind;
using dhad::typing::TypePtr;

std::vector<StdFunctionDescriptor> buildRegistry() {
  std::vector<StdFunctionDescriptor> functions;
  functions.reserve(4);

  const auto intType = dhad::typing::makePrimitive(TypeKind::Int);
  const auto stringType = dhad::typing::makePrimitive(TypeKind::String);
  const auto nullType = dhad::typing::makePrimitive(TypeKind::Null);
  const auto arrayAnyType = dhad::typing::makeArray(nullType);

  const auto printType = dhad::typing::makeFunction({stringType}, intType);
  const auto arrayCreateType = dhad::typing::makeFunction({intType}, arrayAnyType);
  const auto arrayLengthType = dhad::typing::makeFunction({arrayAnyType}, intType);

  const auto add = [&functions](std::string_view arabic, std::string_view ascii,
                                NativeFunction native, TypePtr type) {
    functions.push_back(
        StdFunctionDescriptor{std::string(arabic), std::string(ascii), native, std::move(type)});
  };

  add(identifiers::kPrint, "std_print", &nativePrint, printType);
  add(identifiers::kStdPrint, "std_print", &nativePrint, printType);
  add(identifiers::kStdArrayCreate, "std_array_create", &nativeArrayCreate, arrayCreateType);
  add(identifiers::kStdArrayLength, "std_array_length", &nativeArrayLength, arrayLengthType);
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
