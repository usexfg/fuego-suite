// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "SwapDaemon/Utxo/UtxoFullNode.h"
#include "SwapDaemon/Utxo/UtxoHtlc.h"

#include <secp256k1.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const char* label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label);
  }
}

static std::array<uint8_t, 32> filledKey(uint8_t byte) {
  std::array<uint8_t, 32> key;
  key.fill(byte);
  return key;
}

static std::vector<uint8_t> hex(const std::string& s) {
  std::vector<uint8_t> bytes;
  decodeHex(s, bytes);
  return bytes;
}

static const char* const LOCK_TXID = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b";
static const uint64_t LOCK_VALUE = 1234567;
static const uint64_t FEE = 2000;
static const uint32_t TIMEOUT = 850000;

static const char* const HTLC_SCRIPT =
    "63a820deb0e38ced1e41de6f92e70e80c418d2d356afaaa99e26f5939dbc7d3ef4772a882102466d7fcae563e5cb"
    "09a0d1870bb580344804617879a14949cf22285f1bae3f27ac670350f80cb17521034f355bdcb7cc0af728ef3cce"
    "b9615d90684bb5b2ca5f859ab0f0b704075871aaac68";

static const char* const P2WSH_CLAIM =
    "020000000001013ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a0100000000fdff"
    "ffff01b7ce1200000000001976a914531260aa2a199e228c537dfa42c82bea2c7c1f4d88ac0447304402205083a1"
    "7905e4cf413a191b0b6e5593b65df79860083a778f8703cb2d5dfb27d30220499132bbb11df2a91025baba21706d"
    "203b536c70a5bd24ddc3e7c5c483b6b64c0120333333333333333333333333333333333333333333333333333333"
    "333333333301017263a820deb0e38ced1e41de6f92e70e80c418d2d356afaaa99e26f5939dbc7d3ef4772a882102"
    "466d7fcae563e5cb09a0d1870bb580344804617879a14949cf22285f1bae3f27ac670350f80cb17521034f355bdc"
    "b7cc0af728ef3cceb9615d90684bb5b2ca5f859ab0f0b704075871aaac6800000000";
static const char* const P2WSH_REFUND =
    "020000000001013ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a0100000000feff"
    "ffff01b7ce1200000000001976a914fc7250a211deddc70ee5a2738de5f07817351cef88ac03473044022047f331"
    "fde789745d6303e7b2fb3e4b0503020e3498a3b5a53c3ba68d22874655022049d5d27735a2e4ee60cd62e1c7be9d"
    "d1324a4f5278ea5b47ad12c2bfd8ad5e2301007263a820deb0e38ced1e41de6f92e70e80c418d2d356afaaa99e26"
    "f5939dbc7d3ef4772a882102466d7fcae563e5cb09a0d1870bb580344804617879a14949cf22285f1bae3f27ac67"
    "0350f80cb17521034f355bdcb7cc0af728ef3cceb9615d90684bb5b2ca5f859ab0f0b704075871aaac6850f80c00";
static const char* const P2SH_CLAIM =
    "02000000013ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a01000000de47304402"
    "205e3c7e62f826d3733a9e00f4bcbdfdc53df7eb591410f631afda7599a1bd9c5e02204d9cd1d30a185d4aacf5da"
    "d82ecc57640d73fee8c391ed0db4fc9eda45deba5941203333333333333333333333333333333333333333333333"
    "333333333333333333514c7263a820deb0e38ced1e41de6f92e70e80c418d2d356afaaa99e26f5939dbc7d3ef477"
    "2a882102466d7fcae563e5cb09a0d1870bb580344804617879a14949cf22285f1bae3f27ac670350f80cb1752103"
    "4f355bdcb7cc0af728ef3cceb9615d90684bb5b2ca5f859ab0f0b704075871aaac68fdffffff01b7ce1200000000"
    "001976a914531260aa2a199e228c537dfa42c82bea2c7c1f4d88ac00000000";
static const char* const P2SH_REFUND =
    "02000000013ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a01000000bd47304402"
    "205fa0accbe73779a30880d707e98ab77b589fdf4610195d824e752a758ee995f6022001876c26e8ebb2a9899af7"
    "267f8c73e3fb5b342f71da6685b9fefc1dc90d647c41004c7263a820deb0e38ced1e41de6f92e70e80c418d2d356"
    "afaaa99e26f5939dbc7d3ef4772a882102466d7fcae563e5cb09a0d1870bb580344804617879a14949cf22285f1b"
    "ae3f27ac670350f80cb17521034f355bdcb7cc0af728ef3cceb9615d90684bb5b2ca5f859ab0f0b704075871aaac"
    "68feffffff01b7ce1200000000001976a914fc7250a211deddc70ee5a2738de5f07817351cef88ac50f80c00";

