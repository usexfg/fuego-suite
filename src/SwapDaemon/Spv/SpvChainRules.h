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

#pragma once

#include "SpvHeader.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace XfgSwap {

// Unsigned 256-bit proof-of-work target with the arithmetic and compact
// (nBits) codec of Bitcoin Core's arith_uint256.
class PowTarget {
public:
  PowTarget() { m_limbs.fill(0); }
  explicit PowTarget(uint64_t value);

  // Fails for a negative, overflowing or zero compact value.
  static bool fromCompact(uint32_t bits, PowTarget& target);
  // arith_uint256::SetCompact without the validity flags.
  static PowTarget fromCompactUnchecked(uint32_t bits);
  static PowTarget fromBigEndianHex(const std::string& hex);
  static PowTarget fromLittleEndianBytes(const std::vector<uint8_t>& bytes);

  uint32_t toCompact() const;
  unsigned bits() const;
  bool isZero() const;

  PowTarget& operator*=(uint32_t factor);
  PowTarget& operator/=(uint32_t divisor);
  PowTarget operator<<(unsigned shift) const;
  PowTarget operator>>(unsigned shift) const;

  int compare(const PowTarget& other) const;
  bool operator==(const PowTarget& o) const { return compare(o) == 0; }
  bool operator!=(const PowTarget& o) const { return compare(o) != 0; }
  bool operator<(const PowTarget& o) const { return compare(o) < 0; }
  bool operator>(const PowTarget& o) const { return compare(o) > 0; }
  bool operator<=(const PowTarget& o) const { return compare(o) <= 0; }

private:
  std::array<uint32_t, 8> m_limbs;  // little-endian 32-bit limbs
};

enum class SpvPowHash { DOUBLE_SHA256, SCRYPT_1024_1_1 };
enum class SpvRetarget { BITCOIN_2016, LITECOIN_2016, ASERT, FIXED };

// The consensus rules an SPV client checks on every header of one chain.
struct SpvChainRules {
  std::string name;
  SpvPowHash powHash = SpvPowHash::DOUBLE_SHA256;
  SpvRetarget retarget = SpvRetarget::BITCOIN_2016;
  PowTarget powLimit;
  int64_t targetTimespan = 0;  // seconds per retarget period (2016-block chains)
  int64_t targetSpacing = 0;   // seconds per block
  // ASERT (aserti3-2d) anchor: block height, its nBits, its parent's time.
  int64_t asertAnchorHeight = 0;
  uint32_t asertAnchorBits = 0;
  int64_t asertAnchorParentTime = 0;
  int64_t asertHalfLife = 0;

  int64_t interval() const { return targetSpacing ? targetTimespan / targetSpacing : 0; }

  // Headers ending at the checkpoint that make every check after it exact:
  // the retarget window (Litecoin reads one block further back) or, for
  // ASERT and fixed difficulty, the median-time span.
  uint64_t checkpointHistory() const;
  // ASERT reads its anchor, so a BCH checkpoint below the anchor is refused.
  std::string checkpointPlacementError(uint64_t height) const;

  static SpvChainRules bitcoinMainnet();
  static SpvChainRules litecoinMainnet();
  static SpvChainRules bitcoinCashMainnet();
  static SpvChainRules bitcoinRegtest();
  static SpvChainRules litecoinRegtest();
};

// Header bytes hashed for proof of work; identity hash stays double-SHA256.
std::vector<uint8_t> powHashOf(const SpvChainRules& rules, const SpvHeader& header);

// arith_uint256 CalculateNextWorkRequired for the 2016-block chains.
uint32_t retargetBits(const SpvChainRules& rules, uint32_t lastBits, int64_t actualTimespan);
// Bitcoin Core PermittedDifficultyTransition: the bounds a retarget can reach.
bool permittedTransition(const SpvChainRules& rules, uint64_t height, uint32_t oldBits, uint32_t newBits);
// BCHN CalculateASERT.
PowTarget calculateAsert(const PowTarget& refTarget, int64_t targetSpacing, int64_t timeDiff,
                         int64_t heightDiff, const PowTarget& powLimit, int64_t halfLife);

using SpvAncestorLookup = std::function<bool(uint64_t height, SpvHeader& header)>;

// Why `header` at `height` breaks its chain's rules given its `parent` and
// earlier best-chain headers; empty when it passes. Checks proof of work
// against a valid target within powLimit, the expected nBits (exact when the
// retarget's first block is known, otherwise Core's permitted bounds), median
// time past over 11 blocks when known, and the two-hour future limit.
std::string headerRuleViolation(const SpvChainRules& rules, uint64_t height, const SpvHeader& header,
                                const SpvHeader& parent, const SpvAncestorLookup& ancestorAt,
                                int64_t now);

}  // namespace XfgSwap
