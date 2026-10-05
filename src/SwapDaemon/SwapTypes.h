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

#include <string>
#include <cstdint>
#include <vector>
#include <functional>
#include <array>
#include <limits>
#include <stdexcept>
#include <boost/multiprecision/cpp_int.hpp>
#include "SwapPairCatalog.h"
#include "crypto/hash.h"
#include "crypto/crypto.h"
#include "crypto/adaptor.h"
#include "crypto/dleq.h"
#include "crypto/musig2.h"

namespace XfgSwap {

// Counterparty chains do not share Fuego's 64-bit money representation. EVM
// contracts use uint256 and 18-decimal native assets exceed uint64 at only
// 18.446744073709551615 whole units. Keep the counterparty amount at its
// native atomic precision and narrow only inside adapters whose consensus
// amount is genuinely uint64 (UTXO, Solana, CryptoNote, etc.).
using AtomicAmount = boost::multiprecision::uint256_t;

inline std::string atomicAmountToString(const AtomicAmount& amount) {
  return amount.convert_to<std::string>();
}

inline bool parseAtomicAmount(const std::string& text, AtomicAmount& amount) {
  if (text.empty()) return false;
  if (text.size() > 1 && text.front() == '0') return false;
  AtomicAmount parsed = 0;
  const AtomicAmount max = (std::numeric_limits<AtomicAmount>::max)();
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    const unsigned digit = static_cast<unsigned>(c - '0');
    if (parsed > (max - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  amount = parsed;
  return true;
}

inline bool atomicAmountToUint64(const AtomicAmount& amount, uint64_t& out) {
  if (amount > (std::numeric_limits<uint64_t>::max)()) return false;
  out = amount.convert_to<uint64_t>();
  return true;
}

inline std::array<uint8_t, 32> atomicAmountToBigEndian(const AtomicAmount& amount) {
  std::array<uint8_t, 32> bytes{};
  AtomicAmount remaining = amount;
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[bytes.size() - 1 - i] =
        static_cast<uint8_t>((remaining & 0xff).convert_to<unsigned>());
    remaining >>= 8;
  }
  return bytes;
}

inline AtomicAmount atomicAmountFromBigEndian(const uint8_t* bytes, size_t size) {
  AtomicAmount amount = 0;
  for (size_t i = 0; i < size; ++i) {
    amount <<= 8;
    amount += bytes[i];
  }
  return amount;
}

enum class SwapState : uint8_t {
  // ── Legacy HTLC flow (inactive) ──
  INITIATED = 0,
  XFG_LOCKED = 1,       // Bob created HTLC on Fuego
  CTR_LOCKED = 2,       // Alice locked on counterparty chain (SOL/ETH/XMR/BCH)
  XFG_CLAIMED = 3,      // Alice claimed XFG (preimage revealed)
  CTR_CLAIMED = 4,      // Bob claimed on counterparty chain
  XFG_REFUNDED = 5,     // Bob refunded XFG (timeout)
  CTR_REFUNDED = 6,     // Alice refunded counterparty chain (timeout)
  FAILED = 7,

  // ── ADAPTOR SWAP STATES (active — v1, Alice-locks) ───────────────────────
  //
  // Protocol (Alice has CTR, wants XFG; Bob has XFG, wants CTR):
  //   1. Both exchange pubkeys → Musig2 joint key P
  //   2. Bob picks adaptor secret t, publishes T = t*G + DLEQ proof + H(t)
  //   3. Bob funds escrow: XFG → P (standard KeyOutput)
  //   4. Both exchange nonces + adaptor pre-sigs
  //   5. Alice locks counterparty with H(t) (does not know t)
  //   6. Bob claims CTR with preimage t  → on-chain secret reveal
  //   7. Alice extracts t via tryExtractClaimedSecret
  //   8. Bob spends XFG escrow (adapted / collaborative ring)
  //
  ADAPTOR_KEYS_EXCHANGED = 10,   // pubkeys shared, Musig2 key aggregated
  ADAPTOR_ESCROW_FUNDED  = 11,   // XFG sent to Musig2 joint address
  ADAPTOR_PRESIGS_READY  = 12,   // nonces exchanged, partial sigs created
  ADAPTOR_CTR_LOCKED     = 13,   // counterparty chain locked
  ADAPTOR_SECRET_REVEALED = 14,  // t learned (Bob claimed CTR / Alice extracted)
  ADAPTOR_XFG_SPENT      = 15,   // adapted sig broadcast, escrow spent
  ADAPTOR_REFUNDED       = 16,   // cooperative refund completed

  // ── SPV CONFIRMATION STATES (v1.1) ─────────────────────────────────────────
  ADAPTOR_WAITING_SPV           = 17, // waiting for SPV confirmation of counterparty's lock tx
  ADAPTOR_SECRET_CONFIRMED_SPV  = 18, // secret confirmed + SPV verified

  // ── AFK ADAPTOR SWAP STATES (v2) ───────────────────────────────────────────
  // Non-interactive "Pre-lock" flow for AFK makers
  AFK_OFFER_LOCKED   = 100,  // Maker locked XFG on-chain with adaptor sig
  AFK_OFFER_ACCEPTED = 101,  // Taker locked coins on counterparty chain
  AFK_CLAIMED        = 102,  // Both sides claimed (swap finished)
  AFK_REFUNDED       = 103,  // Maker refunded XFG after timeout
};

enum class SwapRole : uint8_t {
  ALICE = 0,  // Has counterparty coin, wants XFG
  BOB = 1     // Has XFG, wants counterparty coin
};

enum class SwapLockType : uint8_t {
  HTLC = 0,              // legacy hashlock H(t) — HTLC scripts/contracts
  PTLC = 1,              // native point lock T, adaptor verify (Taproot/Schnorr)
  PTLC_HTLC_BRIDGE = 2   // PTLC on XFG leg, HTLC H(t) on CTR leg bridged via DLEQ + H(t)
};

// SHA-256 hashlock family: UTXO chains whose HTLC scripts verify OP_SHA256,
// plus TON (cell-hash preimage convention). Used to select H(t) derivation in
// adaptor_generate_adaptor and the claim-side binding check.
//
// Divergence from origin/dashpltc's helper of the same name:
//   - BTC and TON are INCLUDED here (their bridge HTLCs are OP_SHA256 /
//     cell-hash; dashpltc omitted them because its BTC leg had already gone
//     pure point-lock).
//   - GLEEC is EXCLUDED: on master GLEEC is an EVM client (GleecChainClient),
//     so it must take the keccak256 default, not SHA-256.
//   - SIA is EXCLUDED: Sia uses Blake2b-256(preimage), handled explicitly
//     alongside this predicate.
inline bool isLegacyUtxoPair(SwapPair p) {
  switch (p) {
    case SwapPair::BCH:
    case SwapPair::BTC:
    case SwapPair::LTC:
    case SwapPair::DCR:
    case SwapPair::KMD_SPV:
    case SwapPair::DOGE:
    case SwapPair::DASH:
    case SwapPair::ZEC:
    case SwapPair::TON:
      return true;
    default:
      return false;
  }
}

// Musig2 session state persisted across swap steps.
struct Musig2State {
  Crypto::Musig2KeyAgg keyAgg;
  Crypto::Musig2SecNonce ourSecNonce;
  Crypto::Musig2PubNonce ourPubNonce;
  Crypto::Musig2PubNonce peerPubNonce;
  Crypto::Musig2AggNonce aggNonce;
  Crypto::Musig2Session  session;
  Crypto::Musig2PartialSig ourPartialSig;
  Crypto::Musig2PartialSig peerPartialSig;
  bool nonceGenerated = false;
  bool sessionInitialized = false;

  // ── Presig-round progress flags (persisted) ──
  // These make the ESCROW_FUNDED → PRESIGS_READY round idempotent across
  // daemon restarts. NEVER regenerate a nonce once nonceGenerated is true —
  // the peer may already hold our pub nonce (regenerating = nonce-reuse class
  // key leak). partial_sign zeroes ourSecNonce after use; partialSigGenerated
  // records that so a reloaded swap does not try to sign again.
  bool nonceSent            = false;  // our pub nonce sent to peer
  bool partialSigGenerated  = false;  // our partial sig created (nonce consumed)
  bool partialSigSent       = false;  // our partial sig sent to peer
  bool peerPartialSigVerified = false;  // stored peer partial sig passed verification
};

struct SwapParams {
  std::string swapId;           // unique swap identifier (hex hash)
  SwapPair pair;
  SwapRole role;
  uint64_t xfgAmount;           // atomic units
  AtomicAmount ctrAmount;       // counterparty amount in native atomic units
  // Version 1 records predate full-width counterparty amounts. New swaps use
  // version 2 and bind the amount in the signed key exchange before funding.
  uint8_t amountProtocolVersion = 2;

  uint64_t ctrAmount64() const {
    uint64_t narrowed = 0;
    if (!atomicAmountToUint64(ctrAmount, narrowed)) {
      throw std::overflow_error("counterparty amount exceeds this chain's uint64 atomic limit");
    }
    return narrowed;
  }

  // Keys
  Crypto::PublicKey aliceXfgPubKey;
  Crypto::PublicKey bobXfgPubKey;

  // ── Adaptor signature fields ──
  Crypto::SecretKey ourSwapSecKey;     // our secret key for this swap
  Crypto::PublicKey ourSwapPubKey;     // our public key
  Crypto::PublicKey peerSwapPubKey;    // counterparty's public key
  Crypto::PublicKey expectedPeerSwapPubKey; // anti-griefing: expected peer key, bound on first KEY_EXCHANGE
  Crypto::PublicKey escrowPubKey;      // Musig2 aggregated key (joint address)

  // Adaptor point: T = t*G (Bob generates, Alice verifies)
  Crypto::PublicKey adaptorPoint;
  Crypto::SecretKey adaptorSecret;     // t — known by Bob, revealed via ctr chain
  Crypto::PublicKey adaptorDleqQ;      // Q = t*escrowPubKey (second DLEQ point)
  Crypto::DLEQProof adaptorDleqProof; // proves T and Q share the same t

  // ── PTLC / fallback ──
  SwapLockType lockType = SwapLockType::HTLC; // negotiated per swap
  Crypto::PublicKey ptlcPoint{};              // T duplicated for pure PTLC (same as adaptorPoint), kept for explicit PTLC verify
  bool requirePtlc = false;                   // sender policy: abort if peer cannot do PTLC

  // Cross-curve secp256k1 point T_secp = t·G_secp (Bob publishes, Alice
  // verifies). Compressed 33-byte hex (66 chars); empty = not published.
  // Derived from adaptorSecret with the canonical byte-reversal rule.
  std::string secpPubHex;

  // Musig2 session state
  Musig2State musig2;

  // Escrow tx on XFG chain
  Crypto::Hash escrowTxHash;
  uint32_t escrowOutputIndex = 0;      // global output index of escrow (legacy ring path)
  std::string escrowClaimSigHex;       // completed adaptor aggregate (persisted:
                                       // adaptor_aggregate zeroes adaptorSecret)

  // ── Legacy HTLC fields (kept for backward compat) ──
  Crypto::Hash hashLock;
  Crypto::Hash preimage;        // known only by initiator until claim
  uint32_t xfgTimeoutHeight;
  uint64_t ctrTimeoutBlock;     // counterparty chain timeout

  // Chain state
  uint32_t htlcOutputIndex;     // global HTLC output index on Fuego
  std::string ctrLockTxId;      // counterparty lock tx hash
  std::string ctrClaimTxId;     // counterparty claim tx hash (Bob's on-chain claim of CTR)
  // Persisted before calling claim(): an RPC failure may mean the transaction
  // was submitted but its response was lost. While true, absence of a txid is
  // never evidence that Bob did not claim the counterparty leg.
  bool ctrClaimAttempted = false;
  // Written before Alice calls refund(). A lost RPC response leaves the
  // outcome unknown and must not generate a different refund transaction.
  bool ctrRefundAttempted = false;
  bool ctrRefundSubmitted = false;
  std::string ctrRefundTxId;
  bool escrowRefundBroadcast = false;
  // Signed once and persisted before broadcast so a lost RPC response retries
  // the same XFG transaction instead of generating a conflicting refund.
  std::string escrowRefundTxHex;
  std::string escrowRefundTxId;
  // Direct claim is signed and persisted before send, then retried unchanged
  // until Fuego confirms it. Broadcast alone is not swap completion.
  std::string escrowClaimTxHex;
  std::string escrowClaimTxId;
  bool escrow_claim_fee_report_attempted = false;
  bool escrow_refund_fee_report_attempted = false;
  uint32_t requiredConfirmations = 6;  // SPV confirmations required (default 6 for BCH)

  // Counterparty-specific
  std::string ctrAddress;       // counterparty chain address (SOL/ETH/XMR/BCH)
  std::string ctrPubKey;        // counterparty's compressed pubkey for HTLC claim (hex, 66 chars)
  std::string peerEndpoint;     // swap counterparty's network address

  std::string chainState;

  // Protocol fee: initiation 1% + claim 1% = 2% total for claims, 1% initiation for refunds.
  // Set by SwapDaemon before calling buildUnsignedEscrowSpend.
  uint64_t protocolFee = 0;                // protocol fee in atomic units (0 = no fee)
  Crypto::PublicKey treasuryPubKey;        // treasury output key (from vault derivation)

  // ── Collaborative ring signature peer state (persisted for restart resilience)
  // These are populated by handlePeerMessage() when the peer sends Ring Round 1/2
  // data.  They survive daemon restarts so the collaborative ring sig can
  // complete even if the daemon goes down mid-round.
  Crypto::KeyImage             ringPeerPartialKeyImage;
  Crypto::PublicKey            ringPeerRingNoncePub;
  Crypto::EllipticCurvePoint   ringPeerRingNonceHp;
  Crypto::EllipticCurveScalar  ringPeerPartialResponse;
  bool ringPeerRound1Received = false;
  bool ringPeerRound2Received = false;
  bool ringOurRound1Sent = false;      // we already generated & sent Round 1
  bool ringOurRound2Sent = false;      // we already generated & sent Round 2
  bool ringTxBroadcast = false;        // escrow spend/refund tx was broadcast

  // Agreed decoy ring: the exact sorted ring (global indexes + public keys,
  // including the escrow entry) both parties sign. Set by whoever builds
  // Ring Round 1, persisted for restart resilience, verified on receipt.
  std::vector<uint32_t> ringGlobalIndices;
  std::vector<Crypto::PublicKey> ringPubKeys;
  size_t ringRealIndex = 0;
  bool ringDescriptorValid = false;

  // Our Round 1 contributions (must be restored across ticks — never regenerate
  // after ringOurRound1Sent, or the peer's copy of our nonce becomes invalid).
  Crypto::KeyImage             ringOurPartialKeyImage;
  Crypto::PublicKey            ringOurRingNoncePub;
  Crypto::EllipticCurvePoint   ringOurRingNonceHp;
  Crypto::EllipticCurveScalar  ringOurRingNonceSec;   // secret k; encrypted at rest
  Crypto::EllipticCurveScalar  ringOurPartialResponse;
  bool ringOurRound1MaterialValid = false;

  bool useSpvVerification = false;     // use SPV path for counterparty lock verification

  // Bob→Alice escrow funding notification (ESCROW_FUNDED peer message sent).
  bool escrowFundedSent = false;

  // ── AFK completion state (maker side) ──
  // Populated by the AFK_CLAIM peer message (taker → maker).
  bool afkClaimReceived = false;
  std::string afkClaimCtrLockTxId;   // taker's CTR lock tx id
  std::string afkClaimPayoutAddress; // taker's XFG payout address
  std::string afkClaimFinalSigHex;   // final-signature proof of claim (hex)

  // ── AFK pre-lock material (maker generates; taker receives via the fill
  // result and the initiate_swap RPC) ──
  std::string afkPreSigHex;   // maker's adaptor pre-signature (hex)

  // Bob→Alice: adaptor preimage revealed out-of-band so Alice can claim CTR HTLC.
  bool adaptorSecretRevealedToPeer = false;  // Bob: we already sent SECRET_REVEAL
  bool adaptorSecretReceived = false;        // Alice: we received t from Bob

  // ── XMR (CryptoNote adaptor) leg ──
  // Per-swap XMR keypairs. The shared address's spend key is
  // xmrSpendPub + peerXmrSpendPub; both parties hold their own spend secret
  // and receive the peer's only via XMR_SHARE_REVEAL (Alice reveals after
  // the XFG claim confirms; Bob reveals after the timeout). The VIEW secret
  // is shared in XMR_KEYS — read-only, required for scanning.
  Crypto::SecretKey xmrSpendSec;
  Crypto::PublicKey xmrSpendPub;
  Crypto::SecretKey xmrViewSec;
  Crypto::PublicKey xmrViewPub;
  Crypto::PublicKey peerXmrSpendPub;
  Crypto::PublicKey peerXmrViewPub;
  Crypto::SecretKey peerXmrViewSec;
  Crypto::SecretKey peerXmrSpendShare;   // revealed by the peer (spend secret)
  bool xmrKeysGenerated = false;
  bool xmrKeysSent = false;
  bool peerXmrKeysReceived = false;
  bool peerXmrShareReceived = false;
  bool xmrShareSent = false;

  std::vector<uint8_t> encBlob;        // encrypted adaptorSecret blob (from disk)
  std::vector<uint8_t> encSecKeyBlob;  // encrypted ourSwapSecKey blob (from disk)
  std::vector<uint8_t> encRingNonceBlob; // encrypted ringOurRingNonceSec blob
  std::vector<uint8_t> encMusig2NonceBlob; // encrypted musig2.ourSecNonce blob (64 bytes)
  std::vector<uint8_t> encXmrSpendBlob;  // encrypted xmrSpendSec blob (from disk)
  std::vector<uint8_t> encXmrViewBlob;   // encrypted xmrViewSec blob (from disk)
  std::vector<uint8_t> encPeerXmrShareBlob; // encrypted peerXmrSpendShare blob (from disk)
};

const char* swapStateToString(SwapState s);
const char* swapPairToString(SwapPair p);
SwapPair swapPairFromString(const std::string& s);
bool swapPairFromString(const std::string& s, SwapPair& out);
const char* swapLockTypeToString(SwapLockType t);
bool swapLockTypeFromString(const std::string& s, SwapLockType& out);

} // namespace XfgSwap
