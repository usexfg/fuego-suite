// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
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

class InvestmentIndex {
public:
  using DepositAmount = int64_t;
  using DepositInterest = uint64_t;
  using DepositHeight = uint32_t;
  InvestmentIndex();
  explicit InvestmentIndex(DepositHeight expectedHeight);
  void pushBlock(DepositAmount amount, DepositInterest interest); 
  void popBlock(); 
  void reserve(DepositHeight expectedHeight);
  size_t popBlocks(DepositHeight from); 
  DepositAmount investmentAmountAtHeight(DepositHeight height) const;
  DepositAmount fullDepositAmount() const; 
  DepositInterest depositInterestAtHeight(DepositHeight height) const;
  DepositInterest fullInterestAmount() const; 
  DepositHeight size() const;
  void serialize(ISerializer& s);

private:
  struct InvestmentIndexEntry {
    DepositHeight height;
    DepositAmount amount;
    DepositInterest interest;

    void serialize(ISerializer& s);
  };

  using IndexType = std::vector<InvestmentIndexEntry>;
  IndexType::const_iterator upperBound(DepositHeight height) const;
  IndexType index;
  DepositHeight blockCount;
};
}