// Sighash digests of the four transactions above, computed independently with
// python-bitcoinlib 0.11.2: SignatureHash(script, tx, 0, hashtype,
// amount=1234567, sigversion=SIGVERSION_WITNESS_V0), hashtype 0x01 for P2WSH
// and 0x41 (ALL|FORKID, BCH) for P2SH. See tests/vectors/utxo_htlc_sighash.py.
static const char* const P2WSH_CLAIM_DIGEST = "f6569368496949ca0b929137b630602a3dff1638740828fa150c83cd4a326e4c";
static const char* const P2WSH_REFUND_DIGEST = "cc4cb1006376fc2c0af9c323173f739b0132f709a2d643c4b65c528b965bfcd5";
static const char* const P2SH_CLAIM_DIGEST = "07f860638cb6c7f2a589a37d492e48e4bfc947990fa2f172f91164571e328ed1";
static const char* const P2SH_REFUND_DIGEST = "4416a31dae6df14d2cddf9bffbba77d985bb79b91142b8bb001ba8e15e82a2d8";

struct Fixture {
  std::vector<uint8_t> secret = std::vector<uint8_t>(32, 0x33);
  std::vector<uint8_t> alicePub;
  std::vector<uint8_t> bobPub;
  UtxoHtlcTerms terms;
  std::vector<uint8_t> script;

  Fixture() {
    compressedPublicKey(filledKey(0x11), alicePub);
    compressedPublicKey(filledKey(0x22), bobPub);
    terms.hashLock = sha256Digest(secret);
    terms.recipientPubKey = bobPub;
    terms.senderPubKey = alicePub;
    terms.timeoutBlock = TIMEOUT;
    script = buildHtlcScript(terms);
  }

  UtxoHtlcSpend claimSpend() const {
    UtxoHtlcSpend spend;
    spend.lock = {LOCK_TXID, 1};
    spend.lockValue = LOCK_VALUE;
    spend.htlcScript = script;
    spend.signingKey = filledKey(0x22);
    spend.destinationScript = p2pkhScriptForPubKey(bobPub);
    spend.fee = FEE;
    return spend;
  }

  UtxoHtlcSpend refundSpend() const {
    UtxoHtlcSpend spend = claimSpend();
    spend.signingKey = filledKey(0x11);
    spend.destinationScript = p2pkhScriptForPubKey(alicePub);
    return spend;
  }
};

// The DER signature is the first witness item (P2WSH) or the first push of
// the scriptSig (P2SH); both begin with a one-byte length here.
static std::vector<uint8_t> firstSignature(const std::vector<uint8_t>& rawTx, bool witness) {
  if (witness) {
    const size_t outputEnd = 4 + 2 + 1 + 36 + 1 + 4 + 1 + 8 + 1 + 25;
    const size_t sigLen = rawTx[outputEnd + 1];
    return std::vector<uint8_t>(rawTx.begin() + outputEnd + 2, rawTx.begin() + outputEnd + 2 + sigLen);
  }
  const size_t scriptSigStart = 4 + 1 + 36 + 1;
  const size_t sigLen = rawTx[scriptSigStart];
  return std::vector<uint8_t>(rawTx.begin() + scriptSigStart + 1,
                              rawTx.begin() + scriptSigStart + 1 + sigLen);
}

static bool signatureVerifies(const std::vector<uint8_t>& derWithType, const std::string& digestHex,
                              const std::vector<uint8_t>& pubKey) {
  secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
  secp256k1_ecdsa_signature sig;
  secp256k1_pubkey pub;
  const std::vector<uint8_t> digest = hex(digestHex);
  const bool ok =
      derWithType.size() > 1 &&
      secp256k1_ecdsa_signature_parse_der(ctx, &sig, derWithType.data(), derWithType.size() - 1) &&
      secp256k1_ec_pubkey_parse(ctx, &pub, pubKey.data(), pubKey.size()) &&
      secp256k1_ecdsa_verify(ctx, &sig, digest.data(), &pub) == 1;
  secp256k1_context_destroy(ctx);
  return ok;
}

