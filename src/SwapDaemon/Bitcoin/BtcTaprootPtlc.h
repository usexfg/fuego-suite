// Copyright (c) 2017-2026 Fuego Developers
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "crypto/hash.h"
#include "crypto/secp_adaptor.h"

namespace XfgSwap {

// P2TR output from PTLC pure (BIP341 style) — no P2WSH.
// internalKey P (33 compressed) + tweak = TaggedHash(TapTweak, P_x || m_swap)
// m_swap = T_x(32) || timeout LE32 || recipient(33) || sender(33)  [102 bytes]
// output P_tweak = P + tweak*G  -> bc1p...
struct TaprootPtlcOutput {
  std::string p2trAddress;                          // bech32m
  std::array<uint8_t, 32> tapTweak{};               // tweak scalar
  std::vector<uint8_t> tweakedPubKey;               // 33 compressed
  std::array<uint8_t, 32> tweakedPubKeyXOnly{};     // 32 x-only
  std::vector<uint8_t> controlBlock;                // 33 bytes: leaf-version|parity || internalX
  std::vector<uint8_t> redeemScript;                // leaf/commitment script (for script-path refund)
};

class BtcTaprootPtlc {
public:
  // TaggedHash("TapTweak", internalPubX(32) || m_swap)
  static std::array<uint8_t,32> computeTapTweak(
      const std::array<uint8_t,32>& internalPubX,
      const std::vector<uint8_t>& m_swap);

  static std::array<uint8_t,32> computeTapTweak(
      const std::vector<uint8_t>& internalKey33,
      const std::vector<uint8_t>& ptlcPointX32,
      uint32_t timeoutBlocks,
      const std::vector<uint8_t>& recipientPub33,
      const std::vector<uint8_t>& senderPub33);

  // Main builder: internalKey 33, ptlcPointX32 32, timeout, recipient 33, sender 33 -> P2TR
  static TaprootPtlcOutput createTaprootPtlcOutput(
      const std::vector<uint8_t>& internalKey33,
      const std::vector<uint8_t>& ptlcPointX32,
      uint32_t timeoutBlocks,
      const std::vector<uint8_t>& recipientPub33,
      const std::vector<uint8_t>& senderPub33,
      const std::string& hrp = "bc");

  // P2.1 canonical builder (same as createTaprootPtlcOutput; output includes controlBlock)
  static TaprootPtlcOutput createTaprootPtlc(
      const std::vector<uint8_t>& internalKey33,
      const std::vector<uint8_t>& ptlcPointX32,
      uint32_t timeoutBlock,
      const std::vector<uint8_t>& recipientPub33,
      const std::vector<uint8_t>& senderPub33,
      const std::string& hrp = "bc");

  // Address helpers (bech32m)
  static std::string tweakedPubToP2trAddress(const std::vector<uint8_t>& tweakedPub33, const std::string& hrp = "bc");
  static std::string xOnlyToP2trAddress(const std::array<uint8_t,32>& xOnly, const std::string& hrp = "bc");

  // Adaptor -> Schnorr for key-path spend: s = s' - t (mod n), R_x from presig.R
  static bool adaptorToSchnorrSig(
      const Crypto::SecpAdaptorPresig& presig,
      const Crypto::SecretKey& t,
      Crypto::SecpSchnorrSig& outSig);

  // Witness stacks
  // Key-path: single 64-byte Schnorr sig (SIGHASH_DEFAULT). For SIGHASH_ALL append 0x01.
  static std::vector<std::vector<uint8_t>> createKeyPathWitness(
      const Crypto::SecpSchnorrSig& sig);

  // P2.1 key-path claim witness from a raw adapted Schnorr sig (64 bytes, or 65 with
  // trailing SIGHASH_ALL byte). Returns witness stack [sig].
  static std::vector<std::vector<uint8_t>> createKeyPathClaimWitness(
      const std::vector<uint8_t>& adaptedSig64);

  // Extract the 64-byte Schnorr sig s from a taproot claim tx witness.
  // Prefers script-path stacks (sig-shaped front item), else single-item key-path stack.
  // Ownership of a bare key-path candidate is proven in parseClaimSecret via presig R_x.
  static std::vector<uint8_t> extractClaimSchnorrSig(
      const std::vector<uint8_t>& rawTx,
      const std::vector<uint8_t>& tweakedPub33);

  // P2.1 claim parse: locate the witness sig whose R_x matches storedPresig.R
  // (key-path or script-path), then t = s' - s via Crypto::secp_adaptor_extract.
  static bool parseClaimSecret(
      const std::vector<uint8_t>& rawTx,
      const std::vector<uint8_t>& tweakedPub33,
      const Crypto::SecpAdaptorPresig& storedPresig,
      Crypto::SecretKey& tOut);

