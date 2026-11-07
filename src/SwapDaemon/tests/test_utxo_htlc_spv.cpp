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

// Bob-only SPV swap path (remediation plan 2.5) through the BTC, LTC and BCH
// chain clients: lock verification, claim, refund recovery, claim discovery
// and role-aware readiness, against a scripted SPV client.

#include "SwapDaemon/Bitcoin/BtcChainClient.h"
#include "SwapDaemon/BitcoinCash/BchChainClient.h"
#include "SwapDaemon/Litecoin/LtcChainClient.h"
#include "SwapDaemon/Spv/ElectrumSpvClient.h"
#include "SwapDaemon/SwapTimelock.h"
#include "SwapDaemon/Utxo/UtxoHtlcSwap.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL: %s\n", label.c_str());
  }
}

static bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

class ScriptedSpv : public ISpvClient {
public:
  std::string protocolName() const override { return "scripted"; }
  bool syncHeaders() override { return syncs; }
  bool getTipHeight(uint64_t& height) override {
    height = tip;
    return tipAgreed;
  }
  bool verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) override {
    auto it = inclusions.find(txid);
    out = it == inclusions.end() ? SpvTxInclusion() : it->second;
    return it != inclusions.end();
  }
  bool findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) override {
    auto it = spends.find(txid + ":" + std::to_string(vout));
    out = it == spends.end() ? SpvSpend() : it->second;
    return true;
  }
  bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) override {
    auto it = rawTxs.find(txid);
    if (it == rawTxs.end()) return false;
    rawTx = it->second;
    return true;
  }
  bool broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) override {
    broadcasts.push_back(rawTx);
    txid = ElectrumSpvClient::computeTransactionId(rawTx);
    return acceptBroadcast && !txid.empty();
  }
  std::string newSwapTrustError() override { return trustError; }
  bool feeRatesPerKilobyte(uint64_t& relayFloor, uint64_t& nextBlocks) override {
    relayFloor = floorRate;
    nextBlocks = targetRate;
    return feesReported;
  }

  bool syncs = true;
  bool tipAgreed = true;
  uint64_t tip = 900000;
  bool acceptBroadcast = true;
  bool feesReported = true;
  uint64_t floorRate = 1000;
  uint64_t targetRate = 20000;
  std::string trustError;
  std::map<std::string, SpvTxInclusion> inclusions;
  std::map<std::string, SpvSpend> spends;
  std::map<std::string, std::vector<uint8_t>> rawTxs;
  std::vector<std::vector<uint8_t>> broadcasts;
};

using ClientFactory = std::function<std::unique_ptr<IChainClient>(std::shared_ptr<ISpvClient>, const std::string&)>;

struct ChainCase {
  std::string ticker;
  SwapPair pair;
  UtxoHtlcEncoding encoding;
  UtxoNetworkParams network;
  std::string aliceWif;
  std::string bobWif;
  ClientFactory makeClient;
};

static std::array<uint8_t, 32> filledKey(uint8_t byte) {
  std::array<uint8_t, 32> key;
  key.fill(byte);
  return key;
}

static std::vector<uint8_t> publicKeyOf(uint8_t byte) {
  std::vector<uint8_t> pub;
  compressedPublicKey(filledKey(byte), pub);
  return pub;
}

