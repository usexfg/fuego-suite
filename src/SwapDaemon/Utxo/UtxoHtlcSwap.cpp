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

#include "UtxoHtlcSwap.h"

#include "SwapDaemon/SwapTimelock.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace XfgSwap {

namespace {

constexpr uint32_t DEFAULT_REQUIRED_CONFIRMATIONS = 6;
constexpr uint64_t MAX_FEE_SHARE_DIVISOR = 10;

bool isZero(const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  return std::all_of(bytes, bytes + size, [](uint8_t b) { return b == 0; });
}

std::vector<uint8_t> podBytes(const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  return std::vector<uint8_t>(bytes, bytes + size);
}

}  // namespace

UtxoOutpoint lockOutpoint(const SwapParams& params) {
  UtxoOutpoint outpoint;
  outpoint.txid = params.ctrLockTxId;
  std::transform(outpoint.txid.begin(), outpoint.txid.end(), outpoint.txid.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  outpoint.vout = params.ctrLockVout;
  return outpoint;
}

std::string lockScriptHex(const std::string& chainState) {
  return chainState.substr(0, chainState.find_first_of(":|"));
}

bool lockScriptFromState(const SwapParams& params, std::vector<uint8_t>& script, UtxoHtlcTerms& terms) {
  return decodeHex(lockScriptHex(params.chainState), script) && parseHtlcScript(script, terms);
}

uint32_t requiredConfirmations(const SwapParams& params) {
  return params.requiredConfirmations ? params.requiredConfirmations : DEFAULT_REQUIRED_CONFIRMATIONS;
}

bool hasAdaptorSecret(const SwapParams& params) {
  return !isZero(&params.adaptorSecret, sizeof(params.adaptorSecret));
}

std::vector<uint8_t> adaptorSecretBytes(const SwapParams& params) {
  return podBytes(&params.adaptorSecret, sizeof(params.adaptorSecret));
}

bool agreedHashLock(const SwapParams& params, std::vector<uint8_t>& hashLock) {
  if (isZero(&params.hashLock, sizeof(params.hashLock))) return false;
  hashLock = podBytes(&params.hashLock, sizeof(params.hashLock));
  return true;
}

bool expectedHashLock(const SwapParams& params, std::vector<uint8_t>& hashLock) {
  if (!hasAdaptorSecret(params)) return agreedHashLock(params, hashLock);
  hashLock = sha256Digest(adaptorSecretBytes(params));
  return true;
}

std::string lockTermsError(const UtxoHtlcChain& chain, const SwapParams& params,
                           const std::vector<uint8_t>& claimPubKey, uint64_t tip) {
  std::vector<uint8_t> script;
  UtxoHtlcTerms terms;
  if (!lockScriptFromState(params, script, terms)) return "no lock script received from Alice";
  std::vector<uint8_t> hashLock;
  if (!expectedHashLock(params, hashLock) || terms.hashLock != hashLock)
    return "lock script hash differs from H(t)";
  if (terms.recipientPubKey != claimPubKey) return "lock script does not pay our claim key";
  uint64_t window = 0;
  if (!minimumCounterpartyWindowBlocks(chain.pair, requiredConfirmations(params), window) ||
      tip > std::numeric_limits<uint64_t>::max() - window || terms.timeoutBlock < tip + window)
    return "lock timeout " + std::to_string(terms.timeoutBlock) + " leaves too few blocks to claim";
  return "";
}

std::string claimDeadlineError(const UtxoHtlcChain& chain, const UtxoHtlcTerms& terms, uint64_t tip) {
  const uint64_t runway = claimRunwayBlocks(chain.pair);
  if (runway == 0 || tip > std::numeric_limits<uint64_t>::max() - runway || terms.timeoutBlock < tip + runway)
    return "lock timeout " + std::to_string(terms.timeoutBlock) + " is within the " + std::to_string(runway) +
           "-block claim runway of tip " + std::to_string(tip) + "; publishing t now could let Alice refund and claim";
  return "";
}

std::vector<uint8_t> spendDestination(const UtxoHtlcChain& chain, bool claimPath, const SwapParams& params,
                                      const std::vector<uint8_t>& ownPubKey) {
  std::vector<uint8_t> destination;
  if (claimPath && p2pkhScriptForAddress(chain.network, params.ctrAddress, destination)) return destination;
  return p2pkhScriptForPubKey(ownPubKey);
}

uint64_t spendFeeCeiling(uint64_t lockValue) {
  return lockValue / MAX_FEE_SHARE_DIVISOR;
}

}  // namespace XfgSwap
