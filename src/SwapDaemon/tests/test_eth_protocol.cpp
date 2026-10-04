// Copyright (c) 2017-2026 Fuego Developers
//
// ETH HashedTimelock protocol unit tests (offline):
//   - ethHashLockHex = keccak256(t)  (Alice-locks hashlock)
//   - computeContractId matches Solidity abi.encodePacked layout
//   - ABI selectors for lock/claim/getContract
//   - RLP empty access list is 0xc0 (EIP-1559)

#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "SwapDaemon/SwapHashLock.h"
#include "SwapDaemon/Ethereum/EthRpcClient.h"
#include "SwapDaemon/Ethereum/ContractAbi.h"
#include "SwapDaemon/Crypto/RlpEncoder.h"
#include "SwapDaemon/Crypto/Secp256k1Signer.h"
#include "SwapDaemon/SwapPeerProtocol.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"

extern "C" {
#include "crypto/keccak.h"
}

using namespace XfgSwap;

namespace XfgSwap {
struct EthRpcClientTestAccess {
  static bool funding(const AtomicAmount& balance, const AtomicAmount& value,
                      uint64_t gasLimit, uint64_t feePerGas, uint64_t estimatedGas) {
    return EthRpcClient::transactionFundingSufficient(
        balance, value, gasLimit, feePerGas, estimatedGas);
  }
  static bool fees(uint64_t tip, uint64_t baseFee, uint64_t& priority,
                   uint64_t& maxFee) {
    return EthRpcClient::calculateCappedEip1559Fees(
        tip, baseFee, priority, maxFee);
  }
  static std::string estimateGasParams(const EthRpcClient& client,
                                       const std::string& to,
                                       const std::string& data,
                                       const AtomicAmount& value) {
    return EthRpcClient::buildEstimateGasParams(
        client.m_signerAddress, to, data, value);
  }
  static std::vector<uint8_t> legacy(EthRpcClient& client, const AtomicAmount& value) {
    return client.buildLegacySignedTx(3, 20'000'000'000ULL, 100'000,
                                      std::vector<uint8_t>(20, 0x22), value, {});
  }
  static std::vector<uint8_t> eip1559(EthRpcClient& client, const AtomicAmount& value) {
    return client.buildEip1559SignedTx(3, 1'000'000'000ULL, 20'000'000'000ULL,
                                       100'000, std::vector<uint8_t>(20, 0x22), value, {});
  }
};
}

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { ++g_pass; std::cout << "  PASS: " << m << "\n"; } \
  else { ++g_fail; std::cerr << "  FAIL: " << m << "\n"; } } while (0)

static std::string toHex(const uint8_t* p, size_t n) {
  static const char* h = "0123456789abcdef";
  std::string s; s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) { s += h[p[i] >> 4]; s += h[p[i] & 0xf]; }
  return s;
}

// Decode one RLP item and return its encoded extent and payload. For a list,
// payload is the concatenated child encoding; test fields of interest are
// byte strings, while nested EIP-1559 accessList is skipped as one item.
static bool readRlpItem(const std::vector<uint8_t>& bytes, size_t& offset,
                        bool& isList, std::vector<uint8_t>& payload) {
  if (offset >= bytes.size()) return false;
  const uint8_t prefix = bytes[offset++];
  size_t length = 0;
  size_t lengthBytes = 0;
  uint8_t base = 0;
  if (prefix <= 0x7f) {
    isList = false;
    payload = {prefix};
    return true;
  } else if (prefix <= 0xb7) {
    isList = false; length = prefix - 0x80;
  } else if (prefix <= 0xbf) {
    isList = false; base = 0xb7; lengthBytes = prefix - base;
  } else if (prefix <= 0xf7) {
    isList = true; length = prefix - 0xc0;
  } else {
    isList = true; base = 0xf7; lengthBytes = prefix - base;
  }
  if (lengthBytes != 0) {
    if (offset + lengthBytes > bytes.size()) return false;
    for (size_t i = 0; i < lengthBytes; ++i) length = (length << 8) | bytes[offset++];
  }
  if (offset + length > bytes.size()) return false;
  payload.assign(bytes.begin() + offset, bytes.begin() + offset + length);
  offset += length;
  return true;
}

