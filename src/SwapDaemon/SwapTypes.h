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
#include "crypto/hash.h"
#include "crypto/crypto.h"
#include "crypto/adaptor.h"
#include "crypto/dleq.h"
#include "crypto/musig2.h"

namespace XfgSwap {

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

enum class SwapPair : uint8_t {
  SOL = 0,
  ETH = 1,
  XMR = 2,
  BCH = 3,
  ARB = 4,
  BASE = 5,
  KMD_SPV = 6,
  BNB = 7,
  DCR = 8,
  BTC = 9,
  LTC = 10,
  POLYGON = 11
};

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
};

struct SwapParams {
  std::string swapId;           // unique swap identifier (hex hash)
  SwapPair pair;
  SwapRole role;
  uint64_t xfgAmount;           // atomic units
  uint64_t ctrAmount;           // counterparty amount (atomic units)

  // Keys
  Crypto::PublicKey aliceXfgPubKey;
  Crypto::PublicKey bobXfgPubKey;

  // ── Adaptor signature fields ──
  Crypto::SecretKey ourSwapSecKey;     // our secret key for this swap
  Crypto::PublicKey ourSwapPubKey;     // our public key
  Crypto::PublicKey peerSwapPubKey;    // counterparty's public key
  Crypto::PublicKey escrowPubKey;      // Musig2 aggregated key (joint address)

  // Adaptor point: T = t*G (Bob generates, Alice verifies)
  Crypto::PublicKey adaptorPoint;
  Crypto::SecretKey adaptorSecret;     // t — known by Bob, revealed via ctr chain
  Crypto::PublicKey adaptorDleqQ;      // Q = t*escrowPubKey (second DLEQ point)
  Crypto::DLEQProof adaptorDleqProof; // proves T and Q share the same t

  // Musig2 session state
  Musig2State musig2;

  // Escrow tx on XFG chain
  Crypto::Hash escrowTxHash;
  uint32_t escrowOutputIndex = 0;      // global output index of escrow

  // ── Legacy HTLC fields (kept for backward compat) ──
  Crypto::Hash hashLock;
  Crypto::Hash preimage;        // known only by initiator until claim
  uint32_t xfgTimeoutHeight;
  uint64_t ctrTimeoutBlock;     // counterparty chain timeout

  // Chain state
  uint32_t htlcOutputIndex;     // global HTLC output index on Fuego
  std::string ctrLockTxId;      // counterparty lock tx hash
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

  // Our Round 1 contributions (must be restored across ticks — never regenerate
  // after ringOurRound1Sent, or the peer's copy of our nonce becomes invalid).
  Crypto::KeyImage             ringOurPartialKeyImage;
  Crypto::PublicKey            ringOurRingNoncePub;
  Crypto::EllipticCurvePoint   ringOurRingNonceHp;
  Crypto::EllipticCurveScalar  ringOurRingNonceSec;   // secret k; encrypted at rest
  Crypto::EllipticCurveScalar  ringOurPartialResponse;
  bool ringOurRound1MaterialValid = false;

  bool useSpvVerification = false;     // use SPV path for counterparty lock verification

  // Bob→Alice: adaptor preimage revealed out-of-band so Alice can claim CTR HTLC.
  bool adaptorSecretRevealedToPeer = false;  // Bob: we already sent SECRET_REVEAL
  bool adaptorSecretReceived = false;        // Alice: we received t from Bob

  std::vector<uint8_t> encBlob;        // encrypted adaptorSecret blob (from disk)
  std::vector<uint8_t> encSecKeyBlob;  // encrypted ourSwapSecKey blob (from disk)
  std::vector<uint8_t> encRingNonceBlob; // encrypted ringOurRingNonceSec blob
};

const char* swapStateToString(SwapState s);
const char* swapPairToString(SwapPair p);
SwapPair swapPairFromString(const std::string& s);
bool swapPairFromString(const std::string& s, SwapPair& out);

} // namespace XfgSwap