static void testScriptLayout() {
  Fixture f;
  check(encodeHex(f.script) == HTLC_SCRIPT, "HTLC script bytes are the reference layout");
  UtxoHtlcTerms parsed;
  check(parseHtlcScript(f.script, parsed) && parsed.timeoutBlock == TIMEOUT &&
            parsed.recipientPubKey == f.bobPub && parsed.senderPubKey == f.alicePub &&
            parsed.hashLock == f.terms.hashLock,
        "parse recovers every term");
  std::vector<uint8_t> extended = f.script;
  extended.push_back(0x51);
  check(!parseHtlcScript(extended, parsed), "trailing opcode rejected");
  std::vector<uint8_t> swapped = f.script;
  swapped[71] = 0x63;
  check(!parseHtlcScript(swapped, parsed), "altered opcode rejected");
  UtxoHtlcTerms small = f.terms;
  small.timeoutBlock = 16;
  check(buildHtlcScript(small).empty(), "timeouts that need OP_n are refused");
  UtxoHtlcTerms uncompressed = f.terms;
  uncompressed.senderPubKey[0] = 0x04;
  check(buildHtlcScript(uncompressed).empty(), "non-compressed sender key refused");
}

static void testSpendTransactions() {
  Fixture f;
  struct Case {
    UtxoHtlcEncoding encoding;
    bool claim;
    const char* expected;
    const char* digest;
    const char* label;
  } cases[] = {
      {UtxoHtlcEncoding::P2WSH, true, P2WSH_CLAIM, P2WSH_CLAIM_DIGEST, "P2WSH claim"},
      {UtxoHtlcEncoding::P2WSH, false, P2WSH_REFUND, P2WSH_REFUND_DIGEST, "P2WSH refund"},
      {UtxoHtlcEncoding::P2SH_FORKID, true, P2SH_CLAIM, P2SH_CLAIM_DIGEST, "P2SH claim"},
      {UtxoHtlcEncoding::P2SH_FORKID, false, P2SH_REFUND, P2SH_REFUND_DIGEST, "P2SH refund"},
  };
  for (const Case& c : cases) {
    std::vector<uint8_t> rawTx;
    std::string error;
    const bool built = c.claim ? buildHtlcClaim(c.encoding, f.claimSpend(), f.secret, rawTx, error)
                               : buildHtlcRefund(c.encoding, f.refundSpend(), rawTx, error);
    const std::string label(c.label);
    check(built && encodeHex(rawTx) == c.expected, (label + " bytes are stable").c_str());
    const std::vector<uint8_t> signer = c.claim ? f.bobPub : f.alicePub;
    check(signatureVerifies(firstSignature(rawTx, c.encoding == UtxoHtlcEncoding::P2WSH), c.digest, signer),
          (label + " signature verifies against the independent digest").c_str());
    std::vector<UtxoTxOutput> outputs;
    check(parseTransactionOutputs(rawTx, outputs) && outputs.size() == 1 &&
              outputs[0].value == LOCK_VALUE - FEE,
          (label + " pays the lock value minus fee").c_str());
    check(rawTx.size() <= htlcSpendVirtualSize(c.encoding, c.claim) * 4,
          (label + " fits its size estimate").c_str());
  }
}

static void testSpendRefusals() {
  Fixture f;
  std::vector<uint8_t> rawTx;
  std::string error;
  UtxoHtlcSpend wrongClaimer = f.claimSpend();
  wrongClaimer.signingKey = filledKey(0x11);
  check(!buildHtlcClaim(UtxoHtlcEncoding::P2WSH, wrongClaimer, f.secret, rawTx, error),
        "claim with the sender's key refused");
  check(!buildHtlcRefund(UtxoHtlcEncoding::P2WSH, f.claimSpend(), rawTx, error),
        "refund with the recipient's key refused");
  std::vector<uint8_t> wrongSecret(32, 0x34);
  check(!buildHtlcClaim(UtxoHtlcEncoding::P2WSH, f.claimSpend(), wrongSecret, rawTx, error),
        "claim with a wrong preimage refused");
  UtxoHtlcSpend dust = f.claimSpend();
  dust.fee = LOCK_VALUE;
  check(!buildHtlcClaim(UtxoHtlcEncoding::P2WSH, dust, f.secret, rawTx, error),
        "fee consuming the whole lock refused");
}

