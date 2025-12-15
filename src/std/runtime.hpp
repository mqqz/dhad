#pragma once

#include <string_view>

#include "std/native.hpp"
#include "std/registry.hpp"

namespace dhad::stdlib {

class StdRuntime {
public:
  StdRuntime() = default;

  const std::vector<StdFunctionDescriptor>& functions() const;
  const StdFunctionDescriptor* resolve(std::string_view arabicName) const;
  bool invoke(std::string_view arabicName, NativeCallContext& ctx) const;
};

} // namespace dhad::stdlib
