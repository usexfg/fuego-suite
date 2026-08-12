#include "EthChainClient.h"
#include "Common/StringTools.h"
#include "crypto/keccak.h"
#include "../Crypto/Secp256k1Signer.h"
#include "../SwapHashLock.h"
#include <stdexcept>
#include <cctype>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace XfgSwap {

namespace {
bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) {
    if (p[i]) return false;
  }
  return true;
}
} // namespace

EthChainClient::EthChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address,
                               const std::string& chainName)
  : m_rpc(std::move(rpc)), m_address(address), m_chainName(chainName) {}

ChainClientResult EthChainClient::lock(const SwapParams& params) {
  try {
    // Hashlock MUST be keccak256(adaptorSecret) / params.hashLock from Bob.
    // Alice-locks model: Alice has hashLock (H(t)) but not t; Bob has t for claim.
    std::string hashHex;
    if (!isZeroSecret(params.adaptorSecret)) {
      hashHex = ethHashLockHex(params.adaptorSecret);
    } else {
      // Use Bob's published H(t)
      bool nonzero = false;
      for (size_t i = 0; i < sizeof(params.hashLock); ++i)
        if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nonzero = true; break; }
      if (!nonzero)
        return ChainClientResult::fail(m_chainName + " lock: no adaptor secret or hashLock");
      hashHex = Common::podToHex(params.hashLock);
    }

    std::string contractAddress;
    bool ok = m_rpc->deployHtlc(
        m_address,
        params.ctrAddress,
        hashHex,
        params.ctrTimeoutBlock,
        params.ctrAmount,
        contractAddress);
    if (!ok) return ChainClientResult::fail(m_chainName + " deployHtlc failed");
    return ChainClientResult::ok(contractAddress);
  } catch (const std::runtime_error& e) {
    auto r = ChainClientResult::fail(m_chainName + " lock error: " + e.what());
    r.fatal = true;
    return r;
  }
}

ChainClientResult EthChainClient::verifyLock(const SwapParams& params) {
  // ctrLockTxId holds the HashedTimelock contractId (not a tx hash).
  // Verify amount + recipient + hashlock + not claimed/refunded via getContract.
  // Prefer H(t) from adaptorSecret; Alice has only published hashLock — use that.
  std::string expectedHash;
  if (!isZeroSecret(params.adaptorSecret)) {
    expectedHash = ethHashLockHex(params.adaptorSecret);
  } else {
    bool nonzero = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nonzero = true; break; }
    if (nonzero)
      expectedHash = Common::podToHex(params.hashLock);
  }
  if (expectedHash.empty())
    return ChainClientResult::fail(m_chainName + " verifyLock: no hashLock or adaptorSecret");
  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount,
                              params.ctrAddress, expectedHash);
  if (!ok) return ChainClientResult::fail(m_chainName + " lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult EthChainClient::claim(const SwapParams& params) {
  try {
    std::string claimTxHash;
    bool ok = m_rpc->claimHtlc(
        m_address,
        params.ctrLockTxId,
        Common::podToHex(params.adaptorSecret),
        claimTxHash);
    if (!ok) return ChainClientResult::fail(m_chainName + " claimHtlc failed");
    return ChainClientResult::ok(claimTxHash);
  } catch (const std::runtime_error& e) {
    auto r = ChainClientResult::fail(m_chainName + " claim error: " + e.what());
    r.fatal = true;
    return r;
  }
}

ChainClientResult EthChainClient::refund(const SwapParams& params) {
  try {
    std::string refundTxHash;
    bool ok = m_rpc->refundHtlc(
        m_address,
        params.ctrLockTxId,
        refundTxHash);
    if (!ok) return ChainClientResult::fail(m_chainName + " refundHtlc failed");
    return ChainClientResult::ok(refundTxHash);
  } catch (const std::runtime_error& e) {
    auto r = ChainClientResult::fail(m_chainName + " refund error: " + e.what());
    r.fatal = true;
    return r;
  }
}

ChainClientResult EthChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail(m_chainName + " reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string sigHex    = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  // Parse 65-byte recoverable signature: r(32) + s(32) + v(1)
  if (sigHex.size() != 130)
    return ChainClientResult::fail(m_chainName + " reserve proof: signature must be 65 bytes hex (130 chars)");

  auto hexNibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };

  std::array<uint8_t, 32> rBytes;
  std::array<uint8_t, 32> sBytes;
  for (int i = 0; i < 32; ++i) {
    rBytes[i] = static_cast<uint8_t>((hexNibble(sigHex[2*i]) << 4) | hexNibble(sigHex[2*i + 1]));
    sBytes[i] = static_cast<uint8_t>((hexNibble(sigHex[64 + 2*i]) << 4) | hexNibble(sigHex[64 + 2*i + 1]));
  }
  uint8_t v = static_cast<uint8_t>((hexNibble(sigHex[128]) << 4) | hexNibble(sigHex[129]));

  CryptoNote::SwapDaemon::Crypto::RecoverableSignature sig;
  sig.r = rBytes;
  sig.s = sBytes;
  sig.recid = (v >= 27) ? static_cast<uint8_t>(v - 27) : v;

  // Compute EIP-191 personal_sign digest: keccak256("\x19Ethereum Signed Message:\n" + len + message)
  std::string prefix = "\x19" "Ethereum Signed Message:\n" + std::to_string(message.size());
  std::vector<uint8_t> eip191(prefix.size() + message.size());
  std::memcpy(eip191.data(), prefix.data(), prefix.size());
  std::memcpy(eip191.data() + prefix.size(), message.data(), message.size());

  std::array<uint8_t, 32> msgHash;
  keccak(eip191.data(), static_cast<int>(eip191.size()), msgHash.data(), 32);

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  std::vector<uint8_t> pubkey;
  try {
    pubkey = signer.recoverPublicKey(msgHash, sig);
  } catch (const std::runtime_error&) {
    return ChainClientResult::fail(m_chainName + " reserve proof: ecrecover failed");
  }

  // Derive ETH address: last 20 bytes of keccak256(pubkey[1..64])
  uint8_t addrHash[32];
  keccak(pubkey.data() + 1, 64, addrHash, 32);

  std::ostringstream derived;
  derived << "0x";
  for (int i = 12; i < 32; ++i) {
    derived << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(addrHash[i]);
  }

  // Case-insensitive compare
  std::string derivedAddr = derived.str();
  if (address.size() != derivedAddr.size())
    return ChainClientResult::fail(m_chainName + " reserve proof: invalid signature (address mismatch)");
  for (size_t i = 0; i < address.size(); ++i) {
    if (std::tolower(address[i]) != std::tolower(derivedAddr[i]))
      return ChainClientResult::fail(m_chainName + " reserve proof: invalid signature (address mismatch)");
  }

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail(m_chainName + " reserve proof: message not bound to this offer");

  uint64_t balanceWei = 0;
  if (!m_rpc->getBalance(address, balanceWei))
    return ChainClientResult::fail(m_chainName + " reserve proof: balance check RPC failed");
  if (balanceWei < minAmount)
    return ChainClientResult::fail(m_chainName + " reserve proof: insufficient balance (" +
                                   std::to_string(balanceWei) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool EthChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc->getBlockNumber(height);
}

std::string EthChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  // ctrLockTxId is the HashedTimelock contractId; after claim, preimage is stored on-chain.
  return m_rpc->getClaimedPreimage(params.ctrLockTxId);
}

} // namespace XfgSwap
