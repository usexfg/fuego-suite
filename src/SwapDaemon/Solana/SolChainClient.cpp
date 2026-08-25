#include "SolChainClient.h"
#include <stdexcept>
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

std::string SolChainClient::getReceiveAddress() const {
  // The keypair (64 bytes base58) holds the secret key first, then the
  // public key. Return the public key as a base58 address.
  try {
    std::vector<uint8_t> raw = Base58Std::decode(m_keypairBase58);
    if (raw.size() < 64) return "";
    std::vector<uint8_t> pub(raw.begin() + 32, raw.begin() + 64);
    return Base58Std::encode(pub);
  } catch (const std::exception&) {
    return "";
  }
}

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

ChainClientResult SolChainClient::lockPtlc(const SwapParams& params) {
  // PTLC on SOL: ed25519 adaptor point commitment off-chain; on-chain still uses HTLC hash for now (bridge).
  // Store ptlcPoint in chainState suffix for verifier: htlcAddress|ptlcPointHex
  ChainClientResult base = lock(params);
  if (!base.success) return base;
  Crypto::PublicKey pt = params.ptlcPoint; Crypto::PublicKey zero{}; std::memset(&zero,0,sizeof(zero));
  if (std::memcmp(&pt,&zero,sizeof(zero))==0) pt=params.adaptorPoint;
  std::string ptHex = Common::podToHex(pt);
  std::string state = base.chainState + "|ptlc:" + ptHex;
  return ChainClientResult::okWithState(base.txId, state);
}

ChainClientResult SolChainClient::verifyLock(const SwapParams& params) {
  SolHtlcInfo info;
  if (!m_rpc->getHtlcState(params.ctrLockTxId, info))
    return ChainClientResult::fail("SOL verifyLock: cannot read HTLC account state");
  if (info.claimed || info.refunded)
    return ChainClientResult::fail("SOL verifyLock: HTLC already claimed/refunded");
  if (info.amount < params.ctrAmount)
    return ChainClientResult::fail("SOL verifyLock: amount too low");

  // Expected hashlock: H(t) from secret or published hashLock.
  std::string expectedHash;
  if (!isZeroSecret(params.adaptorSecret)) {
    expectedHash = solHashLockHex(params.adaptorSecret);
  } else {
    bool nz = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nz = true; break; }
    if (!nz)
      return ChainClientResult::fail("SOL verifyLock: no hashLock or adaptorSecret");
    expectedHash = Common::podToHex(params.hashLock);
  }
  // Normalize hex case for compare
  auto lower = [](std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return s;
  };
  if (lower(info.hashLock) != lower(expectedHash))
    return ChainClientResult::fail("SOL verifyLock: hash_lock mismatch");

  if (params.ctrTimeoutBlock > 0 && info.timeoutSlot != 0 &&
      info.timeoutSlot != params.ctrTimeoutBlock) {
    return ChainClientResult::fail("SOL verifyLock: timeout_slot mismatch");
  }
  if (!params.ctrAddress.empty() && !info.recipient.empty() &&
      lower(info.recipient) != lower(params.ctrAddress)) {
    // Recipient may be base58; case-sensitive compare preferred for base58
    if (info.recipient != params.ctrAddress)
      return ChainClientResult::fail("SOL verifyLock: recipient mismatch");
  }
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
