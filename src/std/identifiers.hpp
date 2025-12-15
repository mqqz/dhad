#pragma once

#include <string_view>

namespace dhad::identifiers {

#define DHAD_DEFINE_IDENTIFIER(name, literal)                                             \
  inline constexpr std::string_view name = literal

DHAD_DEFINE_IDENTIFIER(kStdModule, u8"أساس");
DHAD_DEFINE_IDENTIFIER(kStdPrint, u8"أساس.اطبع");
DHAD_DEFINE_IDENTIFIER(kStdArrayCreate, u8"أساس.أنشئ_مصفوفة");
DHAD_DEFINE_IDENTIFIER(kStdArrayLength, u8"أساس.طول_مصفوفة");
DHAD_DEFINE_IDENTIFIER(kPrint, u8"اطبع");

#undef DHAD_DEFINE_IDENTIFIER

} // namespace dhad::identifiers
