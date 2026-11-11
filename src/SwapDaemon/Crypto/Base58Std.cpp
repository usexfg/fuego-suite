// Copyright (c) 2017-2026 Fuego Developers

#include "Base58Std.h"
#include <algorithm>

namespace XfgSwap {

static const char* kAlphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::vector<uint8_t> Base58Std::decode(const std::string& encoded) {
  std::vector<uint8_t> result;
  for (char c : encoded) {
    const char* p = std::find(kAlphabet, kAlphabet + 58, c);
    if (p == kAlphabet + 58) return {};
    int carry = static_cast<int>(p - kAlphabet);
    for (size_t j = 0; j < result.size(); ++j) {
      carry += static_cast<int>(result[j]) * 58;
      result[j] = static_cast<uint8_t>(carry & 0xFF);
      carry >>= 8;
    }
    while (carry > 0) {
      result.push_back(static_cast<uint8_t>(carry & 0xFF));
      carry >>= 8;
    }
  }
  for (char c : encoded) {
    if (c == '1') result.push_back(0);
    else break;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

std::string Base58Std::encode(const std::vector<uint8_t>& data) {
  if (data.empty()) return "";
  size_t leadingZeros = 0;
  while (leadingZeros < data.size() && data[leadingZeros] == 0) ++leadingZeros;
  std::vector<uint8_t> digits((data.size() - leadingZeros) * 138 / 100 + 1, 0);
  for (size_t i = leadingZeros; i < data.size(); ++i) {
    int carry = data[i];
    for (int j = static_cast<int>(digits.size()) - 1; j >= 0; --j) {
      carry += 256 * digits[j];
      digits[j] = static_cast<uint8_t>(carry % 58);
      carry /= 58;
    }
  }
  std::string result(leadingZeros, '1');
  size_t start = 0;
  while (start < digits.size() && digits[start] == 0) ++start;
  for (size_t i = start; i < digits.size(); ++i) result += kAlphabet[digits[i]];
  return result;
}

} // namespace XfgSwap
