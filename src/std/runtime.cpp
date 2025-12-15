#include "std/runtime.hpp"

namespace dhad::stdlib {

const std::vector<StdFunctionDescriptor>& StdRuntime::functions() const {
  return stdFunctionRegistry();
}

const StdFunctionDescriptor* StdRuntime::resolve(std::string_view arabicName) const {
  return lookupStdFunction(arabicName);
}

bool StdRuntime::invoke(std::string_view arabicName, NativeCallContext& ctx) const {
  const auto* descriptor = resolve(arabicName);
  if (!descriptor || !descriptor->native) {
    return false;
  }
  descriptor->native(ctx);
  return true;
}

} // namespace dhad::stdlib
