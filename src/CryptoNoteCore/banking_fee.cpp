// Copyright (c) 2017-2025 Fuego Developers
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

#include "Currency.h"

namespace CryptoNote {

// Calculate banking fee as percentage of deposit amount (0.125%)
uint64_t Currency::calculateBankingFee(uint64_t depositAmount) const {
    // 0.125% = 0.00125 = 125 / 100000
    // To avoid floating point arithmetic, use: fee = (depositAmount * 125) / 100000
    // This is equivalent to: fee = depositAmount * 0.00125
    
    uint64_t fee = (depositAmount * 125) / 100000;
    
    // Ensure minimum fee of 1 XFG for small deposits
    if (fee == 0 && depositAmount > 0) {
        fee = COIN; // 1 XFG = 10,000,000 atomic units
    }
    
    return fee;
}

} // namespace CryptoNote