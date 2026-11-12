// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "SpvChainRules.h"

#include <openssl/evp.h>

#include <algorithm>
#include <stdexcept>

namespace XfgSwap {

namespace {

constexpr int64_t MAX_FUTURE_BLOCK_TIME = 2 * 60 * 60;
constexpr size_t MEDIAN_TIME_SPAN = 11;

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

PowTarget::PowTarget(uint64_t value) {
  m_limbs.fill(0);
  m_limbs[0] = static_cast<uint32_t>(value);
  m_limbs[1] = static_cast<uint32_t>(value >> 32);
}

PowTarget PowTarget::fromCompactUnchecked(uint32_t bits) {
  const unsigned size = bits >> 24;
  uint32_t word = bits & 0x007fffff;
  if (size <= 3) {
    word >>= 8 * (3 - size);
    return PowTarget(word);
  }
  return PowTarget(word) << (8 * (size - 3));
}

bool PowTarget::fromCompact(uint32_t bits, PowTarget& target) {
  const unsigned size = bits >> 24;
  const uint32_t word = bits & 0x007fffff;
  const bool negative = word != 0 && (bits & 0x00800000) != 0;
  const bool overflow =
      word != 0 && (size > 34 || (word > 0xff && size > 33) || (word > 0xffff && size > 32));
  if (negative || overflow) return false;
  target = fromCompactUnchecked(bits);
  return !target.isZero();
}

PowTarget PowTarget::fromBigEndianHex(const std::string& hex) {
  if (hex.size() != 64) throw std::invalid_argument("PowTarget hex must be 64 characters");
  PowTarget out;
  for (size_t i = 0; i < 64; ++i) {
    const int nibble = hexValue(hex[i]);
    if (nibble < 0) throw std::invalid_argument("PowTarget hex is not hexadecimal");
    const size_t bitFromBottom = (63 - i) * 4;
    out.m_limbs[bitFromBottom / 32] |= static_cast<uint32_t>(nibble) << (bitFromBottom % 32);
  }
  return out;
}

PowTarget PowTarget::fromLittleEndianBytes(const std::vector<uint8_t>& bytes) {
  PowTarget out;
  for (size_t i = 0; i < bytes.size() && i < 32; ++i)
    out.m_limbs[i / 4] |= static_cast<uint32_t>(bytes[i]) << (8 * (i % 4));
  return out;
}

unsigned PowTarget::bits() const {
  for (int limb = 7; limb >= 0; --limb) {
    if (m_limbs[limb] == 0) continue;
    for (int bit = 31; bit >= 0; --bit)
      if (m_limbs[limb] & (1u << bit)) return static_cast<unsigned>(limb * 32 + bit + 1);
  }
  return 0;
}

bool PowTarget::isZero() const {
  return std::all_of(m_limbs.begin(), m_limbs.end(), [](uint32_t l) { return l == 0; });
}

uint32_t PowTarget::toCompact() const {
  unsigned size = (bits() + 7) / 8;
  uint32_t compact = 0;
  if (size <= 3) {
    const uint64_t low = static_cast<uint64_t>(m_limbs[0]) | (static_cast<uint64_t>(m_limbs[1]) << 32);
    compact = static_cast<uint32_t>(low << (8 * (3 - size)));
  } else {
    const PowTarget shifted = *this >> (8 * (size - 3));
    compact = shifted.m_limbs[0];
  }
  if (compact & 0x00800000) {
    compact >>= 8;
    ++size;
  }
  return compact | (size << 24);
}

PowTarget& PowTarget::operator*=(uint32_t factor) {
  uint64_t carry = 0;
  for (auto& limb : m_limbs) {
    const uint64_t product = static_cast<uint64_t>(limb) * factor + carry;
    limb = static_cast<uint32_t>(product);
    carry = product >> 32;
  }
  return *this;
}

PowTarget& PowTarget::operator/=(uint32_t divisor) {
  if (divisor == 0) throw std::domain_error("PowTarget division by zero");
  uint64_t remainder = 0;
  for (int i = 7; i >= 0; --i) {
    const uint64_t current = (remainder << 32) | m_limbs[i];
    m_limbs[i] = static_cast<uint32_t>(current / divisor);
    remainder = current % divisor;
  }
  return *this;
}

PowTarget PowTarget::operator<<(unsigned shift) const {
  PowTarget out;
  const unsigned limbShift = shift / 32;
  const unsigned bitShift = shift % 32;
  for (int i = 7; i >= 0; --i) {
    const int from = i - static_cast<int>(limbShift);
    if (from < 0) continue;
    uint64_t value = static_cast<uint64_t>(m_limbs[from]) << bitShift;
    if (bitShift && from - 1 >= 0) value |= static_cast<uint64_t>(m_limbs[from - 1]) >> (32 - bitShift);
    out.m_limbs[i] = static_cast<uint32_t>(value);
  }
  return out;
}

PowTarget PowTarget::operator>>(unsigned shift) const {
  PowTarget out;
  const unsigned limbShift = shift / 32;
  const unsigned bitShift = shift % 32;
  for (unsigned i = 0; i < 8; ++i) {
    const unsigned from = i + limbShift;
    if (from > 7) break;
    uint64_t value = m_limbs[from] >> bitShift;
    if (bitShift && from + 1 <= 7) value |= static_cast<uint64_t>(m_limbs[from + 1]) << (32 - bitShift);
    out.m_limbs[i] = static_cast<uint32_t>(value);
  }
  return out;
}

int PowTarget::compare(const PowTarget& other) const {
  for (int i = 7; i >= 0; --i) {
    if (m_limbs[i] < other.m_limbs[i]) return -1;
    if (m_limbs[i] > other.m_limbs[i]) return 1;
  }
  return 0;
}

SpvChainRules SpvChainRules::bitcoinMainnet() {
  SpvChainRules rules;
  rules.name = "BTC";
  rules.powHash = SpvPowHash::DOUBLE_SHA256;
  rules.retarget = SpvRetarget::BITCOIN_2016;
  rules.powLimit = PowTarget::fromBigEndianHex(
      "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  rules.targetTimespan = 14 * 24 * 60 * 60;
  rules.targetSpacing = 10 * 60;
  return rules;
}

SpvChainRules SpvChainRules::litecoinMainnet() {
  SpvChainRules rules;
  rules.name = "LTC";
  rules.powHash = SpvPowHash::SCRYPT_1024_1_1;
  rules.retarget = SpvRetarget::LITECOIN_2016;
  rules.powLimit = PowTarget::fromBigEndianHex(
      "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  rules.targetTimespan = 302400;  // 3.5 days
  rules.targetSpacing = 150;      // 2.5 minutes
  return rules;
}

SpvChainRules SpvChainRules::bitcoinRegtest() {
  SpvChainRules rules = bitcoinMainnet();
  rules.retarget = SpvRetarget::FIXED;
  rules.powLimit = PowTarget::fromBigEndianHex(
      "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  rules.targetTimespan = 24 * 60 * 60;
  return rules;
}

SpvChainRules SpvChainRules::litecoinRegtest() {
  SpvChainRules rules = litecoinMainnet();
  rules.retarget = SpvRetarget::FIXED;
  rules.powLimit = PowTarget::fromBigEndianHex(
      "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  return rules;
}

uint64_t SpvChainRules::checkpointHistory() const {
  switch (retarget) {
    case SpvRetarget::BITCOIN_2016:
      return static_cast<uint64_t>(interval());
    case SpvRetarget::LITECOIN_2016:
      return static_cast<uint64_t>(interval()) + 1;
    case SpvRetarget::ASERT:
    case SpvRetarget::FIXED:
      return MEDIAN_TIME_SPAN;
  }
  return MEDIAN_TIME_SPAN;
}

std::string SpvChainRules::checkpointPlacementError(uint64_t height) const {
  if (retarget == SpvRetarget::ASERT && static_cast<int64_t>(height) < asertAnchorHeight)
    return name + " checkpoint must be at or above the ASERT anchor height " + std::to_string(asertAnchorHeight);
  return "";
}

SpvChainRules SpvChainRules::bitcoinCashMainnet() {
  SpvChainRules rules;
  rules.name = "BCH";
  rules.powHash = SpvPowHash::DOUBLE_SHA256;
  rules.retarget = SpvRetarget::ASERT;
  rules.powLimit = PowTarget::fromBigEndianHex(
      "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  rules.targetTimespan = 14 * 24 * 60 * 60;
  rules.targetSpacing = 10 * 60;
  rules.asertAnchorHeight = 661647;
  rules.asertAnchorBits = 0x1804dafe;
  rules.asertAnchorParentTime = 1605447844;
  rules.asertHalfLife = 2 * 24 * 60 * 60;
  return rules;
}

std::vector<uint8_t> powHashOf(const SpvChainRules& rules, const SpvHeader& header) {
  if (rules.powHash == SpvPowHash::DOUBLE_SHA256) return header.hash();
  const std::vector<uint8_t> raw = header.serialize();
  std::vector<uint8_t> out(32);
  if (EVP_PBE_scrypt(reinterpret_cast<const char*>(raw.data()), raw.size(), raw.data(), raw.size(),
                     1024, 1, 1, 0, out.data(), out.size()) != 1)
    return {};
  return out;
}

uint32_t retargetBits(const SpvChainRules& rules, uint32_t lastBits, int64_t actualTimespan) {
  const int64_t clamped = std::min(std::max(actualTimespan, rules.targetTimespan / 4),
                                   rules.targetTimespan * 4);
  PowTarget next = PowTarget::fromCompactUnchecked(lastBits);
  // Litecoin: the intermediate product can need one more bit than 256.
  const bool shift = rules.retarget == SpvRetarget::LITECOIN_2016 &&
                     next.bits() > rules.powLimit.bits() - 1;
  if (shift) next = next >> 1;
  next *= static_cast<uint32_t>(clamped);
  next /= static_cast<uint32_t>(rules.targetTimespan);
  if (shift) next = next << 1;
  if (next > rules.powLimit) next = rules.powLimit;
  return next.toCompact();
}

bool permittedTransition(const SpvChainRules& rules, uint64_t height, uint32_t oldBits, uint32_t newBits) {
  if (height % static_cast<uint64_t>(rules.interval()) != 0) return oldBits == newBits;
  PowTarget observed;
  if (!PowTarget::fromCompact(newBits, observed)) return false;
  const PowTarget easiest =
      PowTarget::fromCompactUnchecked(retargetBits(rules, oldBits, rules.targetTimespan * 4));
  const PowTarget hardest =
      PowTarget::fromCompactUnchecked(retargetBits(rules, oldBits, rules.targetTimespan / 4));
  return !(easiest < observed) && !(hardest > observed);
}

PowTarget calculateAsert(const PowTarget& refTarget, int64_t targetSpacing, int64_t timeDiff,
                         int64_t heightDiff, const PowTarget& powLimit, int64_t halfLife) {
  static_assert(int64_t(-1) >> 1 == int64_t(-1), "ASERT needs arithmetic right shift");
  const int64_t exponent = ((timeDiff - targetSpacing * (heightDiff + 1)) * 65536) / halfLife;
  int64_t shifts = exponent >> 16;
  const uint64_t frac = static_cast<uint16_t>(exponent);
  const uint32_t factor = 65536 + static_cast<uint32_t>(
      (195766423245049ull * frac + 971821376ull * frac * frac + 5127ull * frac * frac * frac +
       (1ull << 47)) >> 48);
  PowTarget next = refTarget;
  next *= factor;
  shifts -= 16;
  if (shifts <= 0) {
    next = -shifts >= 256 ? PowTarget() : next >> static_cast<unsigned>(-shifts);
  } else {
    const PowTarget shifted = shifts >= 256 ? PowTarget() : next << static_cast<unsigned>(shifts);
    next = (shifts >= 256 || (shifted >> static_cast<unsigned>(shifts)) != next) ? powLimit : shifted;
  }
  if (next.isZero()) return PowTarget(1);
  return next > powLimit ? powLimit : next;
}

namespace {

uint32_t expectedAsertBits(const SpvChainRules& rules, uint64_t parentHeight, const SpvHeader& parent) {
  return calculateAsert(PowTarget::fromCompactUnchecked(rules.asertAnchorBits), rules.targetSpacing,
                        static_cast<int64_t>(parent.time) - rules.asertAnchorParentTime,
                        static_cast<int64_t>(parentHeight) - rules.asertAnchorHeight,
                        rules.powLimit, rules.asertHalfLife)
      .toCompact();
}

std::string bitsViolation(const SpvChainRules& rules, uint64_t height, const SpvHeader& header,
                          const SpvHeader& parent, const SpvAncestorLookup& ancestorAt) {
  if (rules.retarget == SpvRetarget::ASERT) {
    if (height == 0 || static_cast<int64_t>(height - 1) < rules.asertAnchorHeight)
      return "header below the ASERT anchor";
    return header.bits == expectedAsertBits(rules, height - 1, parent) ? "" : "nBits differs from ASERT";
  }
  if (rules.retarget == SpvRetarget::FIXED)
    return header.bits == parent.bits ? "" : "nBits changed on a fixed-difficulty chain";
  const uint64_t interval = static_cast<uint64_t>(rules.interval());
  if (height % interval != 0)
    return header.bits == parent.bits ? "" : "nBits changed outside a retarget";
  const uint64_t back = rules.retarget == SpvRetarget::LITECOIN_2016 && height != interval ? interval + 1
                                                                                          : interval;
  SpvHeader first;
  if (height >= back && ancestorAt(height - back, first)) {
    const int64_t actual = static_cast<int64_t>(parent.time) - static_cast<int64_t>(first.time);
    return header.bits == retargetBits(rules, parent.bits, actual) ? "" : "nBits differs from the retarget";
  }
  return permittedTransition(rules, height, parent.bits, header.bits) ? ""
                                                                      : "retarget beyond the permitted bounds";
}

std::string timeViolation(uint64_t height, const SpvHeader& header, const SpvHeader& parent,
                          const SpvAncestorLookup& ancestorAt, int64_t now) {
  if (static_cast<int64_t>(header.time) > now + MAX_FUTURE_BLOCK_TIME) return "header time too far in the future";
  std::vector<uint32_t> times = {parent.time};
  for (uint64_t back = 2; back <= MEDIAN_TIME_SPAN && height >= back; ++back) {
    SpvHeader ancestor;
    if (!ancestorAt(height - back, ancestor)) break;
    times.push_back(ancestor.time);
  }
  if (times.size() < MEDIAN_TIME_SPAN) return "";  // too close to the checkpoint to know the median
  std::sort(times.begin(), times.end());
  return header.time > times[times.size() / 2] ? "" : "header time not after the median of the last 11";
}

}  // namespace

std::string headerRuleViolation(const SpvChainRules& rules, uint64_t height, const SpvHeader& header,
                                const SpvHeader& parent, const SpvAncestorLookup& ancestorAt,
                                int64_t now) {
  PowTarget target;
  if (!PowTarget::fromCompact(header.bits, target)) return "nBits is negative, overflowing or zero";
  if (target > rules.powLimit) return "target above powLimit";
  const std::vector<uint8_t> work = powHashOf(rules, header);
  if (work.size() != 32 || target < PowTarget::fromLittleEndianBytes(work)) return "insufficient proof of work";
  const std::string bits = bitsViolation(rules, height, header, parent, ancestorAt);
  if (!bits.empty()) return bits;
  return timeViolation(height, header, parent, ancestorAt, now);
}

}  // namespace XfgSwap
