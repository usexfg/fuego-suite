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

#include "WalletLegacy/HearthQuote.h"

#include <system_error>

#include "Common/Int128.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/AmmPool.h"
#include "Wallet/WalletErrors.h"

namespace CryptoNote {

namespace {

INode::AmmPoolInfo currentPool(INode& node) {
  INode::AmmPoolInfo pool;
  const std::error_code ec = node.getAmmPoolInfo(pool);
  if (ec) {
    throw std::system_error(ec, "Hearth pool unavailable");
  }
  if (pool.reserveXfg == 0 || pool.reserveHeat == 0 || pool.totalLpShares == 0) {
    throw std::system_error(make_error_code(error::WRONG_STATE), "Hearth pool is empty");
  }
  return pool;
}

uint64_t heatAtPrice(uint64_t xfg, uint64_t price) {
  return static_cast<uint64_t>((static_cast<uint128_t>(xfg) * price) / parameters::COIN);
}

uint64_t lessSlippage(uint64_t amount) {
  return static_cast<uint64_t>((static_cast<uint128_t>(amount) * (10000 - parameters::WALLET_SWAP_SLIPPAGE_BPS)) / 10000);
}

}

uint64_t quoteLpAddShares(INode& node, uint64_t amountXfg, uint64_t amountHeat) {
  const INode::AmmPoolInfo pool = currentPool(node);
  return lessSlippage(ammMintLpShares(amountXfg, amountHeat, pool.totalLpShares, pool.reserveXfg, pool.reserveHeat));
}

LpRemovePayout quoteLpRemovePayout(INode& node, uint64_t lpShares) {
  const INode::AmmPoolInfo pool = currentPool(node);
  if (lpShares > pool.totalLpShares) {
    throw std::system_error(make_error_code(error::WRONG_AMOUNT), "LP shares exceed the pool's share supply");
  }
  LpRemovePayout payout;
  ammGetWithdrawalAmounts(lpShares, pool.totalLpShares, pool.reserveXfg, pool.reserveHeat, payout.xfg, payout.heat);
  payout.xfg = lessSlippage(payout.xfg);
  payout.heat = lessSlippage(payout.heat);
  return payout;
}

// The pool query surfaces a node or connection error that getHearthTwap, which
// answers 0 on failure, would report as "no TWAP yet".
HeatMintQuote quoteHeatMint(INode& node, uint64_t xfgBurned) {
  INode::AmmPoolInfo pool;
  const std::error_code ec = node.getAmmPoolInfo(pool);
  if (ec) {
    throw std::system_error(ec, "Hearth pool unavailable");
  }
  return quoteHeatMintAt(node.getHearthTwap(), xfgBurned);
}

HeatMintQuote quoteHeatMintAt(uint64_t hearthTwap, uint64_t xfgBurned) {
  HeatMintQuote quote;
  quote.hearthTwap = hearthTwap;
  if (quote.hearthTwap == 0) {
    throw std::system_error(make_error_code(error::WRONG_STATE), "No Hearth TWAP yet; HEAT mints open two blocks into v11");
  }
  quote.premiumXfg = heatMintPremiumXfg(xfgBurned, BLOCK_MAJOR_VERSION_11);
  const uint64_t burnHeat = heatAtPrice(xfgBurned, quote.hearthTwap);
  quote.premiumHeat = heatAtPrice(quote.premiumXfg, quote.hearthTwap);
  const uint64_t minterCap = quote.premiumHeat < burnHeat ? burnHeat - quote.premiumHeat : 0;
  quote.heatToMinter = static_cast<uint64_t>(
      (static_cast<uint128_t>(minterCap) * (10000 - parameters::WALLET_MINT_TWAP_MARGIN_BPS)) / 10000);
  return quote;
}

uint64_t quoteSwapOutput(INode& node, uint8_t direction, uint64_t inputAmount) {
  const INode::AmmPoolInfo pool = currentPool(node);
  return quoteSwapOutputAt(pool.reserveXfg, pool.reserveHeat, direction, inputAmount);
}

uint64_t quoteSwapOutputAt(uint64_t reserveXfg, uint64_t reserveHeat, uint8_t direction, uint64_t inputAmount) {
  const uint64_t reserveIn = direction == 0 ? reserveXfg : reserveHeat;
  const uint64_t reserveOut = direction == 0 ? reserveHeat : reserveXfg;
  return lessSlippage(ammSwapNetOutput(inputAmount, reserveIn, reserveOut));
}

}
