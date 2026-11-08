// Copyright (c) 2017-2026 Fuego Developers
#pragma once
#include "SwapTypes.h"
#include "crypto/crypto.h"
#include <string>

namespace XfgSwap {

// ── PTLC helper ────────────────────────────────────────────────────────
// Bridges PTLC point-lock T = t*G with legacy HTLC hash-lock H(t) and
// handles negotiation between PTLC-capable and HTLC-only chains.

// Verify t*G == T (public key derivation matches adaptorPoint/ptlcPoint).
bool verifyPtlcPoint(const Crypto::SecretKey& t, const Crypto::PublicKey& T);

// Hex helpers for point (64 hex, 32 bytes PublicKey).
std::string ptlcPointToHex(const Crypto::PublicKey& T);
bool hexToPtlcPoint(const std::string& hex, Crypto::PublicKey& T);

// Negotiate lockType given local/peer PTLC caps and requirePtlc policy.
//   both PTLC  → PTLC
//   one PTLC   → PTLC_HTLC_BRIDGE (keeps H(t) for HTLC leg + point for XFG)
//   none PTLC + requirePtlc → caller must treat as failure (return HTLC but check)
//   none PTLC + !require → HTLC
SwapLockType negotiateLockType(bool localSupportsPtlc, bool peerSupportsPtlc, bool requirePtlc);

// V2 negotiation (PTLC_PURE_PLAN P4.2): pure point-lock when BOTH legs report
// PURE PTLC support (no H(t) anywhere); otherwise falls back to the exact
// legacy 3-arg negotiation above.
//   both pure → PTLC (pure T everywhere)
//   else      → negotiateLockType(localSupportsPtlc, peerSupportsPtlc, requirePtlc)
SwapLockType negotiateLockTypeV2(bool localSupportsPtlc, bool peerSupportsPtlc,
                                 bool localSupportsPurePtlc, bool peerSupportsPurePtlc,
                                 bool requirePtlc);

// True for native adaptor-only pairs (no hashlock; ptlcPoint==ZERO means PTLC).
bool isPtlcNativePair(SwapPair pair);
bool isZeroPubKey(const Crypto::PublicKey& k);
bool isZeroHash(const Crypto::Hash& h);

// For logging / wire
std::string lockTypeToString(SwapLockType t);

} // namespace XfgSwap