  // Control block for single-leaf script path (no merkle path): 33 bytes = 0xc0|parity || internalX(32)
  static std::vector<uint8_t> createControlBlock(
      const std::vector<uint8_t>& internalKey33,
      const std::vector<uint8_t>& tweakedPub33);

  // Script-path witness: [<sig> <leafScript> <controlBlock>] — order per BIP341: stack items, script, control
  static std::vector<std::vector<uint8_t>> createScriptPathWitness(
      const std::vector<uint8_t>& sig,
      const std::vector<uint8_t>& leafScript,
      const std::vector<uint8_t>& controlBlock);

  // Off-chain adaptor verify wrapper (same e = TaggedHash(Fuego/adaptor_challenge, R||P||msg))
  static bool verifyAdaptorClaim(
      const Crypto::SecpPubKey& P,
      const Crypto::SecpPubKey& T,
      const Crypto::SecpAdaptorPresig& presig,
      const Crypto::Hash& msg);

  // Utilities
  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
  static std::array<uint8_t,32> taggedHash(const std::string& tag, const std::vector<uint8_t>& data);
  static std::array<uint8_t,32> taggedHash(const std::string& tag, const uint8_t* d, size_t len);

  // Build a simple P2TR leaf script committing to the PTLC params (for script-path refund)
  // Produced script: OP_IF <ptlcX> OP_DROP <recipient> OP_CHECKSIG OP_ELSE <timeout> CLTV DROP <sender> OP_CHECKSIG OP_ENDIF
  static std::vector<uint8_t> createPtlcTapLeaf(
      const std::vector<uint8_t>& ptlcPointX32,
      uint32_t timeoutBlocks,
      const std::vector<uint8_t>& recipientPub33,
      const std::vector<uint8_t>& senderPub33);

  // ── P2.2/P2.3: on-chain key-path spend support (BTC + LTC share these) ──

  // P2TR scriptPubKey: OP_1 PUSH32 <x-only tweaked key> (34 bytes).
  static std::vector<uint8_t> p2trScriptPubKey(const std::array<uint8_t,32>& xOnly);

  // BIP341 taproot key-path sighash (SIGHASH_DEFAULT, no annex) for a
  // single-input/single-output transaction spending a P2TR output.
  // spentXOnly is the x-only tweaked key of the output being spent.
  static bool computeTaprootKeyPathSighash(
      const std::string& inputTxid, uint32_t inputVout, uint64_t inputValue,
      const std::array<uint8_t,32>& spentXOnly,
      const std::vector<uint8_t>& destScriptPubKey, uint64_t outputValue,
      uint32_t nVersion, uint32_t nSequence, uint32_t nLockTime,
      std::array<uint8_t,32>& sighashOut);

  // BIP340 signature over `sighash` with the TWEAKED secret key:
  // sk_q = normalize_y(sk_internal) + tapTweak (mod n), mirroring the even-y
  // internal-key normalization done by createTaprootPtlcOutput. The tweaked
  // point Q = sk_q*G is additionally normalized to even y before signing
  // (negate sk_q if Q has odd y). Deterministic nonce k = H(sk_q || sighash
  // || counter); R is even-y per BIP340 and s is NOT low-s flipped (BIP340
  // keeps s > n/2 valid — official vector 8).
  // Returns 64 bytes [R_x || s].
  static bool signTaprootKeyPath(
      const std::array<uint8_t,32>& skInternal,
      const std::array<uint8_t,32>& tapTweak,
      const std::array<uint8_t,32>& tweakedXOnly,
      const std::array<uint8_t,32>& sighash,
      std::vector<uint8_t>& sig64Out);

  // Raw SegWit v1 transaction builder taking an explicit destination
  // scriptPubKey (supports P2TR and legacy outputs alike).
  static std::vector<uint8_t> buildRawTaprootSpendTx(
      const std::string& inputTxid, uint32_t inputVout,
      const std::vector<std::vector<uint8_t>>& witnessStack,
      const std::vector<uint8_t>& destScriptPubKey, uint64_t outputAmount,
      uint32_t nVersion = 2, uint32_t nSequence = 0xFFFFFFFD,
      uint32_t nLockTime = 0);

private:
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);
  static uint32_t bech32Polymod(const std::vector<uint8_t>& values);
  static std::vector<uint8_t> hrpExpand(const std::string& hrp);
  static std::vector<uint8_t> convertBits(const std::vector<uint8_t>& data, int fromBits, int toBits, bool pad);
  static std::string bech32mEncode(const std::string& hrp, const std::vector<uint8_t>& data5);
  // Parse all witness stacks of a SegWit v1 (taproot) transaction.
  static bool parseSegWitWitnesses(
      const std::vector<uint8_t>& rawTx,
      std::vector<std::vector<std::vector<uint8_t>>>& witnesses);
};

} // namespace XfgSwap
