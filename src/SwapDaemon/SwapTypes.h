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

  // ── ADAPTOR SWAP STATES (active — v1) ────────────────────────────────────
  //
  // Protocol (Alice sells XFG, Bob buys XFG):
  //   1. Both exchange pubkeys → Musig2 joint key P
  //   2. Bob picks adaptor secret t, publishes T = t*G + DLEQ proof
  //   3. Alice funds escrow: XFG → P (standard KeyOutput)
  //   4. Both exchange nonces + adaptor pre-sigs
  //   5. Bob locks counterparty coins (ETH/BCH HTLC or XMR adaptor)
  //   6. Alice claims counterparty → reveals t
  //   7. Bob adapts pre-sig → valid Musig2 sig → spends P → Bob
  //   8. Alice extracts t from on-chain tx
  //
  ADAPTOR_KEYS_EXCHANGED = 10,   // pubkeys shared, Musig2 key aggregated
  ADAPTOR_ESCROW_FUNDED  = 11,   // XFG sent to Musig2 joint address
  ADAPTOR_PRESIGS_READY  = 12,   // nonces exchanged, partial sigs created
  ADAPTOR_CTR_LOCKED     = 13,   // counterparty chain locked
  ADAPTOR_SECRET_REVEALED = 14,  // adaptor secret learned (from ctr chain claim)
  ADAPTOR_XFG_SPENT      = 15,   // adapted sig broadcast, escrow spent
  ADAPTOR_REFUNDED       = 16,   // cooperative refund completed

  // ── zkLPSWAP POOL STATES (v11 — deferred) ────────────────────────────────
  // These states are reserved for the trustless pool trading subsystem.
  // Not reachable from any active swap path in v1 builds.
  // Implementation lives in src/SwapDaemon/pool_v11/ — see README there.
  POOL_DEPOSIT_INITIATED  = 20,   // LP provider requests deposit
  POOL_DEPOSIT_LOCKED_A   = 21,   // Asset A locked in escrow
  POOL_DEPOSIT_LOCKED_B   = 22,   // Asset B locked in escrow
  POOL_DEPOSIT_CONFIRMED  = 23,   // Both locked, LP shares minted
  POOL_DEPOSIT_COMPLETE   = 24,   // Escrow spent, reserves updated
  POOL_DEPOSIT_REFUNDED   = 25,   // Deposit cancelled, assets returned

  POOL_WITHDRAW_INITIATED = 30,   // LP provider requests withdrawal
  POOL_WITHDRAW_LOCKED    = 31,   // Shares nullified, assets prepared
  POOL_WITHDRAW_COMPLETE  = 32,   // Assets returned to LP provider
  POOL_WITHDRAW_REFUNDED  = 33,   // Withdrawal cancelled

  POOL_SWAP_INITIATED     = 40,   // Swap order submitted
  POOL_SWAP_EXECUTED      = 41,   // Swap processed, reserves updated
  POOL_SWAP_COMPLETE      = 42,   // Atomic swap finished
  POOL_SWAP_REFUNDED      = 43,   // Swap cancelled

  POOL_FEE_CLAIM_INITIATED = 50,  // LP provider requests fee claim
  POOL_FEE_CLAIMED         = 51,  // Fees paid to LP provider
  POOL_FEE_CLAIM_REFUNDED  = 52,  // Fee claim cancelled

  POOL_CHECKPOINT_GENERATED = 60  // Checkpoint attestation generated
};

enum class SwapRole : uint8_t {
  ALICE = 0,  // Has counterparty coin, wants XFG
  BOB = 1     // Has XFG, wants counterparty coin
};

enum class SwapPair : uint8_t {
  SOL = 0,
  ETH = 1,
  XMR = 2,
  BCH = 3
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

  // Counterparty-specific
  std::string ctrAddress;       // counterparty chain address (SOL/ETH/XMR/BCH)
  std::string peerEndpoint;     // swap counterparty's network address

  // BCH-specific: hex-encoded P2SH redeem script for the HTLC.
  // Set when the BCH HTLC is created (lockHtlc) and read on claim/refund.
  // Must be persisted so claim/refund work after a daemon restart.
  std::string bchRedeemScriptHex;
};

const char* swapStateToString(SwapState s);
const char* swapPairToString(SwapPair p);
SwapPair swapPairFromString(const std::string& s);

} // namespace XfgSwap
