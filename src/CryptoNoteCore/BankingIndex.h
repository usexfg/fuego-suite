// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2017-2025 Elderfire Privacy Council
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2014-2017 The XDN developers
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

#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>

namespace CryptoNote {
class ISerializer;

class BankingIndex {
public:
  using DepositAmount = int64_t;
  using DepositInterest = uint64_t;
  using DepositHeight = uint32_t;
  using BurnedAmount = uint64_t;

  BankingIndex();
  explicit BankingIndex(DepositHeight expectedHeight);
  void pushBlock(DepositAmount amount, DepositInterest interest);
  void popBlock();
  void reserve(DepositHeight expectedHeight);
  size_t popBlocks(DepositHeight from);
  DepositAmount depositAmountAtHeight(DepositHeight height) const;
  DepositAmount fullDepositAmount() const;
  DepositInterest depositInterestAtHeight(DepositHeight height) const;
  DepositInterest fullInterestAmount() const;
  DepositHeight size() const;
  void serialize(ISerializer& s);

  // integrated burned XFG tracking
  BurnedAmount getBurnedXfgAmount() const;
  BurnedAmount getBurnedXfgAtHeight(DepositHeight height) const;
  void addForeverDeposit(BurnedAmount amount, DepositHeight height);

  // Combined statistics
  struct DepositStats {
    uint64_t totalDeposits;
    uint64_t ethereal_xfg;
    uint64_t regularDeposits;  // totalDeposits - ethereal_xfg
  };

  DepositStats getStats() const;

private:
  struct BankingIndexEntry {
    DepositHeight height;
    DepositAmount amount;
    DepositInterest interest;

    void serialize(ISerializer& s);
  };

  // Integrated burned XFG tracking
  struct BurnedXfgEntry {
    DepositHeight height;
    BurnedAmount amount;
    BurnedAmount cumulative_burned;

    void serialize(ISerializer& s);
  };

  using IndexType = std::vector<BankingIndexEntry>;
  IndexType::const_iterator upperBound(DepositHeight height) const;
  IndexType index;
  DepositHeight blockCount;

  // Integrated burned XFG tracking
  std::vector<BurnedXfgEntry> m_burnedXfgEntries;
  BurnedAmount m_ethereal_xfg;
};
}