static bool signedTxValueEquals(const std::vector<uint8_t>& tx, bool typed,
                               size_t valueIndex, const AtomicAmount& value) {
  const size_t listOffset = typed ? 1 : 0;
  if (tx.size() <= listOffset) return false;
  std::vector<uint8_t> outer(tx.begin() + listOffset, tx.end());
  size_t offset = 0;
  bool isList = false;
  std::vector<uint8_t> fields;
  if (!readRlpItem(outer, offset, isList, fields) || !isList || offset != outer.size()) return false;
  size_t fieldOffset = 0;
  for (size_t index = 0; fieldOffset < fields.size(); ++index) {
    std::vector<uint8_t> item;
    if (!readRlpItem(fields, fieldOffset, isList, item)) return false;
    if (index == valueIndex) {
      const auto bytes = atomicAmountToBigEndian(value);
      size_t first = 0;
      while (first < bytes.size() && bytes[first] == 0) ++first;
      return !isList && item == std::vector<uint8_t>(bytes.begin() + first, bytes.end());
    }
  }
  return false;
}

int main() {
  std::cout << "=== ETH protocol unit tests ===\n";

  // ── hashlock ─────────────────────────────────────────────────────────
  {
    Crypto::PublicKey T;
    Crypto::SecretKey t;
    Crypto::generate_keys(T, t);
    std::string hl = ethHashLockHex(t);
    uint8_t md[32];
    keccak(reinterpret_cast<const uint8_t*>(&t), 32, md, 32);
    CHECK(hl == toHex(md, 32), "ethHashLockHex == keccak256(t)");
    CHECK(hl != Common::podToHex(T), "ethHashLockHex != adaptor point T");
  }

  // ── contractId packing (Solidity encodePacked) ───────────────────────
  {
    // Fixed vectors so we can recompute independently
    const std::string sender = "1111111111111111111111111111111111111111";
    const std::string recip  = "2222222222222222222222222222222222222222";
    const uint64_t value = 1000000000000000000ULL; // 1 eth wei
    const std::string hashLock =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const uint64_t timeout = 12345678ULL;

    std::string id = EthRpcClient::computeContractId(sender, recip, value, hashLock, timeout);
    CHECK(id.size() == 64, "contractId is 32-byte hex");

    // Independent pack + keccak
    std::vector<uint8_t> packed;
    auto pushHex = [&](const std::string& hx) {
      for (size_t i = 0; i + 1 < hx.size(); i += 2) {
        unsigned v = 0;
        sscanf(hx.c_str() + i, "%2x", &v);
        packed.push_back(static_cast<uint8_t>(v));
      }
    };
    pushHex(sender);
    pushHex(recip);
    std::vector<uint8_t> val(32, 0);
    for (int i = 0; i < 8; ++i) val[31 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xff);
    packed.insert(packed.end(), val.begin(), val.end());
    pushHex(hashLock);
    std::vector<uint8_t> to(32, 0);
    for (int i = 0; i < 8; ++i) to[31 - i] = static_cast<uint8_t>((timeout >> (i * 8)) & 0xff);
    packed.insert(packed.end(), to.begin(), to.end());
    CHECK(packed.size() == 20 + 20 + 32 + 32 + 32, "packed size 136 bytes");
    uint8_t digest[32];
    keccak(packed.data(), static_cast<int>(packed.size()), digest, 32);
    CHECK(id == toHex(digest, 32), "computeContractId matches independent keccak");

    // Mutating amount must change id
    std::string id2 = EthRpcClient::computeContractId(sender, recip, value + 1, hashLock, timeout);
    CHECK(id != id2, "contractId changes with amount");
  }

  // ── ABI selectors ────────────────────────────────────────────────────
  {
    auto sel = [](const std::string& sig) {
      uint8_t md[32];
      keccak(reinterpret_cast<const uint8_t*>(sig.data()), static_cast<int>(sig.size()), md, 32);
      return toHex(md, 4);
    };
    std::string lockSel = EthAbi::functionSelector("lock(address,bytes32,uint256)");
    // functionSelector returns 0x-prefixed 4-byte hex typically
    CHECK(lockSel.size() >= 8, "lock selector non-empty");
    std::string claimSel = EthAbi::functionSelector("claim(bytes32,bytes32)");
    CHECK(claimSel.size() >= 8, "claim selector non-empty");
    std::string getSel = EthAbi::functionSelector("getContract(bytes32)");
    CHECK(getSel.size() >= 8, "getContract selector non-empty");

    // encodeLock starts with lock selector
    Crypto::Hash hl{};
    std::memset(hl.data, 0xab, 32);
    std::string enc = EthAbi::encodeLock(
        "0x2222222222222222222222222222222222222222", hl, 999);
    CHECK(enc.size() > 10 && enc[0] == '0' && enc[1] == 'x', "encodeLock returns 0x hex");
  }

  // ── Full-width native EVM amounts ──────────────────────────────────
  {
    uint64_t narrow = 0;
    AtomicAmount u64Max = 0;
    AtomicAmount u64Next = 0;
    CHECK(parseAtomicAmount("18446744073709551615", u64Max) &&
          atomicAmountToUint64(u64Max, narrow) && narrow == UINT64_MAX,
          "2^64-1 parses and narrows exactly");
    CHECK(parseAtomicAmount("18446744073709551616", u64Next) &&
          !atomicAmountToUint64(u64Next, narrow),
          "2^64 parses but is rejected by uint64 adapters");
    AtomicAmount hundredEth = 0;
    CHECK(parseAtomicAmount("100000000000000000000", hundredEth),
          "100 ETH parses as uint256 atomic amount");
    CHECK(!atomicAmountToUint64(hundredEth, narrow),
          "100 ETH cannot silently narrow to uint64");
    const std::string max256 =
        "115792089237316195423570985008687907853269984665640564039457584007913129639935";
    AtomicAmount boundary = 0;
    CHECK(parseAtomicAmount(max256, boundary) &&
          atomicAmountToString(boundary) == max256,
          "uint256 maximum parses without loss");
    CHECK(!parseAtomicAmount(
          "115792089237316195423570985008687907853269984665640564039457584007913129639936",
          boundary), "uint256 maximum plus one is rejected");
    CHECK(!parseAtomicAmount("01", boundary) && !parseAtomicAmount("-1", boundary),
          "noncanonical and signed decimal amounts are rejected");
    CHECK(!parseAtomicAmount("+1", boundary) && !parseAtomicAmount("1x", boundary),
          "malformed decimal amount text is rejected");
    const uint64_t gasLimit = 200000;
    const uint64_t feePerGas = 20000000000ULL;
    const AtomicAmount feeWei = AtomicAmount(gasLimit) * feePerGas;
    CHECK(EthRpcClientTestAccess::funding(hundredEth + feeWei, hundredEth,
                                          gasLimit, feePerGas, 180000),
          "balance exactly covers uint256 lock value plus buffered EVM fee");
    CHECK(!EthRpcClientTestAccess::funding(hundredEth + feeWei - 1, hundredEth,
                                           gasLimit, feePerGas, 180000),
          "one wei short of lock value plus fee is rejected");
    CHECK(!EthRpcClientTestAccess::funding(hundredEth + feeWei, hundredEth,
                                           gasLimit, feePerGas, gasLimit + 1),
          "RPC gas estimate above selected limit is rejected");
    const AtomicAmount maxU256 = (std::numeric_limits<AtomicAmount>::max)();
    CHECK(!EthRpcClientTestAccess::funding(maxU256, maxU256, 1, 1, 1),
          "uint256 amount plus fee overflow is rejected before signing");
    const AtomicAmount maxU64 = UINT64_MAX;
    const AtomicAmount maxFeeProduct = maxU64 * maxU64;
    CHECK(EthRpcClientTestAccess::funding(maxFeeProduct, 0, UINT64_MAX,
                                          UINT64_MAX, UINT64_MAX) &&
          !EthRpcClientTestAccess::funding(maxFeeProduct - 1, 0, UINT64_MAX,
                                           UINT64_MAX, UINT64_MAX),
          "uint64 gas fee product is widened exactly without truncation");
    uint64_t priority = 0, maxFee = 0;
    CHECK(EthRpcClientTestAccess::fees(1000000000ULL, 249500000000ULL,
                                        priority, maxFee) &&
          priority == 1000000000ULL && maxFee == 500000000000ULL,
          "EIP-1559 quote at the 500 gwei ceiling is accepted exactly");
    CHECK(!EthRpcClientTestAccess::fees(1000000000ULL, 249500000001ULL,
                                         priority, maxFee) &&
          !EthRpcClientTestAccess::fees(500000000001ULL, 0,
                                         priority, maxFee),
          "EIP-1559 base-fee or tip quote above the ceiling is rejected");
    const std::string sender = "1111111111111111111111111111111111111111";
    const std::string recip = "2222222222222222222222222222222222222222";
    const std::string hashLock(64, 'a');
    const std::string id = EthRpcClient::computeContractId(sender, recip, hundredEth,
                                                             hashLock, 12345678);
    std::vector<uint8_t> packed;
    auto pushHex = [&](const std::string& hex) {
      for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned byte = 0;
        sscanf(hex.c_str() + i, "%2x", &byte);
        packed.push_back(static_cast<uint8_t>(byte));
      }
    };
    pushHex(sender);
    pushHex(recip);
    const auto bytes = atomicAmountToBigEndian(hundredEth);
    packed.insert(packed.end(), bytes.begin(), bytes.end());
    pushHex(hashLock);
    const auto timeout = atomicAmountToBigEndian(12345678);
    packed.insert(packed.end(), timeout.begin(), timeout.end());
    uint8_t digest[32];
    keccak(packed.data(), static_cast<int>(packed.size()), digest, 32);
    CHECK(id == toHex(digest, 32), "100 ETH contract ID packs all 256 amount bits");

    auto hexWord = [&](const std::array<uint8_t, 32>& word) {
      return toHex(word.data(), word.size());
    };
    const std::string abi = "0x" + std::string(24, '0') + sender +
        std::string(24, '0') + recip + hexWord(bytes) + hashLock +
        hexWord(timeout) + std::string(63, '0') + "0" +
        std::string(63, '0') + "0" + std::string(64, '0');
    EthAbi::ContractInfo info{};
    CHECK(EthAbi::decodeGetContract(abi, info) && info.amount == hundredEth,
          "ABI getContract decodes high amount bits");

    CryptoNote::SwapDaemon::Crypto::RlpEncoder rlp;
    rlp.writeUint256(bytes.data());
    const auto encoded = rlp.finalize();
    CHECK(encoded.size() > 8 && encoded[0] != 0x80,
          "RLP uint256 value remains nonzero above uint64");

    PeerMessage kx{};
    kx.type = PeerMessageType::KEY_EXCHANGE;
    kx.swapId = "wide-amount-test";
    kx.keyExchange.amountProtocolVersion = 2;
    kx.keyExchange.pair = SwapPair::ETH;
    kx.keyExchange.xfgAmount = 1000000000;
    kx.keyExchange.ctrAmount = hundredEth;
    const auto originalDigest = peerMessageDigest(kx);
    PeerMessage decoded{};
    CHECK(deserializePeerMessage(serializePeerMessage(kx), decoded) &&
          decoded.keyExchange.ctrAmount == hundredEth &&
          peerMessageDigest(decoded) == originalDigest,
          "v2 peer wire preserves and signs full-width amount");
    kx.keyExchange.ctrAmount += 1;
    CHECK(peerMessageDigest(kx) != originalDigest,
          "v2 peer signature digest binds exact atomic amount");

    // A v1 key exchange contains no amount binding and must remain visibly v1
    // after decoding so the receiving v2 state machine can reject it.
    PeerMessage legacy{};
    legacy.type = PeerMessageType::KEY_EXCHANGE;
    legacy.swapId = kx.swapId;
    legacy.keyExchange.swapPubKey = kx.keyExchange.swapPubKey;
    PeerMessage legacyDecoded{};
    CHECK(deserializePeerMessage(serializePeerMessage(legacy), legacyDecoded) &&
          legacyDecoded.keyExchange.amountProtocolVersion == 1 &&
          legacyDecoded.keyExchange.amountProtocolVersion != 2,
          "legacy peer message remains identifiable as a downgrade");

    // Exercise the production signing builders offline and decode their signed
    // RLP payloads to prove the 100 ETH value survives both envelopes.
    const std::string privKey(63, '0');
    const std::string keyHex = privKey + "1";
    CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
    const auto pubkey = signer.derivePublicKey([] {
      std::array<uint8_t, 32> key{}; key.back() = 1; return key;
    }());
    uint8_t addressHash[32];
    keccak(pubkey.data() + 1, 64, addressHash, 32);
    const std::string signerAddress = "0x" + toHex(addressHash + 12, 20);
    EthRpcClient legacyClient("http://127.0.0.1:1", 1, keyHex, signerAddress, 1,
                              EthTxType::Legacy);
    EthRpcClient dynamicClient("http://127.0.0.1:1", 1, keyHex, signerAddress, 1,
                               EthTxType::Eip1559);
    EthRpcClient readOnlyClient("http://127.0.0.1:1", 1);
    CHECK(EthRpcClientTestAccess::estimateGasParams(
              legacyClient, "0x2222222222222222222222222222222222222222",
              "0xaabb", hundredEth) ==
          "[{\"from\":\"" + signerAddress +
              "\",\"to\":\"0x2222222222222222222222222222222222222222\","
              "\"data\":\"0xaabb\",\"value\":\"0x56bc75e2d63100000\"},\"latest\"]",
          "eth_estimateGas request includes configured signer and exact native value");
    CHECK(EthRpcClientTestAccess::estimateGasParams(
              dynamicClient, "", "0x6000", 0) ==
          "[{\"from\":\"" + signerAddress +
              "\",\"data\":\"0x6000\",\"value\":\"0x0\"},\"latest\"]",
          "contract deployment estimate includes signer and omits to");
    CHECK(EthRpcClientTestAccess::estimateGasParams(
              readOnlyClient, "0x2222222222222222222222222222222222222222",
              "0xaabb", hundredEth) ==
          "[{\"to\":\"0x2222222222222222222222222222222222222222\","
              "\"data\":\"0xaabb\",\"value\":\"0x56bc75e2d63100000\"},\"latest\"]",
          "read-only gas estimate omits an unconfigured sender");
    const auto legacyTx = EthRpcClientTestAccess::legacy(legacyClient, hundredEth);
    const auto dynamicTx = EthRpcClientTestAccess::eip1559(dynamicClient, hundredEth);
    CHECK(signedTxValueEquals(legacyTx, false, 4, hundredEth),
          "signed EIP-155 transaction retains exact 100 ETH value");
    CHECK(!dynamicTx.empty() && dynamicTx[0] == 0x02 &&
          signedTxValueEquals(dynamicTx, true, 6, hundredEth),
          "signed EIP-1559 transaction retains exact 100 ETH value");
  }

  // ── RLP empty list = 0xc0 ────────────────────────────────────────────
  {
    CryptoNote::SwapDaemon::Crypto::RlpEncoder bare;
    bare.writeEmptyList();
    auto out = bare.finalize();
    CHECK(out.size() == 1 && out[0] == 0xc0, "writeEmptyList encodes 0xc0 not 0x80");
  }

  std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
