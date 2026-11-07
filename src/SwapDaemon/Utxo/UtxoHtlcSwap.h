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

#include "UtxoHtlc.h"
#include "SwapDaemon/ChainClientResult.h"
#include "SwapDaemon/SwapTypes.h"

#include <string>
#include <vector>

namespace XfgSwap {

struct UtxoHtlcChain {
  std::string ticker;
  SwapPair pair;
  UtxoHtlcEncoding encoding;
  UtxoNetworkParams network;
};

// The swap terms both transports (full node and SPV) enforce identically.

// Lock outpoint as carried by SwapParams.
UtxoOutpoint lockOutpoint(const SwapParams& params);
// The lock script hex at the head of chainState ("<script>[:<claimTxid>][|...]").
std::string lockScriptHex(const std::string& chainState);
bool lockScriptFromState(const SwapParams& params, std::vector<uint8_t>& script, UtxoHtlcTerms& terms);

uint32_t requiredConfirmations(const SwapParams& params);
bool hasAdaptorSecret(const SwapParams& params);
std::vector<uint8_t> adaptorSecretBytes(const SwapParams& params);
// The H(t) Bob published; Alice locks to it and never to a secret she holds.
bool agreedHashLock(const SwapParams& params, std::vector<uint8_t>& hashLock);
// SHA256(t): from the secret when this side holds it, else the agreed hash lock.
bool expectedHashLock(const SwapParams& params, std::vector<uint8_t>& hashLock);

// Why Bob, claiming with `claimPubKey` while the chain tip is `tip`, must not
// accept the lock script in `params`; empty when the script's terms are his.
std::string lockTermsError(const UtxoHtlcChain& chain, const SwapParams& params,
                           const std::vector<uint8_t>& claimPubKey, uint64_t tip);

// A claim pays Bob's configured P2PKH address of this network when there is
// one; a refund always pays the refunder's own key.
std::vector<uint8_t> spendDestination(const UtxoHtlcChain& chain, bool claimPath, const SwapParams& params,
                                      const std::vector<uint8_t>& ownPubKey);
// A spend's fee never takes more than a tenth of the locked value.
uint64_t spendFeeCeiling(uint64_t lockValue);

}  // namespace XfgSwap
