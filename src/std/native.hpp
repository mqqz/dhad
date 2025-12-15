#pragma once

#include <string>
#include <vector>

namespace dhad::stdlib {

struct NativeCallContext {
  std::vector<std::string> args;
  std::string result;
};

using NativeFunction = void (*)(NativeCallContext& ctx);

void nativePrint(NativeCallContext& ctx);
void nativeArrayCreate(NativeCallContext& ctx);
void nativeArrayLength(NativeCallContext& ctx);

} // namespace dhad::stdlib
