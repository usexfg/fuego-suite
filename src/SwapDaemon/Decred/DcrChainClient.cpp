#include "DcrChainClient.h"
#include "DcrHtlcScript.h"
#include "../SwapHashLock.h"
#include "Crypto/Secp256k1Signer.h"
#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include <cstring>
#include <algorithm>

namespace XfgSwap {

DcrChainClient::DcrChainClient(std::unique_ptr<DcrRpcClient> rpc,
                                 const std::string& wif)
  : m_rpc(std::move(rpc))
  , m_wif(wif) {}

DcrChainClient::DcrChainClient(std::shared_ptr<ISpvClient> spvClient,
                                 std::unique_ptr<DcrRpcClient> rpc,
                                 const std::string& wif)
  : m_rpc(std::move(rpc))
  , m_wif(wif)
  , m_spvClient(std::move(spvClient)) {}

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
  if (!m_rpc->listUnspent(htlcAddress, utxos) || utxos.empty()) {
    m_rpc->importAddress(htlcAddress, "htlc", false);
    if (!m_rpc->listUnspent(htlcAddress, utxos) || utxos.empty()) {
      return ChainClientResult::fail("DCR lock: no UTXOs available for " + htlcAddress);
    }
  }

  try {
    const auto& utxo = utxos[0];
    std::string inputsJson = "[{\"txid\":\"" + utxo.first + "\",\"vout\":0}]";
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
  if (m_spvClient) {
    return verifyLockSpv(params);
  }

  if (!m_rpc)
    return ChainClientResult::fail("DCR verifyLock: no RPC or SPV client available");

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

ChainClientResult DcrChainClient::verifyLockSpv(const SwapParams& params) {
  // Fetch the raw locking tx
  std::vector<uint8_t> rawTx;
  if (!m_spvClient->getRawTx(params.ctrLockTxId, rawTx)) {
    return ChainClientResult::fail("DCR verifyLock SPV: getRawTx failed for " + params.ctrLockTxId);
  }

  // DCR raw tx format:
  //   version (4 LE) | vin_count (varint) | inputs... | vout_count (varint) | outputs... | locktime (4 LE) | expiry (4 LE)
  // Each input: prev_txid (32 LE) | prev_vout (4 LE) | tree (1) | scriptSig (varint+data) | sequence (4)
  // Each output: value (8 LE) | scriptPubKey (varint+data)
  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes)
  if (p + 4 > end)
    return ChainClientResult::fail("DCR verifyLock SPV: raw tx too short");
  p += 4;

  // Read vin count (compact size)
  auto readCompactSize = [&](const uint8_t*& ptr, const uint8_t* limit, uint64_t& out) -> bool {
    if (ptr >= limit) return false;
    uint8_t first = *ptr++;
    if (first < 0xFD) {
      out = first;
    } else if (first == 0xFD) {
      if (ptr + 2 > limit) return false;
      out = static_cast<uint64_t>(ptr[0]) | (static_cast<uint64_t>(ptr[1]) << 8);
      ptr += 2;
    } else if (first == 0xFE) {
      if (ptr + 4 > limit) return false;
      out = static_cast<uint64_t>(ptr[0]) | (static_cast<uint64_t>(ptr[1]) << 8) |
            (static_cast<uint64_t>(ptr[2]) << 16) | (static_cast<uint64_t>(ptr[3]) << 24);
      ptr += 4;
    } else {
      if (ptr + 8 > limit) return false;
      out = 0;
      for (int i = 0; i < 8; ++i) {
        out |= static_cast<uint64_t>(ptr[i]) << (i * 8);
      }
      ptr += 8;
    }
    return true;
  };

  uint64_t vinCount = 0;
  if (!readCompactSize(p, end, vinCount))
    return ChainClientResult::fail("DCR verifyLock SPV: truncated vin count");

  // Skip inputs: txid(32) + vout(4) + tree(1) + scriptSig(varint+data) + sequence(4)
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 37 > end) return ChainClientResult::fail("DCR verifyLock SPV: truncated tx input");
    p += 37;  // txid + vout + tree
    uint64_t sigLen = 0;
    if (!readCompactSize(p, end, sigLen))
      return ChainClientResult::fail("DCR verifyLock SPV: truncated scriptSig length");
    if (p + sigLen > end)
      return ChainClientResult::fail("DCR verifyLock SPV: truncated scriptSig");
    p += sigLen;
    if (p + 4 > end) return ChainClientResult::fail("DCR verifyLock SPV: truncated sequence");
    p += 4;  // sequence
  }

  // Read vout count
  uint64_t voutCount = 0;
  if (!readCompactSize(p, end, voutCount))
    return ChainClientResult::fail("DCR verifyLock SPV: truncated vout count");

  // Extract expected P2SH script hash from swap params for verification
  std::vector<uint8_t> expectedScriptHash;
  bool haveExpectedHash = false;
  if (!params.chainState.empty()) {
    auto redeemScript = DcrHtlcScript::hexToBytes(params.chainState);
    if (!redeemScript.empty()) {
      expectedScriptHash = DcrHtlcScript::hash160(redeemScript);
      haveExpectedHash = true;
    }
  }

  bool foundP2sh = false;
  std::vector<uint8_t> onChainScriptHash;
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return ChainClientResult::fail("DCR verifyLock SPV: truncated output value");
    uint64_t value = 0;
    for (int j = 0; j < 8; ++j) {
      value |= static_cast<uint64_t>(p[j]) << (j * 8);
    }
    p += 8;

    // Read scriptPubKey
    uint64_t spkLen = 0;
    if (!readCompactSize(p, end, spkLen))
      return ChainClientResult::fail("DCR verifyLock SPV: truncated scriptPubKey length");
    if (p + spkLen > end)
      return ChainClientResult::fail("DCR verifyLock SPV: truncated scriptPubKey");

    // Check if this is a P2SH output: OP_HASH160 <20 bytes> OP_EQUAL (23 bytes)
    if (spkLen == 23 && p[0] == 0xA9 && p[1] == 0x14 && p[22] == 0x87) {
      if (value >= params.ctrAmount) {
        foundP2sh = true;
        onChainScriptHash.assign(p + 2, p + 22);
      }
    }

    p += spkLen;
  }

  if (!foundP2sh) {
    return ChainClientResult::fail("DCR verifyLock SPV: no P2SH output with expected amount " +
                                   std::to_string(params.ctrAmount));
  }

  // Verify that the P2SH script hash matches the expected HTLC contract
  if (haveExpectedHash && !expectedScriptHash.empty()) {
    if (onChainScriptHash != expectedScriptHash) {
      return ChainClientResult::fail(
          "DCR verifyLock SPV: P2SH script hash does not match expected HTLC contract");
    }
  }

  // Verify inclusion via SPV
  SpvTxInclusion inclusion;
  if (!m_spvClient->verifyTxInclusion(params.ctrLockTxId, inclusion)) {
    return ChainClientResult::fail("DCR verifyLock SPV: verifyTxInclusion failed");
  }

  uint64_t tipHeight = 0;
  m_spvClient->getTipHeight(tipHeight);

  ChainClientResult result = ChainClientResult::ok(params.ctrLockTxId);
  result.confirmed = inclusion.included;
  result.spvVerified = inclusion.merkleVerified;
  result.blockHeight = inclusion.blockHeight;
  result.confirmations = inclusion.depth;
  return result;
}

