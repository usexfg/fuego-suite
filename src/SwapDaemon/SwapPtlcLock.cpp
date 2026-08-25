// Copyright (c) 2017-2026 Fuego Developers
#include "SwapPtlcLock.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"
#include <cstring>

namespace XfgSwap {

bool verifyPtlcPoint(const Crypto::SecretKey& t, const Crypto::PublicKey& T) {
  Crypto::PublicKey derived{};
  if (!Crypto::secret_key_to_public_key(t, derived))
    return false;
  return std::memcmp(&derived, &T, sizeof(Crypto::PublicKey)) == 0;
}

std::string ptlcPointToHex(const Crypto::PublicKey& T) {
  return Common::podToHex(T);
}

bool hexToPtlcPoint(const std::string& hex, Crypto::PublicKey& T) {
  return Common::podFromHex(hex, T);
}

SwapLockType negotiateLockType(bool localSupportsPtlc, bool peerSupportsPtlc, bool requirePtlc) {
  if (localSupportsPtlc && peerSupportsPtlc)
    return SwapLockType::PTLC;
  if (localSupportsPtlc || peerSupportsPtlc)
    return SwapLockType::PTLC_HTLC_BRIDGE;
  if (requirePtlc)
    return SwapLockType::PTLC; // caller must abort; we return PTLC to signal unmet requirement (caller checks requirePtlc && !cap)
  return SwapLockType::HTLC;
}

bool isPtlcNativePair(SwapPair pair) {
  return pair == SwapPair::XMR || pair == SwapPair::ZANO;
}

bool isZeroPubKey(const Crypto::PublicKey& k) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&k);
  for (size_t i = 0; i < sizeof(Crypto::PublicKey); ++i)
    if (p[i] != 0) return false;
  return true;
}

bool isZeroHash(const Crypto::Hash& h) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&h);
  for (size_t i = 0; i < sizeof(Crypto::Hash); ++i)
    if (p[i] != 0) return false;
  return true;
}

std::string lockTypeToString(SwapLockType t) {
  return swapLockTypeToString(t);
}

} // namespace XfgSwap