static void testPreimageExtraction() {
  Fixture f;
  const UtxoOutpoint lock{LOCK_TXID, 1};
  std::vector<uint8_t> preimage;
  check(extractHtlcPreimage(UtxoHtlcEncoding::P2WSH, hex(P2WSH_CLAIM), lock, f.script, preimage) &&
            preimage == f.secret,
        "P2WSH claim reveals the secret");
  preimage.clear();
  check(extractHtlcPreimage(UtxoHtlcEncoding::P2SH_FORKID, hex(P2SH_CLAIM), lock, f.script, preimage) &&
            preimage == f.secret,
        "P2SH claim reveals the secret");
  check(!extractHtlcPreimage(UtxoHtlcEncoding::P2WSH, hex(P2WSH_REFUND), lock, f.script, preimage),
        "refund reveals nothing");
  const UtxoOutpoint otherOutput{LOCK_TXID, 0};
  check(!extractHtlcPreimage(UtxoHtlcEncoding::P2WSH, hex(P2WSH_CLAIM), otherOutput, f.script, preimage),
        "claim of another outpoint is not this lock's claim");
}

static void testAmountsAndKeys() {
  uint64_t units = 0;
  check(formatCoinAmount(12345678) == "0.12345678", "8 decimals kept");
  check(formatCoinAmount(2100000000000000ULL) == "21000000.00000000", "supply formats exactly");
  check(parseCoinAmount(Common::JsonValue::fromString("[0.12345678]")[0], units) && units == 12345678,
        "decimal amount parses exactly");
  check(parseCoinAmount(Common::JsonValue::fromString("[20999999.9769]")[0], units) &&
            units == 2099999997690000ULL,
        "large decimal amount parses exactly");
  check(parseCoinAmount(Common::JsonValue::fromString("[3]")[0], units) && units == 300000000,
        "integer amount parses");
  check(!parseCoinAmount(Common::JsonValue::fromString("[-0.1]")[0], units), "negative amount refused");

  // WIF of key 0x11..11, compressed, mainnet version 0x80.
  const std::string mainnetWif = "KwntMbt59tTsj8xqpqYqRRWufyjGunvhSyeMo3NTYpFYzZbXJ5Hp";
  std::array<uint8_t, 32> key{};
  check(decodeWif(bitcoinNetworkParams(UtxoNetwork::MAINNET), mainnetWif, key) && key == filledKey(0x11),
        "mainnet WIF decodes on mainnet");
  check(!decodeWif(bitcoinNetworkParams(UtxoNetwork::REGTEST), mainnetWif, key),
        "mainnet WIF refused on regtest");

  Fixture f;
  std::vector<uint8_t> script;
  const std::string address = p2pkhAddress(bitcoinNetworkParams(UtxoNetwork::REGTEST), f.bobPub);
  check(p2pkhScriptForAddress(bitcoinNetworkParams(UtxoNetwork::REGTEST), address, script) &&
            script == p2pkhScriptForPubKey(f.bobPub),
        "regtest P2PKH address round-trips");
  check(!p2pkhScriptForAddress(bitcoinNetworkParams(UtxoNetwork::MAINNET), address, script),
        "regtest address refused on mainnet");
  check(htlcFundingAddress(UtxoHtlcEncoding::P2WSH, bitcoinNetworkParams(UtxoNetwork::REGTEST), f.script)
                .compare(0, 5, "bcrt1") == 0,
        "regtest lock address uses the bcrt HRP");
}

// ── A scripted full node ─────────────────────────────────────────────────

struct FakeNode {
  struct Block {
    std::string hash;
    std::vector<std::string> txJson;
  };
  std::vector<Block> chain;  // index = height
  std::map<std::string, std::string> unspent;  // "txid:vout" → gettxout JSON
  std::string chainName = "regtest";
  bool initialDownload = false;
  bool walletLoaded = true;
  bool privateKeys = true;
  std::string walletTxJson;
  std::vector<std::string> calls;

  static std::string blockHash(uint64_t height, char fork) {
    std::string h(64, '0');
    const std::string tail = std::to_string(height);
    h.replace(64 - tail.size(), tail.size(), tail);
    h[0] = fork;
    return h;
  }

  void growTo(uint64_t height, char fork) {
    chain.resize(height + 1);
    for (uint64_t h = 0; h <= height; ++h)
      if (chain[h].hash.empty()) chain[h].hash = blockHash(h, fork);
  }

