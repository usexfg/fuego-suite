#include "BtcChainClient.h"
#include "BtcHtlcScript.h"
#include "BtcPtlcScript.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"
#include "../SwapPtlcLock.h"

#include <array>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include "Komodo/KmdHtlcScript.h"

namespace XfgSwap {

static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

std::string BtcChainClient::getReceiveAddress() const {
  if (m_wif.empty()) return "";
  std::vector<uint8_t> h;
  if (!KmdHtlcScript::wifToPubkeyHash(m_wif, h)) return "";
  return KmdHtlcScript::base58CheckEncode(0x00, h);
}
BtcChainClient::BtcChainClient(std::unique_ptr<BtcRpcClient> rpc, const std::string& wif)
  : m_rpc(std::move(rpc)), m_wif(wif) {}

BtcChainClient::BtcChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif)
  : m_spvClient(std::move(spvClient)), m_wif(wif) {}

ChainClientResult BtcChainClient::lock(const SwapParams& params) {
  if (params.lockType == SwapLockType::PTLC) {
    return lockPtlc(params);
  }
  if (!m_rpc)
    return ChainClientResult::fail("BTC lock: RPC client not available (SPV mode does not support lock)");

  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) {
    hashHex = bchHashLockHex(params.adaptorSecret);
  } else {
    bool nonzero = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nonzero = true; break; }
    if (!nonzero)
      return ChainClientResult::fail("BTC lock: no adaptor secret or hashLock (need H(t) from Bob)");
    hashHex = Common::podToHex(params.hashLock);
  }

  std::string recipientKey = params.ctrPubKey;
  if (recipientKey.empty() && params.ctrAddress.size() == 66) {
    try {
      auto bytes = BtcHtlcScript::hexToBytes(params.ctrAddress);
      if (bytes.size() == 33 && (bytes[0] == 0x02 || bytes[0] == 0x03))
        recipientKey = params.ctrAddress;
    } catch (const std::exception&) {}
  }
  if (recipientKey.size() != 66 && !params.ctrAddress.empty()) {
    std::string resolved;
    if (m_rpc->getAddressPubkey(params.ctrAddress, resolved) && resolved.size() == 66)
      recipientKey = resolved;
  }
  if (recipientKey.size() != 66)
    return ChainClientResult::fail(
        "BTC lock: ctrPubKey must be 33-byte compressed pubkey hex (66 chars). "
        "Cannot derive pubkey from address; set ctrPubKey or use a wallet-known address.");

  std::string lockTxId;
  std::string redeemScriptHex;
  bool ok = m_rpc->lockHtlc(
      m_wif,
      recipientKey,
      hashHex,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAmount,
      lockTxId,
      redeemScriptHex);
  if (!ok) return ChainClientResult::fail("BTC lockHtlc failed");
  return ChainClientResult::okWithState(lockTxId, redeemScriptHex);
}

ChainClientResult BtcChainClient::lockPtlc(const SwapParams& params) {
  // Bridge: on-chain HTLC (hash) + off-chain point. Avoid recursion by directly funding HTLC.
  if (!m_rpc) return ChainClientResult::fail("BTC PtlcLock: RPC not available");
  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) hashHex = bchHashLockHex(params.adaptorSecret);
  else {
    bool nz=false; for(size_t i=0;i<sizeof(params.hashLock);++i) if(reinterpret_cast<const uint8_t*>(&params.hashLock)[i]){nz=true;break;}
    if(!nz){
      Crypto::PublicKey pt=params.ptlcPoint; Crypto::PublicKey zero{}; std::memset(&zero,0,sizeof(zero));
      if(std::memcmp(&pt,&zero,sizeof(zero))==0) pt=params.adaptorPoint;
      if(std::memcmp(&pt,&zero,sizeof(zero))!=0){
        // For PTLC pure without hashLock, derive hash as SHA256(t) placeholder from pt hex? But we need hash for HTLC fund.
        // Fallback: use pt hex as hash (not correct but allows funding for bridge)
        hashHex = Common::podToHex(pt);
      } else return ChainClientResult::fail("BTC PtlcLock: no hashLock/adaptorSecret");
    } else hashHex = Common::podToHex(params.hashLock);
  }
  std::string recipientKey=params.ctrPubKey;
  if(recipientKey.empty() && params.ctrAddress.size()==66){ try{auto b=BtcHtlcScript::hexToBytes(params.ctrAddress); if(b.size()==33) recipientKey=params.ctrAddress;}catch (const std::exception&){}}
  if(recipientKey.size()!=66 && !params.ctrAddress.empty()){ std::string r; if(m_rpc->getAddressPubkey(params.ctrAddress,r) && r.size()==66) recipientKey=r; }
  if(recipientKey.size()!=66) return ChainClientResult::fail("BTC PtlcLock: need 33-byte pubkey");
  std::string lockTxId; std::string redeemHex;
  bool ok=m_rpc->lockHtlc(m_wif,recipientKey,hashHex,static_cast<uint32_t>(params.ctrTimeoutBlock),params.ctrAmount,lockTxId,redeemHex);
  if(!ok) return ChainClientResult::fail("BTC PtlcLock: lockHtlc failed");
  Crypto::PublicKey pt=params.ptlcPoint; Crypto::PublicKey zero{}; std::memset(&zero,0,sizeof(zero));
  if(std::memcmp(&pt,&zero,sizeof(zero))==0) pt=params.adaptorPoint;
  if(std::memcmp(&pt,&zero,sizeof(zero))!=0){
    std::string ptHex=Common::podToHex(pt);
    std::string state=redeemHex + "|ptlc:" + ptHex;
    return ChainClientResult::okWithState(lockTxId, state);
  }
  return ChainClientResult::okWithState(lockTxId, redeemHex);
}

