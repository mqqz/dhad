#pragma once

#include <string_view>

#include "std/native.hpp"
#include "std/registry.hpp"

namespace dhad::stdlib {

class StdRuntime {
public:
  StdRuntime() = default;

  [[nodiscard]] const std::vector<StdFunctionDescriptor>& functions() const;
  [[nodiscard]] const StdFunctionDescriptor* resolve(std::string_view arabicName) const;
  bool invoke(std::string_view arabicName, NativeCallContext& ctx) const;
};

} // namespace dhad::stdlib
