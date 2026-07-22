#include "SolChainClient.h"
#include "Common/StringTools.h"
#include "../Crypto/Base58Std.h"
#include "../Crypto/Ed25519Verify.h"
#include "../SwapHashLock.h"

namespace XfgSwap {

static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

SolChainClient::SolChainClient(std::unique_ptr<SolRpcClient> rpc, const std::string& keypairBase58)
  : m_rpc(std::move(rpc)), m_keypairBase58(keypairBase58) {}

ChainClientResult SolChainClient::lock(const SwapParams& params) {
  // The Solana xfg_htlc program verifies keccak256(preimage) == hash_lock, and
  // claim() reveals the adaptor secret t as the preimage. So the hashlock MUST
  // be keccak256(t) — NOT the adaptor point T = t*G. Committing T (the old bug)
  // made every claim fail with InvalidPreimage; funds could only be refunded.
  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) {
    hashHex = solHashLockHex(params.adaptorSecret);
  } else {
    bool nz = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nz = true; break; }
    if (!nz)
      return ChainClientResult::fail("SOL lock: need adaptor secret or hashLock H(t) from Bob");
    hashHex = Common::podToHex(params.hashLock);
  }

  SolTxResult solResult;
  bool ok = m_rpc->lock(
      m_keypairBase58,
      params.ctrAddress,
      hashHex,
      params.ctrTimeoutBlock,
      params.ctrAmount,
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL lock failed: " + solResult.error);
  // The canonical reference for verifyLock/claim is the HTLC state PDA, NOT
  // the lock-tx signature. claim() base58-decodes this and requires 32 bytes.
  if (solResult.htlcAddress.empty())
    return ChainClientResult::fail("SOL lock: could not derive HTLC account address");
  return ChainClientResult::ok(solResult.htlcAddress);
}

ChainClientResult SolChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("SOL lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult SolChainClient::claim(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->claim(
      m_keypairBase58,
      params.ctrLockTxId,
      Common::podToHex(params.adaptorSecret),
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL claim failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

std::string SolChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  // Alice-locks: Bob's claim writes preimage t into the HTLC state account.
  // ctrLockTxId is the HTLC PDA (not the lock signature) after a successful lock().
  if (params.ctrLockTxId.empty() || !m_rpc) return {};
  SolHtlcInfo info;
  if (!m_rpc->getHtlcState(params.ctrLockTxId, info)) return {};
  if (!info.claimed) return {};
  // Reject all-zero / empty preimage
  if (info.preimage.empty() || info.preimage.size() != 64) return {};
  bool any = false;
  for (char c : info.preimage) {
    if (c != '0') { any = true; break; }
  }
  if (!any) return {};
  return info.preimage;
}

ChainClientResult SolChainClient::refund(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->refund(
      m_keypairBase58,
      params.ctrLockTxId,
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL refund failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

ChainClientResult SolChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("SOL reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string sigB58    = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  std::vector<uint8_t> pubkeyBytes = Base58Std::decode(address);
  if (pubkeyBytes.size() != 32)
    return ChainClientResult::fail("SOL reserve proof: invalid pubkey length (" +
                                   std::to_string(pubkeyBytes.size()) + ")");

  std::vector<uint8_t> sigBytes = Base58Std::decode(sigB58);
  if (sigBytes.size() != 64)
    return ChainClientResult::fail("SOL reserve proof: invalid signature length (" +
                                   std::to_string(sigBytes.size()) + ")");

  if (!Ed25519Verify::verify(pubkeyBytes.data(), message, sigBytes.data()))
    return ChainClientResult::fail("SOL reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("SOL reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("SOL reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("SOL reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool SolChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc->getSlot(height);
}

} // namespace XfgSwap