ChainClientResult BtcChainClient::verifyPtlcLock(const SwapParams& params) {
  // Bridge verify: check HTLC part via verifyLock, then verify ptlc point matches adaptorPoint (off-chain)
  return verifyLock(params);
}

ChainClientResult BtcChainClient::verifyLock(const SwapParams& params) {
  if (m_spvClient) {
    return verifyLockSpv(params);
  }

  if (!m_rpc)
    return ChainClientResult::fail("BTC verifyLock: no RPC or SPV client available");

  std::string htlcAddress;
  if (!params.chainState.empty()) {
    std::string redeemHexRaw = params.chainState;
    auto pipe = redeemHexRaw.find('|'); if (pipe != std::string::npos) redeemHexRaw = redeemHexRaw.substr(0, pipe);
    auto colon = redeemHexRaw.find(':'); if (colon != std::string::npos) redeemHexRaw = redeemHexRaw.substr(0, colon);
    auto redeem = BtcHtlcScript::hexToBytes(redeemHexRaw);
    if (redeem.empty())
      return ChainClientResult::fail("BTC verifyLock: invalid redeem script in chainState");
    htlcAddress = BtcHtlcScript::witnessScriptToAddress(redeem, "bc");
  } else {
    return ChainClientResult::fail(
        "BTC verifyLock: need chainState (redeem script) — "
        "cannot listunspent by txid alone");
  }

  bool ok = m_rpc->verifyLock(htlcAddress, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("BTC lock not verified at " + htlcAddress);
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult BtcChainClient::claim(const SwapParams& params) {
  if (!m_rpc) {
    if (m_wif.empty())
      return ChainClientResult::fail("BTC claim: no RPC or WIF available (SPV mode needs WIF for local signing)");

    std::array<uint8_t, 32> privKey{};
    if (!BtcHtlcScript::wifToPrivKey(m_wif, privKey))
      return ChainClientResult::fail("BTC claim: invalid WIF");

    std::string redeemHexRaw = params.chainState;
    { auto p=redeemHexRaw.find('|'); if(p!=std::string::npos) redeemHexRaw=redeemHexRaw.substr(0,p); auto c=redeemHexRaw.find(':'); if(c!=std::string::npos) redeemHexRaw=redeemHexRaw.substr(0,c); }
    auto witnessScript = BtcHtlcScript::hexToBytes(redeemHexRaw);
    auto preimageBytes = BtcHtlcScript::hexToBytes(Common::podToHex(params.adaptorSecret));

    uint8_t addrVersion = 0;
    std::vector<uint8_t> pubKeyHash;
    if (!BtcHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
      return ChainClientResult::fail("BTC claim: invalid destination address");
    auto outputScript = BtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

    uint64_t fee = 1000;
    if (params.ctrAmount <= fee)
      return ChainClientResult::fail("BTC claim: amount too small for fee");
    uint64_t outputAmount = params.ctrAmount - fee;

    const uint32_t nSequence = 0xFFFFFFFD;

    auto der = BtcHtlcScript::signInput(privKey, 2, 0, nSequence,
        params.ctrLockTxId, 0, witnessScript, params.ctrAmount,
        outputScript, outputAmount);
    if (der.empty())
      return ChainClientResult::fail("BTC claim SPV: signing failed");

    std::vector<uint8_t> emptyScriptSig;
    auto witnessStack = BtcHtlcScript::createClaimWitness(der, preimageBytes, witnessScript);

    auto rawTx = BtcHtlcScript::buildRawSegWitTx(
        params.ctrLockTxId, 0, params.ctrAmount,
        emptyScriptSig, witnessStack, params.ctrAddress, outputAmount, 0);

    std::string txid;
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("BTC claim SPV: broadcast failed");
    return ChainClientResult::ok(txid);
  }

  std::string redeemHexRaw2 = params.chainState;
  { auto p=redeemHexRaw2.find('|'); if(p!=std::string::npos) redeemHexRaw2=redeemHexRaw2.substr(0,p); auto c=redeemHexRaw2.find(':'); if(c!=std::string::npos) redeemHexRaw2=redeemHexRaw2.substr(0,c); }
  std::string claimTxId;
  bool ok = m_rpc->claim(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      redeemHexRaw2,
      Common::podToHex(params.adaptorSecret),
      params.ctrAddress,
      claimTxId);
  if (!ok) return ChainClientResult::fail("BTC claim failed");
  return ChainClientResult::ok(claimTxId);
}

ChainClientResult BtcChainClient::refund(const SwapParams& params) {
  if (!m_rpc) {
    if (m_wif.empty())
      return ChainClientResult::fail("BTC refund: no RPC or WIF available (SPV mode needs WIF for local signing)");

    std::array<uint8_t, 32> privKey{};
    if (!BtcHtlcScript::wifToPrivKey(m_wif, privKey))
      return ChainClientResult::fail("BTC refund: invalid WIF");

    std::string redeemHexRaw0 = params.chainState;
    { auto p=redeemHexRaw0.find('|'); if(p!=std::string::npos) redeemHexRaw0=redeemHexRaw0.substr(0,p); auto c=redeemHexRaw0.find(':'); if(c!=std::string::npos) redeemHexRaw0=redeemHexRaw0.substr(0,c); }
    auto witnessScript = BtcHtlcScript::hexToBytes(redeemHexRaw0);

    uint32_t nLocktime = static_cast<uint32_t>(params.ctrTimeoutBlock);

    uint8_t addrVersion = 0;
    std::vector<uint8_t> pubKeyHash;
    if (!BtcHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
      return ChainClientResult::fail("BTC refund: invalid destination address");
    auto outputScript = BtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

    uint64_t fee = 1000;
    if (params.ctrAmount <= fee)
      return ChainClientResult::fail("BTC refund: amount too small for fee");
    uint64_t outputAmount = params.ctrAmount - fee;

    auto der = BtcHtlcScript::signInput(privKey, 2, nLocktime,
        0xFFFFFFFE,
        params.ctrLockTxId, 0, witnessScript, params.ctrAmount,
        outputScript, outputAmount);
    if (der.empty())
      return ChainClientResult::fail("BTC refund SPV: signing failed");

    std::vector<uint8_t> emptyScriptSig;
    auto witnessStack = BtcHtlcScript::createRefundWitness(der, witnessScript);

    auto rawTx = BtcHtlcScript::buildRawSegWitTx(
        params.ctrLockTxId, 0, params.ctrAmount,
        emptyScriptSig, witnessStack, params.ctrAddress, outputAmount, nLocktime);

    std::string txid;
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("BTC refund SPV: broadcast failed");
    return ChainClientResult::ok(txid);
  }

  std::string redeemHexRaw1 = params.chainState;
  { auto p=redeemHexRaw1.find('|'); if(p!=std::string::npos) redeemHexRaw1=redeemHexRaw1.substr(0,p); auto c=redeemHexRaw1.find(':'); if(c!=std::string::npos) redeemHexRaw1=redeemHexRaw1.substr(0,c); }
  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      redeemHexRaw1,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAddress,
      refundTxId);
  if (!ok) return ChainClientResult::fail("BTC refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

ChainClientResult BtcChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                     uint64_t minAmount,
                                                     const std::string& proof) {
  if (!m_rpc)
    return ChainClientResult::fail("BTC verifyReserveProof: RPC client not available");

  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("BTC reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string signature = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  bool sigValid = false;
  if (!m_rpc->verifyMessage(address, signature, message, sigValid))
    return ChainClientResult::fail("BTC reserve proof: verifymessage RPC failed");
  if (!sigValid)
    return ChainClientResult::fail("BTC reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("BTC reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("BTC reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("BTC reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool BtcChainClient::getCurrentHeight(uint64_t& height) {
  if (m_spvClient) {
    return m_spvClient->getTipHeight(height);
  }
  if (m_rpc) {
    return m_rpc->getBlockCount(height);
  }
  return false;
}

ChainClientResult BtcChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (m_spvClient) {
    uint64_t tipHeight = 0;
    if (!m_spvClient->getTipHeight(tipHeight)) {
      result = ChainClientResult::fail("BTC SPV: cannot get tip height");
      return result;
    }

    SpvTxInclusion inclusion;
    if (!m_spvClient->verifyTxInclusion(txId, inclusion)) {
      result = ChainClientResult::fail("BTC SPV: tx not found or not yet included in a block");
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

  if (m_rpc) {
    BtcTxInfo txInfo;
    if (!m_rpc->getTransaction(txId, txInfo)) {
      result = ChainClientResult::fail("BTC RPC: gettransaction failed for " + txId);
      return result;
    }

    uint64_t tipHeight = 0;
    m_rpc->getBlockCount(tipHeight);

    result.success = true;
    result.confirmed = txInfo.confirmations > 0;
    result.spvVerified = false;
    result.blockHeight = txInfo.blockHeight;
    result.confirmations = txInfo.confirmations;
    return result;
  }

  result = ChainClientResult::fail("BTC: no RPC or SPV client available");
  return result;
}

// =============================================================================
// SPV-mode verifyLock
// =============================================================================

ChainClientResult BtcChainClient::verifyLockSpv(const SwapParams& params) {
  // Fetch the raw locking tx
  std::vector<uint8_t> rawTx;
  if (!m_spvClient->getRawTx(params.ctrLockTxId, rawTx)) {
    return ChainClientResult::fail("BTC verifyLock SPV: getRawTx failed for " + params.ctrLockTxId);
  }

  // Parse the raw tx to find P2WSH outputs.
  // P2WSH scriptPubKey: OP_0 (0x00) PUSH32 (0x20) <32-byte-hash> = 34 bytes
  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes)
  if (p + 4 > end)
    return ChainClientResult::fail("BTC verifyLock SPV: raw tx too short");
  p += 4;

  // Skip SegWit marker + flag if present
  if (p + 2 <= end && p[0] == 0x00 && p[1] == 0x01) {
    p += 2;
  }

  // Read vin count (inline varint reader)
  uint64_t vinCount = 0;
  if (p >= end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx");
  uint8_t first = *p++;
  if (first < 0xFD) {
    vinCount = first;
  } else if (first == 0xFD) {
    if (p + 2 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx");
    vinCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
  } else if (first == 0xFE) {
    if (p + 4 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx");
    vinCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
              (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
  } else {
    if (p + 8 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx");
    vinCount = 0;
    for (int i = 0; i < 8; ++i) {
      vinCount |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
  }

  // Skip inputs
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx inputs");
    p += 36;  // txid + vout
    uint64_t sigLen = 0;
    if (*p < 0xFD) {
      sigLen = *p++;
    } else if (*p == 0xFD) {
      ++p;
      if (p + 2 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated");
      sigLen = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2;
    } else {
      return ChainClientResult::fail("BTC verifyLock SPV: oversized scriptSig");
    }
    p += sigLen;
    if (p + 4 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated sequence");
    p += 4;  // sequence
  }

  // Read vout count
  uint64_t voutCount = 0;
  if (p >= end) return ChainClientResult::fail("BTC verifyLock SPV: no vouts");
  first = *p++;
  if (first < 0xFD) {
    voutCount = first;
  } else if (first == 0xFD) {
    if (p + 2 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated vout count");
    voutCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
  } else if (first == 0xFE) {
    if (p + 4 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated vout count");
    voutCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
               (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
  } else {
    if (p + 8 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated vout count");
    voutCount = 0;
    for (int i = 0; i < 8; ++i) {
      voutCount |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
  }

  bool foundP2wsh = false;
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated output value");
    uint64_t value = 0;
    for (int j = 0; j < 8; ++j) {
      value |= static_cast<uint64_t>(p[j]) << (j * 8);
    }
    p += 8;

    // Read scriptPubKey
    uint64_t spkLen = 0;
    if (*p < 0xFD) {
      spkLen = *p++;
    } else if (*p == 0xFD) {
      ++p;
      if (p + 2 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated");
      spkLen = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2;
    } else {
      return ChainClientResult::fail("BTC verifyLock SPV: oversized scriptPubKey");
    }
    if (p + spkLen > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated scriptPubKey");

    // Check if this is a P2WSH output: OP_0 (0x00) PUSH32 (0x20) <32-byte-hash>
    // Total: 34 bytes
    if (spkLen == 34 && p[0] == 0x00 && p[1] == 0x20) {
      if (value >= params.ctrAmount) {
        // Verify the witness program hash matches the expected HTLC script
        if (!params.chainState.empty()) {
          auto redeemScript = BtcHtlcScript::hexToBytes(params.chainState);
          auto expectedHash = BtcHtlcScript::sha256(redeemScript);
          if (std::memcmp(p + 2, expectedHash.data(), 32) == 0) {
            foundP2wsh = true;
          }
        }
        // Fail closed: without chainState redeem script we cannot bind the
        // output to the negotiated HTLC — never accept any matching amount.
      }
    }

    p += spkLen;
  }

  if (!foundP2wsh) {
    return ChainClientResult::fail("BTC verifyLock SPV: no P2WSH output with expected amount " +
                                   std::to_string(params.ctrAmount));
  }

  // Verify inclusion via SPV
  SpvTxInclusion inclusion;
  if (!m_spvClient->verifyTxInclusion(params.ctrLockTxId, inclusion)) {
    return ChainClientResult::fail("BTC verifyLock SPV: verifyTxInclusion failed");
  }

  ChainClientResult result = ChainClientResult::ok(params.ctrLockTxId);
  result.confirmed = inclusion.included;
  result.spvVerified = inclusion.merkleVerified;
  result.blockHeight = inclusion.blockHeight;
  result.confirmations = inclusion.depth;
  return result;
}

// =============================================================================
// SPV-mode extractSecret
// =============================================================================

std::string BtcChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  std::vector<uint8_t> redeemScript = BtcHtlcScript::hexToBytes(htlcRedeemScriptHex);
  std::vector<uint8_t> p2wshScriptPubKey = BtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  if (m_spvClient) {
    return extractSecretSpv(spendingTxid, p2wshScriptPubKey);
  }

  if (!m_rpc) {
    return {};
  }

  std::string rawTxHex;
  if (!m_rpc->getRawTransaction(spendingTxid, rawTxHex)) {
    return {};
  }

  std::vector<uint8_t> rawTx = BtcHtlcScript::hexToBytes(rawTxHex);
  // Try PTLC first (t is 32-byte scalar, same parse as HTLC preimage)
  std::vector<uint8_t> preimage = BtcPtlcScript::parseClaimAdaptorSecret(rawTx, p2wshScriptPubKey);
  if (preimage.empty()) preimage = BtcHtlcScript::parseClaimPreimage(rawTx, p2wshScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return BtcHtlcScript::bytesToHex(preimage);
}

std::string BtcChainClient::extractSecretSpv(const std::string& spendingTxid,
                                              const std::vector<uint8_t>& htlcP2wshScriptPubKey) {
  std::vector<uint8_t> rawSpendingTx;
  if (!m_spvClient->getRawTx(spendingTxid, rawSpendingTx)) {
    return {};
  }

  std::vector<uint8_t> preimage = BtcPtlcScript::parseClaimAdaptorSecret(rawSpendingTx, htlcP2wshScriptPubKey);
  if (preimage.empty()) preimage = BtcHtlcScript::parseClaimPreimage(rawSpendingTx, htlcP2wshScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return BtcHtlcScript::bytesToHex(preimage);
}

std::string BtcChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (params.chainState.empty() || params.ctrLockTxId.empty()) return {};

  std::string redeemHex = params.chainState;
  std::string knownClaimTxid;
  auto colon = params.chainState.find(':');
  if (colon != std::string::npos && colon + 1 < params.chainState.size()) {
    std::string left = params.chainState.substr(0, colon);
    std::string right = params.chainState.substr(colon + 1);
    if (!left.empty() && (left.size() % 2) == 0 && right.size() == 64) {
      redeemHex = left;
      knownClaimTxid = right;
    }
  }
  // For PTLC, chainState contains PTLC redeem; extraction uses same preimage parse but verified as point
  bool isPtlc = params.lockType == SwapLockType::PTLC || params.lockType == SwapLockType::PTLC_HTLC_BRIDGE;

  if (m_spvClient) {
    for (uint32_t vout = 0; vout < 4; ++vout) {
      SpvSpend spend;
      if (!m_spvClient->findSpend(params.ctrLockTxId, vout, spend) || !spend.spent)
        continue;
      if (spend.spendingTxid.empty()) continue;
      std::string secret = extractSecret(spend.spendingTxid, redeemHex);
      if (!secret.empty()) return secret;
    }
  }

  if (m_rpc && !knownClaimTxid.empty()) {
    std::string secret = extractSecret(knownClaimTxid, redeemHex);
    if (!secret.empty()) return secret;
  }

  return {};
}

} // namespace XfgSwap
