// Copyright (c) 2017-2025 Fuego Developers
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
#include "../CryptoNoteConfig.h"

namespace CryptoNote {

// EFier banking fee removed — banking fee is zero pending v11 governance vote - no. recalculate bankfee
uint64_t Currency::calculateBankingFee(uint64_t /*depositAmount*/) const {
    return 0;
}

} // namespace CryptoNote
