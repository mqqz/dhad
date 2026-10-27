#pragma once

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

namespace dhad::numeric {
inline bool normalize(std::string_view in, std::string& out) {
  out.clear();
  for (std::size_t i = 0; i < in.size();) {
    const auto lead = static_cast<unsigned char>(in[i]);
    if (lead < 0x80) {
      out.push_back(static_cast<char>(lead));
      ++i;
      continue;
    }
    if (i + 1 < in.size()) {
      const auto next = static_cast<unsigned char>(in[i + 1]);
      if ((lead == 0xD9 && next >= 0xA0 && next <= 0xA9) ||
          (lead == 0xDB && next >= 0xB0 && next <= 0xB9)) {
        out.push_back(static_cast<char>('0' + next - (lead == 0xD9 ? 0xA0 : 0xB0)));
        i += 2;
        continue;
      }
    }
    return false;
  }
  return !out.empty();
}
inline bool parseInt(std::string_view text, int64_t& value) {
  std::string normalized;
  if (!normalize(text, normalized)) return false;
  const auto result =
      std::from_chars(normalized.data(), normalized.data() + normalized.size(), value);
  return result.ec == std::errc{} && result.ptr == normalized.data() + normalized.size();
}
inline bool parseFloat(std::string_view text, double& value) {
  std::string normalized;
  if (!normalize(text, normalized)) return false;
  char* end = nullptr;
  errno = 0;
  value = std::strtod(normalized.c_str(), &end);
  return end == normalized.c_str() + normalized.size() && std::isfinite(value) &&
         !(errno == ERANGE && value == 0.0);
}
inline bool isMinMagnitude(std::string_view text) {
  std::string normalized;
  if (!normalize(text, normalized)) return false;
  uint64_t magnitude = 0;
  const auto parsed =
      std::from_chars(normalized.data(), normalized.data() + normalized.size(), magnitude);
  return parsed.ec == std::errc{} && parsed.ptr == normalized.data() + normalized.size() &&
         magnitude == (uint64_t{1} << 63);
}
inline int exitCode(int64_t value) {
  const uint32_t low = static_cast<uint32_t>(value);
  return low <= INT32_MAX ? static_cast<int>(low)
                          : static_cast<int>(static_cast<int64_t>(low) - (int64_t{1} << 32));
}
inline bool add(int64_t a, int64_t b, int64_t& out) {
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
  out = a + b;
  return true;
}
inline bool sub(int64_t a, int64_t b, int64_t& out) {
  if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return false;
  out = a - b;
  return true;
}
inline bool mul(int64_t a, int64_t b, int64_t& out) {
  if (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
            : (a < 0 && (b > 0 ? a < INT64_MIN / b : b < 0 && a < INT64_MAX / b)))
    return false;
  out = a * b;
  return true;
}
} // namespace dhad::numeric
