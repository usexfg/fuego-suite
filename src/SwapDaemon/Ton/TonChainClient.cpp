// Copyright (c) 2017-2026 Fuego Developers

#include "TonChainClient.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"

namespace XfgSwap {

namespace {
bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}
// TON HTLC uses SHA-256(preimage) — same as UTXO family.
std::string tonHashLockHex(const Crypto::SecretKey& t) {
  return bchHashLockHex(t);
}
} // namespace

TonChainClient::TonChainClient(std::unique_ptr<TonRpcClient> rpc,
                               const std::string& walletKeyHex)
  : m_rpc(std::move(rpc)), m_walletKeyHex(walletKeyHex) {}

ChainClientResult TonChainClient::lock(const SwapParams& params) {
  if (!m_rpc) return ChainClientResult::fail("TON lock: no RPC");

  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) {
    hashHex = tonHashLockHex(params.adaptorSecret);
  } else {
    bool nz = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nz = true; break; }
    if (!nz) return ChainClientResult::fail("TON lock: need adaptorSecret or hashLock");
    hashHex = Common::podToHex(params.hashLock);
  }

  std::string lockRef, err;
  bool ok = m_rpc->lockHtlc(m_walletKeyHex, params.ctrAddress, hashHex,
                            params.ctrTimeoutBlock, params.ctrAmount, lockRef, err);
  if (!ok) return ChainClientResult::fail(err.empty() ? "TON lock failed" : err);
  return ChainClientResult::ok(lockRef);
}

ChainClientResult TonChainClient::verifyLock(const SwapParams& params) {
  if (!m_rpc) return ChainClientResult::fail("TON verifyLock: no RPC");
  std::string addr = params.ctrLockTxId.empty() ? m_rpc->htlcAddress() : params.ctrLockTxId;
  if (addr.empty()) return ChainClientResult::fail("TON verifyLock: no HTLC address");

  TonHtlcState st;
  if (!m_rpc->getHtlcState(addr, st))
    return ChainClientResult::fail("TON verifyLock: get_state failed");
  if (st.claimed || st.refunded)
    return ChainClientResult::fail("TON verifyLock: already claimed/refunded");
  if (st.amountNano < params.ctrAmount)
    return ChainClientResult::fail("TON verifyLock: amount too low");

  std::string expectedHash;
  if (!isZeroSecret(params.adaptorSecret))
    expectedHash = tonHashLockHex(params.adaptorSecret);
  else {
    bool nz = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nz = true; break; }
    if (nz) expectedHash = Common::podToHex(params.hashLock);
  }
  if (!expectedHash.empty() && !st.hashLockHex.empty()) {
    auto lower = [](std::string s) {
      for (char& c : s) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
      return s;
    };
    if (lower(st.hashLockHex) != lower(expectedHash))
      return ChainClientResult::fail("TON verifyLock: hashlock mismatch");
  }
  return ChainClientResult::ok(addr);
}

ChainClientResult TonChainClient::claim(const SwapParams& params) {
  if (!m_rpc) return ChainClientResult::fail("TON claim: no RPC");
  std::string addr = params.ctrLockTxId.empty() ? m_rpc->htlcAddress() : params.ctrLockTxId;
  std::string claimRef, err;
  bool ok = m_rpc->claimHtlc(m_walletKeyHex, addr,
                             Common::podToHex(params.adaptorSecret), claimRef, err);
  if (!ok) return ChainClientResult::fail(err.empty() ? "TON claim failed" : err);
  return ChainClientResult::ok(claimRef);
}

ChainClientResult TonChainClient::refund(const SwapParams& params) {
  if (!m_rpc) return ChainClientResult::fail("TON refund: no RPC");
  std::string addr = params.ctrLockTxId.empty() ? m_rpc->htlcAddress() : params.ctrLockTxId;
  std::string refundRef, err;
  bool ok = m_rpc->refundHtlc(m_walletKeyHex, addr, refundRef, err);
  if (!ok) return ChainClientResult::fail(err.empty() ? "TON refund failed" : err);
  return ChainClientResult::ok(refundRef);
}

ChainClientResult TonChainClient::verifyReserveProof(const std::string&,
                                                     uint64_t,
                                                     const std::string&) {
  return ChainClientResult::fail("TON verifyReserveProof: not implemented");
}

std::string TonChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (!m_rpc) return {};
  std::string addr = params.ctrLockTxId.empty() ? m_rpc->htlcAddress() : params.ctrLockTxId;
  if (addr.empty()) return {};
  TonHtlcState st;
  if (!m_rpc->getHtlcState(addr, st)) return {};
  if (!st.claimed) return {};
  if (st.preimageHex.size() != 64) return {};
  bool any = false;
  for (char c : st.preimageHex) if (c != '0') { any = true; break; }
  if (!any) return {};
  return st.preimageHex;
}

bool TonChainClient::getCurrentHeight(uint64_t& height) {
  if (!m_rpc) return false;
  return m_rpc->getMasterchainSeqno(height);
}

} // namespace XfgSwap