  UtxoRpcCall rpc() {
    return [this](const std::string& method, const std::string& params, Common::JsonValue& result,
                  std::string& error) {
      calls.push_back(method);
      const Common::JsonValue args = Common::JsonValue::fromString(params);
      std::string json;
      if (method == "getblockcount") {
        json = std::to_string(chain.size() - 1);
      } else if (method == "getblockhash") {
        const uint64_t h = static_cast<uint64_t>(args[0].getInteger());
        if (h >= chain.size()) {
          error = "Block height out of range";
          return false;
        }
        json = "\"" + chain[h].hash + "\"";
      } else if (method == "getblock") {
        for (const auto& block : chain) {
          if (block.hash != args[0].getString()) continue;
          json = "{\"tx\":[";
          for (size_t i = 0; i < block.txJson.size(); ++i) json += (i ? "," : "") + block.txJson[i];
          json += "]}";
        }
        if (json.empty()) {
          error = "Block not found";
          return false;
        }
      } else if (method == "gettxout") {
        const std::string key = args[0].getString() + ":" + std::to_string(args[1].getInteger());
        auto it = unspent.find(key);
        json = it == unspent.end() ? "null" : it->second;
      } else if (method == "getblockchaininfo") {
        json = "{\"chain\":\"" + chainName + "\",\"initialblockdownload\":" +
               (initialDownload ? "true" : "false") + "}";
      } else if (method == "getwalletinfo") {
        if (!walletLoaded) {
          error = "No wallet is loaded";
          return false;
        }
        json = std::string("{\"private_keys_enabled\":") + (privateKeys ? "true" : "false") + "}";
      } else if (method == "gettransaction") {
        json = walletTxJson;
      } else {
        error = "Method not found";
        return false;
      }
      result = Common::JsonValue::fromString("[" + json + "]")[0];
      return true;
    };
  }
};

static std::string spendingTxJson(const std::string& txid, const std::string& rawHex) {
  return "{\"txid\":\"" + txid + "\",\"hex\":\"" + rawHex + "\",\"vin\":[{\"txid\":\"" +
         std::string(LOCK_TXID) + "\",\"vout\":1}]}";
}

static std::string unrelatedTxJson() {
  return "{\"txid\":\"" + std::string(64, 'e') + "\",\"hex\":\"00\",\"vin\":[{\"coinbase\":\"00\"}]}";
}

static void testSpendTracker() {
  const UtxoOutpoint lock{LOCK_TXID, 1};
  const std::string claimTxid(64, 'c');
  FakeNode node;
  node.growTo(103, 'a');
  for (auto& block : node.chain) block.txJson.push_back(unrelatedTxJson());
  node.unspent[lock.key()] = "{\"value\":0.01234567,\"confirmations\":4,\"scriptPubKey\":{\"hex\":\"00\"}}";

  UtxoSpendTracker tracker(2);
  UtxoSpend spend;
  std::string error;
  check(!tracker.findSpend(node.rpc(), lock, 100, spend, error) && error.empty(),
        "unspent lock: no spend and no block scan");
  check(std::count(node.calls.begin(), node.calls.end(), "getblock") == 0, "unspent lock costs no getblock");

  node.unspent.clear();
  node.chain[102].txJson.push_back(spendingTxJson(claimTxid, P2WSH_CLAIM));
  check(!tracker.findSpend(node.rpc(), lock, 100, spend, error) && error.empty(),
        "spend beyond the per-call budget is not found yet");
  check(tracker.findSpend(node.rpc(), lock, 100, spend, error) && spend.blockHeight == 102 &&
            spend.confirmations == 2 && spend.spendingTxid == claimTxid &&
            encodeHex(spend.rawTx) == P2WSH_CLAIM,
        "spend found in block 102 with 2 confirmations");

  node.growTo(104, 'a');
  check(tracker.findSpend(node.rpc(), lock, 100, spend, error) && spend.confirmations == 3,
        "confirmations grow with the tip");

  // Reorg: blocks 102.. are replaced; the claim lands in the new block 103.
  node.chain.resize(102);
  node.growTo(104, 'b');
  node.chain[103].txJson.push_back(spendingTxJson(claimTxid, P2WSH_CLAIM));
  node.calls.clear();
  bool found = false;
  for (int i = 0; i < 4 && !found; ++i) found = tracker.findSpend(node.rpc(), lock, 100, spend, error);
  check(found && spend.blockHeight == 103 && spend.confirmations == 2,
        "reorged claim rediscovered in its new block");

  // Reorg that removes the claim entirely: lock unspent again.
  node.chain.resize(103);
  node.growTo(104, 'd');
  node.unspent[lock.key()] = "{\"value\":0.01234567,\"confirmations\":5,\"scriptPubKey\":{\"hex\":\"00\"}}";
  check(!tracker.findSpend(node.rpc(), lock, 100, spend, error) && error.empty(),
        "claim reorged out: lock reported unspent");

  check(!tracker.findSpend(node.rpc(), lock, 0, spend, error) && !error.empty(),
        "unknown lock height refused");
}

