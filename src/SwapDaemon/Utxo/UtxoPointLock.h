// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
// Taproot point lock for BTC and LTC (CROSS_CURVE_PLAN X6).
//
// Output key: BIP341's unspendable internal key tweaked with two leaves,
//   claim:  <A> OP_CHECKSIGVERIFY <B> OP_CHECKSIG
//   refund: <timeout> OP_CHECKLOCKTIMEVERIFY OP_DROP <A> OP_CHECKSIG
// A is Alice (locker), B is Bob (claimer). Alice gives Bob a BIP340 adaptor
// pre-signature, encrypted to T_secp, on the one claim transaction; Bob
// completes it with t, so his claim publishes s and Alice recovers t = s - s'.
// Alice cannot spend before the timeout, so she cannot race Bob's claim.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "SwapDaemon/Crypto/Bip340Adaptor.h"
#include "UtxoFullNode.h"

namespace XfgSwap {

struct PointLockTerms {
  Bip340::XOnlyKey aliceKey{};
  Bip340::XOnlyKey bobKey{};
  uint32_t timeoutBlock = 0;
};

// One pre-signed claim at one fee. Alice signs a short ladder of fees so a
// claim that stops confirming can be replaced by a higher one before Alice's
// refund becomes valid; every rung is encrypted to the same T.
struct PointLockClaimOffer {
  uint64_t fee = 0;
  Bip340::AdaptorPresig presig{};
};

constexpr size_t MAX_CLAIM_OFFERS = 4;
constexpr uint64_t CLAIM_FEE_STEP = 3;  // each rung, and each fee child, triples the fee rate

struct PointLockState {
  PointLockTerms terms;
  std::vector<PointLockClaimOffer> claimOffers;  // ascending fees
};

// baseFee x1, x3, x9, x27, each capped at `ceiling`, strictly ascending.
std::vector<uint64_t> claimFeeLadder(uint64_t baseFee, uint64_t ceiling);

struct PointLockSpend {
  UtxoOutpoint lock;
  uint64_t lockValue = 0;
  uint64_t fee = 0;
  std::vector<uint8_t> destinationScript;
};

std::vector<uint8_t> pointLockClaimScript(const PointLockTerms& terms);
std::vector<uint8_t> pointLockRefundScript(const PointLockTerms& terms);
bool pointLockScriptPubKey(const PointLockTerms& terms, std::vector<uint8_t>& scriptPubKey);
std::string pointLockAddress(const PointLockTerms& terms, const std::string& bech32Hrp);

bool pointLockClaimSighash(const PointLockTerms& terms, const PointLockSpend& spend, Bip340::Message& sighash);
bool pointLockRefundSighash(const PointLockTerms& terms, const PointLockSpend& spend, Bip340::Message& sighash);
bool pointLockClaimTransaction(const PointLockTerms& terms, const PointLockSpend& spend,
                               const Bip340::Signature& bobSignature, const Bip340::Signature& aliceSignature,
                               std::vector<uint8_t>& rawTx);
bool pointLockRefundTransaction(const PointLockTerms& terms, const PointLockSpend& spend,
                                const Bip340::Signature& aliceSignature, std::vector<uint8_t>& rawTx);
uint64_t pointLockSpendVirtualSize(bool claimPath);

// Alice's completed signature from a transaction that spends `lock` through
// the claim leaf of `terms`.
bool pointLockClaimSignature(const std::vector<uint8_t>& rawTx, const UtxoOutpoint& lock,
                             const PointLockTerms& terms, Bip340::Signature& aliceSignature);

bool isPointLockState(const std::string& chainState);
std::string encodePointLockState(const PointLockState& state);
bool decodePointLockState(const std::string& chainState, PointLockState& state);

}  // namespace XfgSwap
