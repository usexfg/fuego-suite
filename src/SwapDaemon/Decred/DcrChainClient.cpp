#include "DcrChainClient.h"
#include "DcrHtlcScript.h"
#include "../SwapHashLock.h"
#include "Crypto/Secp256k1Signer.h"
#include "Common/JsonValue.h"
#include <cstring>
#include <algorithm>

namespace XfgSwap {

DcrChainClient::DcrChainClient(std::unique_ptr<DcrRpcClient> rpc,
                                 const std::string& wif)
  : m_rpc(std::move(rpc))
  , m_wif(wif) {}

// ---- Lock: create P2SH HTLC ------------------------------------------------

ChainClientResult DcrChainClient::lock(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DCR lock: RPC client not available");
  if (m_wif.empty())
    return ChainClientResult::fail("DCR lock: no WIF key configured");

  // Derive sender pubkey from WIF
  std::array<uint8_t, 32> privKey{};
  // WIF decode: strip leading version byte (0x80) and checksum
  auto wifBytes = DcrHtlcScript::hexToBytes(m_wif);
  if (wifBytes.size() == 33) {
    std::memcpy(privKey.data(), wifBytes.data() + 1, 32);
  } else if (wifBytes.size() == 34 && wifBytes.back() == 0x01) {
    std::memcpy(privKey.data(), wifBytes.data() + 1, 32);
  } else {
    return ChainClientResult::fail("DCR lock: invalid WIF key");
  }

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto senderPubKey = signer.derivePublicKeyCompressed(privKey);

  // Parse recipient pubkey from ctrAddress if it's a 66-char hex pubkey
  std::vector<uint8_t> recipientPubKey(33, 0);
  if (params.ctrAddress.size() == 66) {
    auto bytes = DcrHtlcScript::hexToBytes(params.ctrAddress);
    if (bytes.size() == 33 && (bytes[0] == 0x02 || bytes[0] == 0x03)) {
      recipientPubKey = bytes;
    }
  }

  // Build hashlock = SHA256(adaptor secret)
  auto hashLock = bchHashLockHex(params.adaptorSecret);
  auto hashLockBytes = DcrHtlcScript::hexToBytes(hashLock);
  if (hashLockBytes.size() != 32)
    return ChainClientResult::fail("DCR lock: failed to derive hashlock");

  // Build redeem script
  auto redeemScript = DcrHtlcScript::createRedeemScript(
      hashLockBytes, recipientPubKey, senderPubKey,
      static_cast<uint32_t>(params.ctrTimeoutBlock));
  std::string redeemScriptHex = DcrHtlcScript::bytesToHex(redeemScript);

  // Compute P2SH address
  auto scriptHash = DcrHtlcScript::hash160(redeemScript);
  std::string htlcAddress = DcrHtlcScript::scriptHashToAddress(scriptHash);

  // Create raw transaction: sendtoaddress equivalent
  // Build inputs JSON: listunspent to find a UTXO
  std::vector<std::pair<std::string, uint64_t>> utxos;
  if (!m_rpc->listUnspent(htlcAddress, utxos)) {
    // Import the HTLC address and retry
    m_rpc->importAddress(htlcAddress, "htlc", false);
  }

  // Use createrawtransaction + signrawtransaction + sendrawtransaction
  try {
    std::string inputsJson = "[{\"txid\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"vout\":0}]";
    std::string outputsJson = "{\"" + htlcAddress + "\":" +
        std::to_string(static_cast<double>(params.ctrAmount) / 1e8) + "}";

    std::string rawTxHex;
    if (!m_rpc->createRawTransaction(inputsJson, outputsJson, 0, rawTxHex)) {
      return ChainClientResult::fail("DCR lock: createRawTransaction failed");
    }

    std::string signedTxHex;
    if (!m_rpc->signRawTransaction(rawTxHex, signedTxHex)) {
      return ChainClientResult::fail("DCR lock: signRawTransaction failed");
    }

    std::string txid;
    if (!m_rpc->sendRawTransaction(signedTxHex, txid)) {
      return ChainClientResult::fail("DCR lock: sendRawTransaction failed");
    }

    return ChainClientResult::okWithState(txid, redeemScriptHex);
  } catch (...) {
    return ChainClientResult::fail("DCR lock: RPC exception");
  }
}

// ---- Verify Lock ------------------------------------------------------------

ChainClientResult DcrChainClient::verifyLock(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DCR verifyLock: RPC client not available");

  if (params.ctrLockTxId.empty())
    return ChainClientResult::fail("DCR verifyLock: no lock txid");

  uint64_t amount = 0;
  if (!m_rpc->getTxOut(params.ctrLockTxId, 0, amount))
    return ChainClientResult::fail("DCR verifyLock: tx output not found: " + params.ctrLockTxId);

  if (amount < params.ctrAmount)
    return ChainClientResult::fail("DCR verifyLock: amount " +
        std::to_string(amount) + " < required " + std::to_string(params.ctrAmount));

  ChainClientResult result;
  result.success = true;
  result.txId = params.ctrLockTxId;
  result.confirmed = true;
  return result;
}

// ---- Claim: reveal preimage -------------------------------------------------

ChainClientResult DcrChainClient::claim(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DCR claim: RPC client not available");

  // Claim requires building a spending tx with the preimage in the scriptSig.
  // For now, delegate to refund-style construction with preimage revealed.
  return ChainClientResult::fail("DCR claim: not yet implemented (requires raw tx construction with preimage)");
}

// ---- Refund: timelocked spending -------------------------------------------

ChainClientResult DcrChainClient::refund(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DCR refund: RPC client not available");

  // Refund requires building a spending tx after timeout.
  return ChainClientResult::fail("DCR refund: not yet implemented (requires raw tx construction with CLTV)");
}

ChainClientResult DcrChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                      uint64_t minAmount,
                                                      const std::string& proof) {
  return ChainClientResult::fail("DCR verifyReserveProof: not supported");
}

ChainClientResult DcrChainClient::getTransactionDetails(const std::string& txId,
                                                         ChainClientResult& result) {
  if (!m_rpc) {
    result = ChainClientResult::fail("DCR: RPC client not available");
    return result;
  }

  uint64_t height = 0;
  if (!m_rpc->getBlockCount(height)) {
    result = ChainClientResult::fail("DCR: cannot get block count");
    return result;
  }

  std::string rawTxHex;
  if (!m_rpc->getRawTransaction(txId, rawTxHex)) {
    result = ChainClientResult::fail("DCR: getrawtransaction failed for " + txId);
    return result;
  }

  result.success = true;
  result.confirmed = true;
  result.spvVerified = false;
  result.blockHeight = height;
  result.confirmations = 1;
  return result;
}

bool DcrChainClient::getCurrentHeight(uint64_t& height) {
  if (m_rpc) return m_rpc->getBlockCount(height);
  return false;
}

std::string DcrChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  if (!m_rpc) return {};

  std::vector<uint8_t> rawTx;
  if (!m_rpc->getRawTransactionBytes(spendingTxid, rawTx)) return {};

  auto redeemScript = DcrHtlcScript::hexToBytes(htlcRedeemScriptHex);
  auto p2shScriptPubKey = DcrHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);
  auto preimage = DcrHtlcScript::parseClaimPreimage(rawTx, p2shScriptPubKey);
  if (preimage.empty()) return {};

  return DcrHtlcScript::bytesToHex(preimage);
}

} // namespace XfgSwap
