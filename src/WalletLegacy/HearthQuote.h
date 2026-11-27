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

#include <cstdint>

#include "INode.h"

namespace CryptoNote {

struct HeatMintQuote {
  uint64_t hearthTwap = 0;
  uint64_t premiumXfg = 0;
  uint64_t premiumHeat = 0;
  uint64_t heatToMinter = 0;
};

struct LpRemovePayout {
  uint64_t xfg = 0;
  uint64_t heat = 0;
};

// Quotes from the node's current Hearth pool, less WALLET_SWAP_SLIPPAGE_BPS:
// consensus caps both at the formula when the transaction is included, and the
// reserves move with almost every block before then.
uint64_t quoteLpAddShares(INode& node, uint64_t amountXfg, uint64_t amountHeat);
LpRemovePayout quoteLpRemovePayout(INode& node, uint64_t lpShares);

// Prices a v11 mint at the node's Hearth oracle price, as consensus does: the
// premium is priced separately (its truncation can differ by an atomic), and the
// minter's HEAT is quoted WALLET_MINT_TWAP_MARGIN_BPS under the price so a TWAP
// move before inclusion does not invalidate the mint.
HeatMintQuote quoteHeatMint(INode& node, uint64_t xfgBurned);
HeatMintQuote quoteHeatMintAt(uint64_t hearthTwap, uint64_t xfgBurned);

// Hearth swap output on the constant-product curve, net of the taker fee, less
// WALLET_SWAP_SLIPPAGE_BPS. direction 0 sells XFG for HEAT, 1 sells HEAT for XFG.
uint64_t quoteSwapOutput(INode& node, uint8_t direction, uint64_t inputAmount);
uint64_t quoteSwapOutputAt(uint64_t reserveXfg, uint64_t reserveHeat, uint8_t direction, uint64_t inputAmount);

}
