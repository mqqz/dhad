#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "std/native.hpp"
#include "typing/types.hpp"

namespace dhad::stdlib {

struct StdFunctionDescriptor {
  std::string arabicName;
  std::string asciiName;
  NativeFunction native;
  dhad::typing::TypePtr type;
};

const std::vector<StdFunctionDescriptor>& stdFunctionRegistry();
const StdFunctionDescriptor* lookupStdFunction(std::string_view arabicName);

} // namespace dhad::stdlib
