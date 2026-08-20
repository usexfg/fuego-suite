#include "XmrChainClient.h"
#include "MoneroAddress.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"

#include <cstring>

extern "C" {
#include "crypto/crypto-ops.h"
}

namespace XfgSwap {

namespace {

bool hexToVec(const std::string& hex, std::vector<uint8_t>& out) {
  if (hex.size() != 64) return false;
  out.resize(32);
  for (size_t i = 0; i < 32; ++i) {
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

std::string vecToHex(const std::vector<uint8_t>& v) {
  static const char* hexc = "0123456789abcdef";
  std::string out;
  out.reserve(v.size() * 2);
  for (uint8_t b : v) { out += hexc[b >> 4]; out += hexc[b & 0xF]; }
  return out;
}

bool podToVec(const Crypto::PublicKey& pk, std::vector<uint8_t>& out) {
  out.assign(reinterpret_cast<const uint8_t*>(&pk),
             reinterpret_cast<const uint8_t*>(&pk) + sizeof(Crypto::PublicKey));
  return true;
}

bool scalarAdd(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b,
               std::vector<uint8_t>& out) {
  if (a.size() != 32 || b.size() != 32) return false;
  unsigned char sum[32];
  sc_add(sum, a.data(), b.data());
  bool zero = true;
  for (size_t i = 0; i < 32; ++i) if (sum[i]) { zero = false; break; }
  if (zero) return false; // a combined zero key would brick the sweep
  out.assign(sum, sum + 32);
  return true;
}

} // anonymous namespace

XmrChainClient::XmrChainClient(std::unique_ptr<MoneroRpcClient> rpc,
                               const std::string& spendKeyHex,
                               const std::string& viewKeyHex)
  : m_rpc(std::move(rpc)), m_spendKeyHex(spendKeyHex), m_viewKeyHex(viewKeyHex) {}

ChainClientResult XmrChainClient::lock(const SwapParams& params) {
  MoneroTransferResult xmrResult;
  bool ok = m_rpc->lockAdaptor(
      params.ctrAddress,
      params.ctrAmount,
      xmrResult);
  if (!ok || !xmrResult.success)
    return ChainClientResult::fail("XMR lockAdaptor failed: " + xmrResult.error);
  return ChainClientResult::ok(xmrResult.txHash);
}

ChainClientResult XmrChainClient::verifyLock(const SwapParams& params) {
  // Must scan the negotiated shared address with the SHARED view secret
  // (own + peer view secrets) — never the operator wallet's view key.
  if (!params.peerXmrKeysReceived)
    return ChainClientResult::fail("XMR verifyLock: peer XMR keys not received");
  std::vector<uint8_t> ownView, peerView, combinedView;
  if (!hexToVec(Common::podToHex(params.xmrViewSec), ownView) ||
      !hexToVec(Common::podToHex(params.peerXmrViewSec), peerView) ||
      !scalarAdd(ownView, peerView, combinedView)) {
    return ChainClientResult::fail("XMR verifyLock: shared view key derivation failed");
  }
  bool ok = m_rpc->verifyLock(params.ctrAddress, vecToHex(combinedView), params.ctrAmount);
  if (!ok) return ChainClientResult::fail("XMR lock not verified for shared address");
  return ChainClientResult::ok(params.ctrAddress);
}

bool XmrChainClient::combinedKeys(const SwapParams& params,
                                  std::vector<uint8_t>& combinedSpend,
                                  std::vector<uint8_t>& combinedView,
                                  std::string& error) const {
  if (!params.peerXmrShareReceived) {
    error = "peer spend share not revealed yet";
    return false;
  }
  std::vector<uint8_t> ownSpend, peerSpend;
  if (!hexToVec(Common::podToHex(params.xmrSpendSec), ownSpend) ||
      !hexToVec(Common::podToHex(params.peerXmrSpendShare), peerSpend) ||
      !scalarAdd(ownSpend, peerSpend, combinedSpend)) {
    error = "combined spend key derivation failed";
    return false;
  }

  // Sanity check: combined*G must equal the published A+B.
  ge_p3 p;
  ge_scalarmult_base(&p, combinedSpend.data());
  unsigned char derivedBytes[32];
  ge_p3_tobytes(derivedBytes, &p);
  std::vector<uint8_t> spendPubA, spendPubB, expected;
  podToVec(params.xmrSpendPub, spendPubA);
  podToVec(params.peerXmrSpendPub, spendPubB);
  if (!MoneroAddress::sharedSpendPub(spendPubA, spendPubB, expected) ||
      std::memcmp(derivedBytes, expected.data(), 32) != 0) {
    error = "combined spend key does not match published pubkeys";
    return false;
  }

  std::vector<uint8_t> ownView, peerView;
  if (!hexToVec(Common::podToHex(params.xmrViewSec), ownView) ||
      !hexToVec(Common::podToHex(params.peerXmrViewSec), peerView) ||
      !scalarAdd(ownView, peerView, combinedView)) {
    error = "combined view key derivation failed";
    return false;
  }
  return true;
}

std::string XmrChainClient::ownAddress() const {
  std::vector<uint8_t> spendSec, viewSec;
  if (!hexToVec(m_spendKeyHex, spendSec) || !hexToVec(m_viewKeyHex, viewSec)) return "";
  ge_p3 p;
  unsigned char spendPub[32], viewPub[32];
  ge_scalarmult_base(&p, spendSec.data());
  ge_p3_tobytes(spendPub, &p);
  ge_scalarmult_base(&p, viewSec.data());
  ge_p3_tobytes(viewPub, &p);
  return MoneroAddress::encode(
      std::vector<uint8_t>(spendPub, spendPub + 32),
      std::vector<uint8_t>(viewPub, viewPub + 32),
      MoneroAddress::MAINNET);
}

bool XmrChainClient::computeSharedAddress(const SwapParams& params, std::string& out) {
  if (!params.peerXmrKeysReceived) return false;
  std::vector<uint8_t> spendPubA, spendPubB, viewPubA, viewPubB;
  podToVec(params.xmrSpendPub, spendPubA);
  podToVec(params.peerXmrSpendPub, spendPubB);
  podToVec(params.xmrViewPub, viewPubA);
  podToVec(params.peerXmrViewPub, viewPubB);
  return m_rpc->createSharedAddress(vecToHex(spendPubA), vecToHex(spendPubB),
                                    vecToHex(viewPubA), vecToHex(viewPubB),
                                    out, MoneroAddress::MAINNET);
}

ChainClientResult XmrChainClient::claim(const SwapParams& params) {
  std::vector<uint8_t> combinedSpend, combinedView;
  std::string error;
  if (!combinedKeys(params, combinedSpend, combinedView, error))
    return ChainClientResult::fail("XMR claim: " + error);

  // Sweep the shared address to the operator's own XMR address. Never sweep
  // back to the shared address (that would re-lock the funds).
  const std::string dest = ownAddress();
  if (dest.empty())
    return ChainClientResult::fail("XMR claim: cannot derive own XMR address");

  MoneroTransferResult xmrResult;
  if (!m_rpc->sweepSharedAddress(vecToHex(combinedSpend), vecToHex(combinedView),
                                 dest, xmrResult)) {
    return ChainClientResult::fail("XMR claim sweep failed: " + xmrResult.error);
  }
  return ChainClientResult::ok(xmrResult.txHash);
}

ChainClientResult XmrChainClient::refund(const SwapParams& params) {
  std::vector<uint8_t> combinedSpend, combinedView;
  std::string error;
  if (!combinedKeys(params, combinedSpend, combinedView, error))
    return ChainClientResult::fail("XMR refund: " + error);

  const std::string dest = ownAddress();
  if (dest.empty())
    return ChainClientResult::fail("XMR refund: cannot derive own XMR address");

  MoneroTransferResult xmrResult;
  if (!m_rpc->sweepSharedAddress(vecToHex(combinedSpend), vecToHex(combinedView),
                                 dest, xmrResult)) {
    return ChainClientResult::fail("XMR refund sweep failed: " + xmrResult.error);
  }
  return ChainClientResult::ok(xmrResult.txHash);
}

ChainClientResult XmrChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("XMR reserve proof: invalid format (expected address:message:signature)");

  std::string address  = proof.substr(0, c1);
  std::string message  = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string signature = proof.substr(c2 + 1);

  bool good = false;
  uint64_t total = 0;
  if (!m_rpc->checkReserveProof(address, message, signature, good, total))
    return ChainClientResult::fail("XMR reserve proof: RPC call failed");
  if (!good)
    return ChainClientResult::fail("XMR reserve proof: invalid signature");
  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("XMR reserve proof: message not bound to this offer");
  if (total < minAmount)
    return ChainClientResult::fail("XMR reserve proof: insufficient balance (" +
                                   std::to_string(total) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool XmrChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc->getHeight(height);
}

} // namespace XfgSwap