// ---- Claim: reveal preimage -------------------------------------------------

ChainClientResult DcrChainClient::claim(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DCR claim: RPC client not available");
  if (m_wif.empty())
    return ChainClientResult::fail("DCR claim: no WIF key configured");

  std::string claimTxId;
  bool ok = m_rpc->claim(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      Common::podToHex(params.adaptorSecret),
      params.ctrAddress,
      claimTxId);

  if (!ok) return ChainClientResult::fail("DCR claim failed");
  return ChainClientResult::ok(claimTxId);
}

// ---- Refund: timelocked spending -------------------------------------------

ChainClientResult DcrChainClient::refund(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DCR refund: RPC client not available");
  if (m_wif.empty())
    return ChainClientResult::fail("DCR refund: no WIF key configured");

  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAddress,
      refundTxId);

  if (!ok) return ChainClientResult::fail("DCR refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

ChainClientResult DcrChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                      uint64_t minAmount,
                                                      const std::string& proof) {
  return ChainClientResult::fail("DCR verifyReserveProof: not supported");
}

ChainClientResult DcrChainClient::getTransactionDetails(const std::string& txId,
                                                          ChainClientResult& result) {
  if (m_spvClient) {
    uint64_t tipHeight = 0;
    if (!m_spvClient->getTipHeight(tipHeight)) {
      result = ChainClientResult::fail("DCR SPV: cannot get tip height");
      return result;
    }

    SpvTxInclusion inclusion;
    if (!m_spvClient->verifyTxInclusion(txId, inclusion)) {
      result = ChainClientResult::fail("DCR SPV: tx not found or not yet included in a block");
      result.confirmed = false;
      result.confirmations = 0;
      return result;
    }

    result.success = true;
    result.confirmed = true;
    result.spvVerified = true;
    result.blockHeight = inclusion.blockHeight;
    result.confirmations = (tipHeight >= inclusion.blockHeight)
        ? (tipHeight - inclusion.blockHeight + 1) : 1;
    return result;
  }

  if (!m_rpc) {
    result = ChainClientResult::fail("DCR: no RPC or SPV client available");
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
  if (m_spvClient) {
    return m_spvClient->getTipHeight(height);
  }
  if (m_rpc) return m_rpc->getBlockCount(height);
  return false;
}

std::string DcrChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  auto redeemScript = DcrHtlcScript::hexToBytes(htlcRedeemScriptHex);
  auto p2shScriptPubKey = DcrHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  if (m_spvClient) {
    return extractSecretSpv(spendingTxid, p2shScriptPubKey);
  }

  if (!m_rpc) return {};

  std::vector<uint8_t> rawTx;
  if (!m_rpc->getRawTransactionBytes(spendingTxid, rawTx)) return {};

  auto preimage = DcrHtlcScript::parseClaimPreimage(rawTx, p2shScriptPubKey);
  if (preimage.empty()) return {};

  return DcrHtlcScript::bytesToHex(preimage);
}

std::string DcrChainClient::extractSecretSpv(const std::string& spendingTxid,
                                               const std::vector<uint8_t>& htlcP2shScriptPubKey) {
  std::vector<uint8_t> rawSpendingTx;
  if (!m_spvClient->getRawTx(spendingTxid, rawSpendingTx)) {
    return {};
  }

  std::vector<uint8_t> preimage = DcrHtlcScript::parseClaimPreimage(rawSpendingTx, htlcP2shScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return DcrHtlcScript::bytesToHex(preimage);
}

} // namespace XfgSwap
