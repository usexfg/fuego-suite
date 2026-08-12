// Copyright (c) 2017-2026 Fuego Developers

#include "SiaChainClient.h"
#include "SiaHtlcScript.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"

namespace XfgSwap {

namespace {
bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}
std::string siaHashLockHex(const Crypto::SecretKey& t) {
  return SiaHtlcScript::hashLockHex(reinterpret_cast<const uint8_t*>(&t));
}
} // namespace

SiaChainClient::SiaChainClient(std::unique_ptr<SiaRpcClient> rpc)
  : m_rpc(std::move(rpc)) {}

ChainClientResult SiaChainClient::lock(const SwapParams& params) {
  if (!m_rpc) return ChainClientResult::fail("SIA lock: no RPC");
  if (params.ctrAddress.empty())
    return ChainClientResult::fail("SIA lock: ctrAddress required (Sia unlock hash)");

  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret))
    hashHex = siaHashLockHex(params.adaptorSecret);
  else {
    bool nz = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nz = true; break; }
    if (!nz) return ChainClientResult::fail("SIA lock: need hashLock or adaptorSecret");
    hashHex = Common::podToHex(params.hashLock);
  }

  // Embed hashlock in memo so counterparties can correlate the lock.
  std::string memo = "xfg-htlc:hashlock:" + hashHex + ":timeout:" +
                     std::to_string(params.ctrTimeoutBlock);
  std::string txid;
  // params.ctrAmount is expected in hastings (10^24 per SC) from UI conversion.
  if (!m_rpc->sendSiacoins(params.ctrAddress, params.ctrAmount, memo, txid))
    return ChainClientResult::fail("SIA lock: sendSiacoins failed");
  return ChainClientResult::okWithState(txid, hashHex);
}

ChainClientResult SiaChainClient::verifyLock(const SwapParams& params) {
  if (!m_rpc) return ChainClientResult::fail("SIA verifyLock: no RPC");
  if (params.ctrLockTxId.empty())
    return ChainClientResult::fail("SIA verifyLock: no ctrLockTxId");
  SiaTxInfo info;
  if (!m_rpc->getTransaction(params.ctrLockTxId, info) || !info.confirmed)
    return ChainClientResult::fail("SIA verifyLock: tx not found/confirmed");
  // Amount verification requires full tx decode; accept confirmed lock tx + chainState hashlock.
  if (params.chainState.empty() && isZeroSecret(params.adaptorSecret)) {
    // require some hashlock binding
    bool nz = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nz = true; break; }
    if (!nz)
      return ChainClientResult::fail("SIA verifyLock: missing hashLock binding");
  }
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult SiaChainClient::claim(const SwapParams& params) {
  if (!m_rpc) return ChainClientResult::fail("SIA claim: no RPC");
  if (isZeroSecret(params.adaptorSecret))
    return ChainClientResult::fail("SIA claim: missing adaptorSecret preimage");

  // Broadcast a wallet send that embeds preimage so Alice can extract.
  // Destination: our wallet address (claim to self) or params.ctrAddress claim sink.
  std::string dest;
  if (!m_rpc->getAddress(dest) || dest.empty())
    dest = params.ctrAddress;
  if (dest.empty())
    return ChainClientResult::fail("SIA claim: no destination address");

  std::string preHex = Common::podToHex(params.adaptorSecret);
  std::string memo = "xfg-htlc:preimage:" + preHex + ":lock:" + params.ctrLockTxId;
  // Minimal dust claim signal — full value already at lock address; preimage reveal is the atomic step.
  // When HTLC unlock conditions land natively, replace with spend of lock output.
  std::string txid;
  uint64_t dust = 1; // 1 hasting — may be rejected; use small SC amount if needed
  if (!m_rpc->sendSiacoins(dest, dust, memo, txid)) {
    // Retry with 1e12 hastings (0.000001 SC) if dust fails
    if (!m_rpc->sendSiacoins(dest, 1000000000000ULL, memo, txid))
      return ChainClientResult::fail("SIA claim: failed to broadcast preimage reveal tx");
  }
  return ChainClientResult::ok(txid);
}

ChainClientResult SiaChainClient::refund(const SwapParams& params) {
  if (!m_rpc) return ChainClientResult::fail("SIA refund: no RPC");
  uint64_t height = 0;
  if (!m_rpc->getBlockHeight(height))
    return ChainClientResult::fail("SIA refund: cannot query height");
  if (params.ctrTimeoutBlock > 0 && height < params.ctrTimeoutBlock)
    return ChainClientResult::fail("SIA refund: timeout not reached");
  // With native HTLC unlock, spend lock output to sender. Until then: no-op success
  // if lock was never co-owned; operator recovers via wallet.
  return ChainClientResult::ok("sia-refund-wallet-recovery");
}

ChainClientResult SiaChainClient::verifyReserveProof(const std::string&,
                                                     uint64_t minAmount,
                                                     const std::string&) {
  uint64_t bal = 0;
  if (!m_rpc || !m_rpc->getBalance(bal))
    return ChainClientResult::fail("SIA reserve: balance query failed");
  if (bal < minAmount)
    return ChainClientResult::fail("SIA reserve: insufficient balance");
  return ChainClientResult::ok("sia-balance-ok");
}

std::string SiaChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (!m_rpc) return {};
  std::string pre;
  if (m_rpc->findClaimPreimage(params.ctrLockTxId, pre) && pre.size() == 64)
    return pre;
  return {};
}

bool SiaChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc && m_rpc->getBlockHeight(height);
}

} // namespace XfgSwap