static void appendLE(std::vector<uint8_t>& out, uint64_t value, size_t width) {
  for (size_t i = 0; i < width; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

static std::vector<uint8_t> fundingTransaction(const std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& outputs) {
  std::vector<uint8_t> tx;
  appendLE(tx, 2, 4);
  tx.push_back(1);
  tx.insert(tx.end(), 32, 0x33);
  appendLE(tx, 0, 4);
  tx.push_back(0);
  appendLE(tx, 0xFFFFFFFF, 4);
  tx.push_back(static_cast<uint8_t>(outputs.size()));
  for (const auto& output : outputs) {
    appendLE(tx, output.first, 8);
    tx.push_back(static_cast<uint8_t>(output.second.size()));
    tx.insert(tx.end(), output.second.begin(), output.second.end());
  }
  appendLE(tx, 0, 4);
  return tx;
}

static uint32_t lockTimeOf(const std::vector<uint8_t>& rawTx) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(rawTx[rawTx.size() - 4 + i]) << (8 * i);
  return value;
}

static const uint64_t LOCK_VALUE = 100000;
static const uint32_t LOCK_VOUT = 1;

struct Swap {
  SwapParams params;
  std::vector<uint8_t> script;
  std::string lockTxid;
  uint32_t timeout = 0;
};

static uint64_t claimWindow(SwapPair pair) {
  uint64_t window = 0;
  minimumCounterpartyWindowBlocks(pair, 6, window);
  return window;
}

static Swap makeSwap(const ChainCase& chain, ScriptedSpv& spv, uint32_t timeout, uint8_t recipientKeyByte = 0x22) {
  Swap swap{};
  swap.timeout = timeout;
  std::memset(&swap.params.adaptorSecret, 0x5a, sizeof(swap.params.adaptorSecret));
  const std::vector<uint8_t> secret = adaptorSecretBytes(swap.params);
  UtxoHtlcTerms terms;
  terms.hashLock = sha256Digest(secret);
  terms.recipientPubKey = publicKeyOf(recipientKeyByte);
  terms.senderPubKey = publicKeyOf(0x11);
  terms.timeoutBlock = timeout;
  swap.script = buildHtlcScript(terms);
  const std::vector<uint8_t> change = p2pkhScriptForPubKey(publicKeyOf(0x44));
  const std::vector<uint8_t> raw =
      fundingTransaction({{5000, change}, {LOCK_VALUE, htlcScriptPubKey(chain.encoding, swap.script)}});
  swap.lockTxid = ElectrumSpvClient::computeTransactionId(raw);
  spv.rawTxs[swap.lockTxid] = raw;
  spv.inclusions[swap.lockTxid] = {true, spv.tip - 9, 10, true};
  swap.params.pair = chain.pair;
  swap.params.ctrLockTxId = swap.lockTxid;
  swap.params.ctrLockVout = LOCK_VOUT;
  swap.params.ctrAmount = LOCK_VALUE;
  swap.params.chainState = encodeHex(swap.script);
  return swap;
}

static void testReadiness(const ChainCase& chain) {
  auto spv = std::make_shared<ScriptedSpv>();
  auto bob = chain.makeClient(spv, chain.bobWif);
  check(contains(bob->readinessErrorFor(SwapRole::ALICE), "Alice needs a full node"),
        chain.ticker + ": SPV refuses Alice (cannot fund a lock)");
  check(bob->readinessErrorFor(SwapRole::BOB).empty(), chain.ticker + ": SPV serves Bob under a trusted quorum");
  check(!bob->isReadyForNewSwap(), chain.ticker + ": role-agnostic readiness stays strict");
  spv->trustError = "SPV needs at least 3 Electrum servers";
  check(contains(bob->readinessErrorFor(SwapRole::BOB), "at least 3 Electrum servers"),
        chain.ticker + ": an untrusted quorum blocks Bob");
  auto keyless = chain.makeClient(std::make_shared<ScriptedSpv>(), chain.aliceWif.substr(1));
  check(contains(keyless->readinessErrorFor(SwapRole::BOB), "no WIF key"), chain.ticker + ": bad WIF blocks Bob");
  check(bob->getClaimPubKey() == encodeHex(publicKeyOf(0x22)), chain.ticker + ": claim key from WIF");
  check(bob->getReceiveAddress() == p2pkhAddress(chain.network, publicKeyOf(0x22)),
        chain.ticker + ": receive address on the configured network");
  check(!bob->lock(SwapParams()).success, chain.ticker + ": SPV never funds a lock");
}

static void testVerifyLock(const ChainCase& chain) {
  auto spv = std::make_shared<ScriptedSpv>();
  auto bob = chain.makeClient(spv, chain.bobWif);
  const uint32_t timeout = static_cast<uint32_t>(spv->tip + claimWindow(chain.pair) + 10);
  Swap swap = makeSwap(chain, *spv, timeout);

  ChainClientResult verified = bob->verifyLock(swap.params);
  check(verified.success && verified.confirmed && verified.spvVerified && verified.confirmations == 10 &&
            verified.outputIndex == LOCK_VOUT,
        chain.ticker + ": genuine buried lock accepted (" + verified.error + ")");

  SwapParams wrongVout = swap.params;
  wrongVout.ctrLockVout = 0;
  check(contains(bob->verifyLock(wrongVout).error, "does not commit to the lock script"),
        chain.ticker + ": other output of the lock transaction refused");
  SwapParams missingOutput = swap.params;
  missingOutput.ctrLockVout = 7;
  check(contains(bob->verifyLock(missingOutput).error, "has no output 7"), chain.ticker + ": absent output refused");
  SwapParams short_ = swap.params;
  short_.ctrAmount = LOCK_VALUE + 1;
  check(contains(bob->verifyLock(short_).error, "expected at least"), chain.ticker + ": short lock refused");
  SwapParams otherSecret = swap.params;
  std::memset(&otherSecret.adaptorSecret, 0x5b, sizeof(otherSecret.adaptorSecret));
  check(contains(bob->verifyLock(otherSecret).error, "hash differs"), chain.ticker + ": other hash lock refused");
  SwapParams unknownTx = swap.params;
  unknownTx.ctrLockTxId = std::string(64, 'e');
  check(contains(bob->verifyLock(unknownTx).error, "unavailable"), chain.ticker + ": unknown lock tx refused");
  SwapParams taprootStateOnHtlc = swap.params;
  taprootStateOnHtlc.chainState = "p2tr:02" + std::string(64, '3') + "|ptlc:" + std::string(64, '4');
  check(!bob->verifyLock(taprootStateOnHtlc).success && !bob->claimDeadlineError(taprootStateOnHtlc).empty(),
        chain.ticker + ": a p2tr lock state on a swap not negotiated as pure PTLC is refused");
  SwapParams malformedTaproot = taprootStateOnHtlc;
  malformedTaproot.lockType = SwapLockType::PTLC;
  malformedTaproot.chainState = "p2tr:zz";
  check(!bob->verifyLock(malformedTaproot).success, chain.ticker + ": a malformed p2tr lock state is refused");

  Swap otherRecipient = makeSwap(chain, *spv, timeout, 0x66);
  check(contains(bob->verifyLock(otherRecipient.params).error, "does not pay our claim key"),
        chain.ticker + ": lock paying another key refused");
  Swap shortRunway = makeSwap(chain, *spv, static_cast<uint32_t>(spv->tip + claimWindow(chain.pair) - 1));
  check(contains(bob->verifyLock(shortRunway.params).error, "too few blocks"),
        chain.ticker + ": lock expiring inside the claim window refused");

  spv->inclusions[swap.lockTxid] = {true, spv->tip - 4, 5, true};
  check(contains(bob->verifyLock(swap.params).error, "5 of 6"), chain.ticker + ": shallow lock refused");
  spv->inclusions[swap.lockTxid] = {true, spv->tip - 9, 10, false};
  check(contains(bob->verifyLock(swap.params).error, "quorum-verified"),
        chain.ticker + ": inclusion without a quorum Merkle proof refused");
  spv->inclusions[swap.lockTxid] = {false, 0, 0, false};
  check(!bob->verifyLock(swap.params).success, chain.ticker + ": unconfirmed lock refused");
  spv->inclusions[swap.lockTxid] = {true, spv->tip - 9, 10, true};
  spv->tipAgreed = false;
  check(contains(bob->verifyLock(swap.params).error, "do not agree on the chain tip"),
        chain.ticker + ": no tip quorum, no verification");
}

static bool spendsOutpoint(const std::vector<uint8_t>& rawTx, const std::string& txid, uint32_t vout) {
  std::vector<uint8_t> txidBytes;
  decodeHex(txid, txidBytes);
  std::vector<uint8_t> littleEndian(txidBytes.rbegin(), txidBytes.rend());
  const size_t inputStart = rawTx[4] == 0x00 ? 7 : 5;
  return std::equal(littleEndian.begin(), littleEndian.end(), rawTx.begin() + inputStart) &&
         rawTx[inputStart + 32] == static_cast<uint8_t>(vout);
}

static void testClaim(const ChainCase& chain) {
  auto spv = std::make_shared<ScriptedSpv>();
  auto bob = chain.makeClient(spv, chain.bobWif);
  Swap swap = makeSwap(chain, *spv, static_cast<uint32_t>(spv->tip + claimWindow(chain.pair) + 10));
  swap.params.ctrAddress = p2pkhAddress(chain.network, publicKeyOf(0x33));

  ChainClientResult claimed = bob->claim(swap.params);
  check(claimed.success && spv->broadcasts.size() == 1, chain.ticker + ": claim broadcast (" + claimed.error + ")");
  if (spv->broadcasts.size() != 1) return;
  const std::vector<uint8_t>& claimTx = spv->broadcasts.front();
  uint64_t value = 0;
  std::vector<uint8_t> paidTo;
  const uint64_t fee = (20000 * htlcSpendVirtualSize(chain.encoding, true) + 999) / 1000;
  check(readTransactionOutput(claimTx, 0, value, paidTo) && value == LOCK_VALUE - fee,
        chain.ticker + ": claim pays the lock value less the two-block fee");
  check(paidTo == p2pkhScriptForPubKey(publicKeyOf(0x33)), chain.ticker + ": claim pays Bob's configured address");
  check(spendsOutpoint(claimTx, swap.lockTxid, LOCK_VOUT) && lockTimeOf(claimTx) == 0,
        chain.ticker + ": claim spends the lock outpoint with no lock time");
  std::vector<uint8_t> preimage;
  check(extractHtlcPreimage(chain.encoding, claimTx, lockOutpoint(swap.params), swap.script, preimage) &&
            preimage == adaptorSecretBytes(swap.params),
        chain.ticker + ": claim reveals t through the claim branch");

  spv->broadcasts.clear();
  spv->floorRate = 200000;
  bob->claim(swap.params);
  check(spv->broadcasts.size() == 1 && readTransactionOutput(spv->broadcasts.front(), 0, value, paidTo) &&
            LOCK_VALUE - value == (200000 * htlcSpendVirtualSize(chain.encoding, true) + 999) / 1000,
        chain.ticker + ": a relay floor above the tenth-of-value ceiling still wins");
  spv->floorRate = 1000;

  spv->broadcasts.clear();
  spv->inclusions[swap.lockTxid] = {true, spv->tip - 1, 2, true};
  const ChainClientResult unburied = bob->claim(swap.params);
  check(contains(unburied.error, "not buried") && unburied.nothingSent && spv->broadcasts.empty(),
        chain.ticker + ": t is never broadcast against an unburied lock");
  spv->inclusions[swap.lockTxid] = {true, spv->tip - 9, 10, true};
  spv->feesReported = false;
  const ChainClientResult unpriced = bob->claim(swap.params);
  check(contains(unpriced.error, "no fee rate") && unpriced.nothingSent && spv->broadcasts.empty(),
        chain.ticker + ": no fee rate, no claim");
  spv->feesReported = true;
  spv->acceptBroadcast = false;
  const ChainClientResult refusedBroadcast = bob->claim(swap.params);
  check(contains(refusedBroadcast.error, "no server accepted") && !refusedBroadcast.nothingSent,
        chain.ticker + ": a refused broadcast may still have reached a server, so it is not reported as unsent");
  SwapParams noSecret = swap.params;
  std::memset(&noSecret.adaptorSecret, 0, sizeof(noSecret.adaptorSecret));
  const ChainClientResult withoutSecret = bob->claim(noSecret);
  check(contains(withoutSecret.error, "adaptor secret") && withoutSecret.nothingSent, chain.ticker + ": claim needs t");
}

static void testClaimDeadline(const ChainCase& chain) {
  auto spv = std::make_shared<ScriptedSpv>();
  auto bob = chain.makeClient(spv, chain.bobWif);
  Swap swap = makeSwap(chain, *spv, static_cast<uint32_t>(spv->tip + claimWindow(chain.pair) + 10));
  check(bob->claimDeadlineError(swap.params).empty(), chain.ticker + ": a claim with its runway may proceed");
  const uint64_t runway = claimRunwayBlocks(chain.pair);
  spv->tip = swap.timeout - runway + 1;
  spv->inclusions[swap.lockTxid] = {true, spv->tip - 9, 10, true};
  check(contains(bob->claimDeadlineError(swap.params), "claim runway"),
        chain.ticker + ": the engine is told not to claim inside the runway");
  const ChainClientResult late = bob->claim(swap.params);
  check(contains(late.error, "claim runway") && late.nothingSent && spv->broadcasts.empty(),
        chain.ticker + ": t is never published once Alice's refund is within the runway");
  spv->tip = swap.timeout - runway;
  check(bob->claimDeadlineError(swap.params).empty(), chain.ticker + ": exactly the runway left still claims");
}

static void testRefundAndDiscovery(const ChainCase& chain) {
  auto bobSpv = std::make_shared<ScriptedSpv>();
  auto bob = chain.makeClient(bobSpv, chain.bobWif);
  Swap swap = makeSwap(chain, *bobSpv, static_cast<uint32_t>(bobSpv->tip + claimWindow(chain.pair) + 10));
  bob->claim(swap.params);
  if (bobSpv->broadcasts.size() != 1) {
    check(false, chain.ticker + ": claim fixture");
    return;
  }
  const std::vector<uint8_t> claimTx = bobSpv->broadcasts.front();
  const std::string claimTxid = ElectrumSpvClient::computeTransactionId(claimTx);

  auto aliceSpv = std::make_shared<ScriptedSpv>(*bobSpv);
  aliceSpv->broadcasts.clear();
  auto alice = chain.makeClient(aliceSpv, chain.aliceWif);
  SwapParams aliceView = swap.params;
  std::memset(&aliceView.adaptorSecret, 0, sizeof(aliceView.adaptorSecret));
  std::memcpy(&aliceView.hashLock, sha256Digest(adaptorSecretBytes(swap.params)).data(), 32);
  aliceView.ctrAddress = p2pkhAddress(chain.network, publicKeyOf(0x22));

  check(alice->tryExtractClaimedSecret(aliceView).empty(), chain.ticker + ": nothing to extract before the claim");
  check(contains(alice->refund(aliceView).error, "refund locked until height"),
        chain.ticker + ": refund refused before the timeout");

  aliceSpv->rawTxs[claimTxid] = claimTx;
  aliceView.ctrClaimTxId = claimTxid;
  check(alice->tryExtractClaimedSecret(aliceView) == encodeHex(adaptorSecretBytes(swap.params)),
        chain.ticker + ": t from the announced claim txid");
  aliceView.ctrClaimTxId.clear();

  SpvSpend spend;
  spend.spent = true;
  spend.spendingTxid = claimTxid;
  spend.rawSpendingTx = claimTx;
  spend.inclusion = {true, aliceSpv->tip - 5, 6, true};
  aliceSpv->spends[swap.lockTxid + ":" + std::to_string(LOCK_VOUT)] = spend;
  check(alice->tryExtractClaimedSecret(aliceView) == encodeHex(adaptorSecretBytes(swap.params)),
        chain.ticker + ": t discovered from chain data without the claim txid");
  check(alice->hasConfirmedClaim(aliceView), chain.ticker + ": claim buried six deep is final");
  aliceSpv->spends[swap.lockTxid + ":" + std::to_string(LOCK_VOUT)].inclusion = {true, aliceSpv->tip - 4, 5, true};
  check(!alice->hasConfirmedClaim(aliceView), chain.ticker + ": claim five deep is not final");

  std::vector<uint8_t> forged = claimTx;
  const std::vector<uint8_t> secret = adaptorSecretBytes(swap.params);
  auto at = std::search(forged.begin(), forged.end(), secret.begin(), secret.end());
  if (at != forged.end()) *at ^= 0x01;
  aliceSpv->spends[swap.lockTxid + ":" + std::to_string(LOCK_VOUT)].rawSpendingTx = forged;
  check(alice->tryExtractClaimedSecret(aliceView).empty(), chain.ticker + ": a preimage not matching H(t) is ignored");

  aliceSpv->tip = swap.timeout;
  ChainClientResult refunded = alice->refund(aliceView);
  check(refunded.success && aliceSpv->broadcasts.size() == 1, chain.ticker + ": refund at the timeout (" +
                                                                   refunded.error + ")");
  if (aliceSpv->broadcasts.size() != 1) return;
  const std::vector<uint8_t>& refundTx = aliceSpv->broadcasts.front();
  uint64_t value = 0;
  std::vector<uint8_t> paidTo;
  check(readTransactionOutput(refundTx, 0, value, paidTo) && paidTo == p2pkhScriptForPubKey(publicKeyOf(0x11)),
        chain.ticker + ": refund pays Alice's own key, never the counterparty address");
  check(lockTimeOf(refundTx) == swap.timeout && spendsOutpoint(refundTx, swap.lockTxid, LOCK_VOUT),
        chain.ticker + ": refund spends the lock with nLockTime at the timeout");
}

static void testTransactionDetails(const ChainCase& chain) {
  auto spv = std::make_shared<ScriptedSpv>();
  auto bob = chain.makeClient(spv, chain.bobWif);
  const std::string txid(64, 'c');
  ChainClientResult details;
  bob->getTransactionDetails("not-a-txid", details);
  check(!details.success, chain.ticker + ": malformed txid refused");
  bob->getTransactionDetails(txid, details);
  check(!details.success, chain.ticker + ": unknown transaction is not confirmed");
  spv->inclusions[txid] = {true, spv->tip - 2, 3, false};
  bob->getTransactionDetails(txid, details);
  check(details.success && !details.confirmed && details.confirmations == 0,
        chain.ticker + ": inclusion without quorum proof is unconfirmed");
  spv->inclusions[txid] = {true, spv->tip - 5, 6, true};
  bob->getTransactionDetails(txid, details);
  check(details.success && details.confirmed && details.spvVerified && details.confirmations == 6,
        chain.ticker + ": quorum-proven inclusion counts its depth");
  uint64_t height = 0;
  check(bob->getCurrentHeight(height) && height == spv->tip, chain.ticker + ": height from the quorum tip");
  spv->syncs = false;
  check(!bob->getCurrentHeight(height), chain.ticker + ": no header sync, no height");
}

static void testFeeBound() {
  check(boundedFee(1000, 20000, 150, 10000) == 3000, "fee at the two-block rate");
  check(boundedFee(1000, 200000, 150, 10000) == 10000, "fee capped at the ceiling");
  check(boundedFee(100000, 20000, 150, 10000) == 15000, "relay floor overrides the ceiling");
  check(boundedFee(1000, 0, 150, 10000) == 150, "missing estimate falls back to the floor");
}

int main() {
  const std::vector<ChainCase> chains = {
      {"BTC", SwapPair::BTC, UtxoHtlcEncoding::P2WSH, bitcoinNetworkParams(UtxoNetwork::MAINNET),
       "KwntMbt59tTsj8xqpqYqRRWufyjGunvhSyeMo3NTYpFYzZbXJ5Hp", "KxN4XYdzu6f9j3EMryaMwZvUVLk3y29M4QZ2xwPoFP2zwka1aWxU",
       [](std::shared_ptr<ISpvClient> spv, const std::string& wif) {
         return std::unique_ptr<IChainClient>(new BtcChainClient(spv, wif));
       }},
      {"LTC", SwapPair::LTC, UtxoHtlcEncoding::P2WSH, litecoinNetworkParams(UtxoNetwork::MAINNET),
       "T3d9oMBFZGSUVybiNUVhdn4HcqNayswbGBYcer117nRiWT8Gph6i", "T4CKyHwBJUdkVssEQcXE9vTrSCPN37AEscTHpk2LpMDATe4itm1R",
       [](std::shared_ptr<ISpvClient> spv, const std::string& wif) {
         return std::unique_ptr<IChainClient>(new LtcChainClient(spv, wif));
       }},
      {"BCH", SwapPair::BCH, UtxoHtlcEncoding::P2SH_FORKID, bitcoinCashNetworkParams(UtxoNetwork::MAINNET),
       "KwntMbt59tTsj8xqpqYqRRWufyjGunvhSyeMo3NTYpFYzZbXJ5Hp", "KxN4XYdzu6f9j3EMryaMwZvUVLk3y29M4QZ2xwPoFP2zwka1aWxU",
       [](std::shared_ptr<ISpvClient> spv, const std::string& wif) {
         return std::unique_ptr<IChainClient>(new BchChainClient(spv, wif));
       }},
  };
  for (const ChainCase& chain : chains) {
    testReadiness(chain);
    testVerifyLock(chain);
    testClaim(chain);
    testClaimDeadline(chain);
    testRefundAndDiscovery(chain);
    testTransactionDetails(chain);
  }
  testFeeBound();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
