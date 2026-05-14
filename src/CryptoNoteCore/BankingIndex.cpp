// Copyright (c) 2017-2026, Fuego Developers
// Copyright (c) 2017-2025 Elderfire Privacy Council
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2014-2016 The XDN developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include "BankingIndex.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>

//#include "CryptoNoteSerialization.h"
#include "../Serialization/SerializationOverloads.h"

namespace CryptoNote {

BankingIndex::BankingIndex() : blockCount(0), m_ethereal_xfg(0) {
}

BankingIndex::BankingIndex(DepositHeight expectedHeight) : blockCount(0), m_ethereal_xfg(0) {
  index.reserve(expectedHeight + 1);
}

void BankingIndex::reserve(DepositHeight expectedHeight) {
  index.reserve(expectedHeight + 1);
}

auto BankingIndex::fullDepositAmount() const -> DepositAmount {
  return index.empty() ? 0 : index.back().amount;
}

auto BankingIndex::fullInterestAmount() const -> DepositInterest {
  return index.empty() ? 0 : index.back().interest;
}

static inline bool sumWillOverflow(int64_t x, int64_t y) {
  if (y > 0 && x > std::numeric_limits<int64_t>::max() - y) {
    return true;
  }

  if (y < 0 && x < std::numeric_limits<int64_t>::min() - y) {
    return true;
  }

  return false;
}

static inline bool sumWillOverflow(uint64_t x, uint64_t y) {
  if (x > std::numeric_limits<uint64_t>::max() - y) {
    return true;
  }

  return false;
}

void BankingIndex::pushBlock(DepositAmount amount, DepositInterest interest) {
  DepositAmount lastAmount;
  DepositInterest lastInterest;
  if (index.empty()) {
    lastAmount = 0;
    lastInterest = 0;
  } else {
    lastAmount = index.back().amount;
    lastInterest = index.back().interest;
  }

  assert(!sumWillOverflow(amount, lastAmount));
  assert(!sumWillOverflow(interest, lastInterest));
  assert(amount + lastAmount >= 0);
  if (amount != 0) {
    index.push_back({blockCount, amount + lastAmount, interest + lastInterest});
  }

  ++blockCount;
}

void BankingIndex::popBlock() {
  assert(blockCount > 0);

  // Calculate burned amount (if any) in block before popping
  uint64_t burnedInBlock = 0;
  if (!m_burnedXfgEntries.empty() && m_burnedXfgEntries.back().height == blockCount) {
    uint64_t currentTotal = m_ethereal_xfg;
    uint64_t previousTotal = (m_burnedXfgEntries.size() > 1) ?
      (--m_burnedXfgEntries.end() - 1)->cumulative_burned : 0;
    burnedInBlock = currentTotal - previousTotal;

    m_burnedXfgEntries.pop_back();
    m_ethereal_xfg -= burnedInBlock;  // Update total
  }

  --blockCount;
  if (!index.empty() && index.back().height == blockCount) {
    index.pop_back();
  }
}

auto BankingIndex::size() const -> DepositHeight {
  return blockCount;
}

auto BankingIndex::upperBound(DepositHeight height) const -> IndexType::const_iterator {
  return std::upper_bound(
      index.cbegin(), index.cend(), height,
      [] (DepositHeight height, const BankingIndexEntry& left) { return height < left.height; });
}

size_t BankingIndex::popBlocks(DepositHeight from) {
  if (from >= blockCount) {
    return 0;
  }

  IndexType::iterator it = index.begin();
  std::advance(it, std::distance(index.cbegin(), upperBound(from)));
  if (it != index.begin()) {
    --it;
    if (it->height != from) {
      ++it;
    }
  }

  index.erase(it, index.end());

  // Also pop burned XFG entries from this height
  auto burnedIt = m_burnedXfgEntries.begin();
  while (burnedIt != m_burnedXfgEntries.end() && burnedIt->height >= from) {
    ++burnedIt;
  }
  m_burnedXfgEntries.erase(burnedIt, m_burnedXfgEntries.end());

  auto diff = blockCount - from;
  blockCount -= diff;
  return diff;
}

auto BankingIndex::depositAmountAtHeight(DepositHeight height) const -> DepositAmount {
  if (blockCount == 0) {
    return 0;
  } else {
    auto it = upperBound(height);
    return it == index.cbegin() ? 0 : (--it)->amount;
  }
}

auto BankingIndex::depositInterestAtHeight(DepositHeight height) const -> DepositInterest {
  if (blockCount == 0) {
    return 0;
  } else {
    auto it = upperBound(height);
    return it == index.cbegin() ? 0 : (--it)->interest;
  }
}

// burned XFG tracking methods
BankingIndex::BurnedAmount BankingIndex::getBurnedXfgAmount() const {
  return m_ethereal_xfg;
}

BankingIndex::BurnedAmount BankingIndex::getBurnedXfgAtHeight(DepositHeight height) const {
  if (m_burnedXfgEntries.empty()) {
    return 0;
  }

  auto it = std::upper_bound(
    m_burnedXfgEntries.cbegin(), m_burnedXfgEntries.cend(), height,
    [] (DepositHeight height, const BurnedXfgEntry& entry) { return height < entry.height; });

  return it == m_burnedXfgEntries.cbegin() ? 0 : (--it)->cumulative_burned;
}

void BankingIndex::addForeverDeposit(BurnedAmount amount, DepositHeight height) {
  if (amount == 0) return;

  // Add to regular deposit tracking (existing functionality)
  // Note: This would typically be called from the wallet when creating a FOREVER deposit
  // pushBlock(static_cast<DepositAmount>(amount), 0);

  // Add to burned XFG tracking (new functionality)
  // SECURITY: Check for overflow before adding to cumulative burned amount
  const uint64_t MAX_BURNED = std::numeric_limits<uint64_t>::max();
  assert(amount <= MAX_BURNED - m_ethereal_xfg && "addForeverDeposit: Overflow in cumulative burned amount!");

  m_ethereal_xfg += amount;

  if (!m_burnedXfgEntries.empty() && m_burnedXfgEntries.back().height == height) {
    // Update existing entry
    // SECURITY: Check for overflow in entry amount as well
    assert(amount <= MAX_BURNED - m_burnedXfgEntries.back().amount && "addForeverDeposit: Overflow in entry amount!");
    m_burnedXfgEntries.back().amount += amount;
    m_burnedXfgEntries.back().cumulative_burned = m_ethereal_xfg;
  } else {
    // Create new entry
    m_burnedXfgEntries.push_back({
      height,
      amount,
      m_ethereal_xfg
    });
  }
}



BankingIndex::DepositStats BankingIndex::getStats() const {
  DepositStats stats;
  stats.totalDeposits = static_cast<uint64_t>(fullDepositAmount());
  stats.ethereal_xfg = m_ethereal_xfg;
  stats.regularDeposits = stats.totalDeposits > stats.ethereal_xfg ?
    stats.totalDeposits - stats.ethereal_xfg : 0;
  return stats;
}

void BankingIndex::serialize(ISerializer& s) {
  s(blockCount, "blockCount");
  s(m_ethereal_xfg, "ethernalXFG");

  if (s.type() == ISerializer::INPUT) {
    readSequence<BankingIndexEntry>(std::back_inserter(index), "index", s);
    readSequence<BurnedXfgEntry>(std::back_inserter(m_burnedXfgEntries), "burnedXfgEntries", s);
  } else {
    writeSequence<BankingIndexEntry>(index.begin(), index.end(), "index", s);
    writeSequence<BurnedXfgEntry>(m_burnedXfgEntries.begin(), m_burnedXfgEntries.end(), "burnedXfgEntries", s);
  }
}

void BankingIndex::BankingIndexEntry::serialize(ISerializer& s) {
  s(height, "height");
  s(amount, "amount");
  s(interest, "interest");
}

void BankingIndex::BurnedXfgEntry::serialize(ISerializer& s) {
  s(height, "height");
  s(amount, "amount");
  s(cumulative_burned, "cumulative_burned");
}

}
