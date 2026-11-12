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
// BCH P2SH point lock (CROSS_CURVE_PLAN X7).
//
// Redeem script, the HTLC script's IF/ELSE skeleton with a point check in
// place of the hash check:
//   OP_IF <A> OP_CHECKSIGVERIFY <B> OP_CHECKSIG
//   OP_ELSE <timeoutBlock> OP_CHECKLOCKTIMEVERIFY OP_DROP <A> OP_CHECKSIG OP_ENDIF
// A is Alice (locker), B is Bob (claimer), both 33-byte compressed keys. The
// same script is both branches' scriptCode; a claim's scriptSig pushes
// sigB, sigA, OP_1, the script; a refund's pushes sigA, OP_0, the script.
// Signatures are BCH Schnorr (CHECKSIG accepts Schnorr since the 2019-05-15
// upgrade), 64 bytes plus the SIGHASH_ALL|FORKID type byte (0x41), under the
// BIP143-style FORKID sighash the existing HTLC path already uses. Alice
// pre-signs Bob's claim with a BCH Schnorr adaptor signature encrypted to
// T_secp, so Bob's claim publishes the signature from which Alice recovers t.
//
// Unlike BTC/LTC's Taproot point lock, a broadcast BCH claim's fee cannot be
// raised afterward (no replacement, no ancestor-fee-rate mining since BCHN
// v23.0.0), so Bob claims at the top pre-signed rung (UtxoFeeBump::NONE).

#pragma once

#include "SwapDaemon/Crypto/BchSchnorrAdaptor.h"
#include "UtxoFullNode.h"
#include "UtxoHtlc.h"
#include "UtxoPointLock.h"  // PointLockSpend, MAX_CLAIM_OFFERS, claimFeeLadder

namespace XfgSwap {

struct BchPointLockTerms {
  BchSchnorr::Point aliceKey{};
  BchSchnorr::Point bobKey{};
  uint32_t timeoutBlock = 0;
};

// One pre-signed claim at one fee; see PointLockClaimOffer's comment in
// UtxoPointLock.h. BCH never replaces a broadcast claim, so Bob claims at
// the top rung regardless of how many are offered.
struct BchPointLockClaimOffer {
  uint64_t fee = 0;
  BchSchnorr::AdaptorPresig presig{};
};

struct BchPointLockState {
  BchPointLockTerms terms;
  std::vector<BchPointLockClaimOffer> claimOffers;  // ascending fees
};

std::vector<uint8_t> bchPointLockScript(const BchPointLockTerms& terms);
std::vector<uint8_t> bchPointLockScriptPubKey(const BchPointLockTerms& terms);
std::string bchPointLockAddress(const UtxoNetworkParams& network, const BchPointLockTerms& terms);

bool bchPointLockClaimSighash(const BchPointLockTerms& terms, const PointLockSpend& spend,
                              BchSchnorr::Message& sighash);
bool bchPointLockRefundSighash(const BchPointLockTerms& terms, const PointLockSpend& spend,
                               BchSchnorr::Message& sighash);
bool bchPointLockClaimTransaction(const BchPointLockTerms& terms, const PointLockSpend& spend,
                                  const BchSchnorr::Signature& bobSignature,
                                  const BchSchnorr::Signature& aliceSignature, std::vector<uint8_t>& rawTx);
bool bchPointLockRefundTransaction(const BchPointLockTerms& terms, const PointLockSpend& spend,
                                   const BchSchnorr::Signature& aliceSignature, std::vector<uint8_t>& rawTx);
uint64_t bchPointLockSpendVirtualSize(bool claimPath);

// Every Schnorr signature with the FORKID hash type in the scriptSig that
// spends `lock`. A claim must carry Alice's completed signature among them,
// but the claimer chooses the encoding of everything around it, so the caller
// tests each against T instead of trusting a position.
std::vector<BchSchnorr::Signature> bchPointLockSpendSignatures(const std::vector<uint8_t>& rawTx,
                                                              const UtxoOutpoint& lock);

bool isBchPointLockState(const std::string& chainState);
std::string encodeBchPointLockState(const BchPointLockState& state);
bool decodeBchPointLockState(const std::string& chainState, BchPointLockState& state);

}  // namespace XfgSwap
