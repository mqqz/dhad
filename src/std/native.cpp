#include "std/native.hpp"

#include <cstdio>
#include <iostream>
#include <string_view>

#include "std/identifiers.hpp"

namespace dhad::stdlib {

namespace {
void logUnimplemented(std::string_view name) {
  const auto module = identifiers::kStdModule;
  std::fprintf(stderr, "[%.*s] Unimplemented std function: %.*s\n", static_cast<int>(module.size()),
               module.data(), static_cast<int>(name.size()), name.data());
}
} // namespace

void nativePrint(NativeCallContext& ctx) {
  if (ctx.args.empty()) {
    return;
  }
  std::fputs(ctx.args.front().c_str(), stdout);
  std::fputc('\n', stdout);
  ctx.result = "0";
}

void nativeArrayCreate(NativeCallContext&) { logUnimplemented(identifiers::kStdArrayCreate); }

void nativeArrayLength(NativeCallContext&) { logUnimplemented(identifiers::kStdArrayLength); }

} // namespace dhad::stdlib