static void testLockVerification() {
  Fixture f;
  const UtxoOutpoint lock{LOCK_TXID, 1};
  FakeNode node;
  node.growTo(10, 'a');
  UtxoLockExpectation expected;
  expected.scriptPubKey = htlcScriptPubKey(UtxoHtlcEncoding::P2WSH, f.script);
  expected.minValue = LOCK_VALUE;
  expected.minConfirmations = 3;
  const std::string spk = encodeHex(expected.scriptPubKey);

  check(!verifyLockedOutput(node.rpc(), lock, expected).success, "missing output fails");
  node.unspent[lock.key()] = "{\"value\":0.01234567,\"confirmations\":3,\"scriptPubKey\":{\"hex\":\"" + spk + "\"}}";
  const ChainClientResult ok = verifyLockedOutput(node.rpc(), lock, expected);
  check(ok.success && ok.confirmations == 3 && ok.outputIndex == 1, "exact lock verifies");
  node.unspent[lock.key()] = "{\"value\":0.01234566,\"confirmations\":3,\"scriptPubKey\":{\"hex\":\"" + spk + "\"}}";
  check(!verifyLockedOutput(node.rpc(), lock, expected).success, "one unit short fails");
  node.unspent[lock.key()] = "{\"value\":0.01234567,\"confirmations\":2,\"scriptPubKey\":{\"hex\":\"" + spk + "\"}}";
  check(!verifyLockedOutput(node.rpc(), lock, expected).success, "too few confirmations fails");
  node.unspent[lock.key()] = "{\"value\":0.01234567,\"confirmations\":3,\"scriptPubKey\":{\"hex\":\"0014" +
                             std::string(40, '0') + "\"}}";
  check(!verifyLockedOutput(node.rpc(), lock, expected).success, "different script fails");
  const UtxoOutpoint injected{"\",\"x", 0};
  check(!verifyLockedOutput(node.rpc(), injected, expected).success, "non-hex txid never reaches the node");
}

static void testWalletOutputIndex() {
  FakeNode node;
  node.walletTxJson = std::string("{\"hex\":\"") + P2WSH_CLAIM + "\",\"blockhash\":\"" + std::string(64, 'b') + "\"}";
  uint32_t vout = 9;
  std::string error;
  Fixture f;
  check(findWalletOutputIndex(node.rpc(), LOCK_TXID, p2pkhScriptForPubKey(f.bobPub), vout, error) && vout == 0,
        "wallet output index found by script");
  check(!findWalletOutputIndex(node.rpc(), LOCK_TXID, p2pkhScriptForPubKey(f.alicePub), vout, error),
        "absent script reported");
}

static void testNodeHealth() {
  FakeNode node;
  check(checkNodeHealth(node.rpc(), "regtest").ready, "synced regtest node with wallet is ready");
  check(!checkNodeHealth(node.rpc(), "main").ready, "chain mismatch not ready");
  node.initialDownload = true;
  check(!checkNodeHealth(node.rpc(), "regtest").ready, "initial block download not ready");
  node.initialDownload = false;
  node.walletLoaded = false;
  check(!checkNodeHealth(node.rpc(), "regtest").ready, "no wallet not ready");
  node.walletLoaded = true;
  node.privateKeys = false;
  check(!checkNodeHealth(node.rpc(), "regtest").ready, "watch-only wallet not ready");
}

int main() {
  testScriptLayout();
  testSpendTransactions();
  testSpendRefusals();
  testPreimageExtraction();
  testAmountsAndKeys();
  testSpendTracker();
  testLockVerification();
  testWalletOutputIndex();
  testNodeHealth();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
