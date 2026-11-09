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

// Counterparty-chain transport gate (remediation plan 1.9): the real BTC, LTC
// and BCH chain clients against a real regtest node. Alice and Bob are two
// client instances with their own keys; only Alice's node wallet funds locks.
//
//   test_utxo_regtest <btc|ltc|bch> <rpc_host> <rpc_port> <rpc_user> <rpc_pass>
//
// The node must run -regtest with one wallet named "alice" loaded or none
// (the test creates it). CI starts the nodes; see .github/workflows/check.yml.

#include "SwapDaemon/Bitcoin/BtcChainClient.h"
#include "SwapDaemon/BitcoinCash/BchChainClient.h"
#include "SwapDaemon/Crypto/Base58Std.h"
#include "SwapDaemon/Litecoin/LtcChainClient.h"
#include "SwapDaemon/Utxo/UtxoFullNode.h"
#include "SwapDaemon/Utxo/UtxoHtlc.h"
#include "SwapDaemon/Utxo/UtxoPointLock.h"
#include "Common/StringTools.h"
#include "crypto/cross_dleq.h"
#include "crypto/secp_adaptor.h"

#include <openssl/rand.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  std::printf("%s  %s\n", condition ? "ok  " : "FAIL", label.c_str());
  if (!condition) ++g_failures;
}

struct Node {
  std::string chain;
  std::string host;
  uint16_t port = 0;
  std::string user;
  std::string pass;

  // A fresh RPC transport per call site, so every client owns its own.
  UtxoRpcCall rpc() const {
    const Node self = *this;
    return [self](const std::string& method, const std::string& params, Common::JsonValue& result,
                  std::string& error) {
      if (self.chain == "ltc") {
        LtcRpcClient client(self.host, self.port, self.user, self.pass);
        return client.callJson(method, params, result, error);
      }
      if (self.chain == "bch") {
        BchRpcClient client(self.host, self.port, self.user, self.pass);
        return client.callJson(method, params, result, error);
      }
      BtcRpcClient client(self.host, self.port, self.user, self.pass);
      return client.callJson(method, params, result, error);
    };
  }

  std::unique_ptr<IChainClient> client(const std::string& wif) const {
    if (chain == "ltc")
      return std::unique_ptr<IChainClient>(new LtcChainClient(
          std::unique_ptr<LtcRpcClient>(new LtcRpcClient(host, port, user, pass)), wif,
          UtxoNetwork::REGTEST));
    if (chain == "bch")
      return std::unique_ptr<IChainClient>(new BchChainClient(
          std::unique_ptr<BchRpcClient>(new BchRpcClient(host, port, user, pass)), wif,
          UtxoNetwork::REGTEST));
    return std::unique_ptr<IChainClient>(new BtcChainClient(
        std::unique_ptr<BtcRpcClient>(new BtcRpcClient(host, port, user, pass)), wif,
        UtxoNetwork::REGTEST));
  }

  SwapPair pair() const {
    return chain == "ltc" ? SwapPair::LTC : chain == "bch" ? SwapPair::BCH : SwapPair::BTC;
  }

  bool call(const std::string& method, const std::string& params, Common::JsonValue& result) const {
    std::string error;
    const bool ok = rpc()(method, params, result, error);
    if (!ok) std::printf("      rpc %s: %s\n", method.c_str(), error.c_str());
    return ok;
  }

  std::string newAddress() const {
    Common::JsonValue result;
    return call("getnewaddress", "[]", result) && result.isString() ? result.getString() : "";
  }

  void mine(int blocks) const {
    Common::JsonValue result;
    call("generatetoaddress", "[" + std::to_string(blocks) + ",\"" + newAddress() + "\"]", result);
  }

  uint64_t tip() const {
    uint64_t height = 0;
    std::string error;
    fetchChainTip(rpc(), height, error);
    return height;
  }

  std::string blockHashAt(uint64_t height) const {
    Common::JsonValue result;
    return call("getblockhash", "[" + std::to_string(height) + "]", result) ? result.getString() : "";
  }
};

static std::vector<uint8_t> randomBytes(size_t size) {
  std::vector<uint8_t> bytes(size);
  RAND_bytes(bytes.data(), static_cast<int>(bytes.size()));
  return bytes;
}

static std::string regtestWif(const std::vector<uint8_t>& key) {
  std::vector<uint8_t> payload = {0xef};
  payload.insert(payload.end(), key.begin(), key.end());
  payload.push_back(0x01);
  const std::vector<uint8_t> checksum = sha256Digest(sha256Digest(payload));
  payload.insert(payload.end(), checksum.begin(), checksum.begin() + 4);
  return Base58Std::encode(payload);
}

struct Swap {
  SwapParams alice;
  SwapParams bob;
  std::vector<uint8_t> secret;
};

// Value-initialized like SwapStateMachine's params: no indeterminate secrets.
static Swap newSwap(const Node& node, IChainClient& bob, uint64_t amount, uint64_t timeoutBlock) {
  Swap swap{};
  swap.secret = randomBytes(32);
  const std::vector<uint8_t> hashLock = sha256Digest(swap.secret);
  for (SwapParams* side : {&swap.alice, &swap.bob}) {
    side->pair = node.pair();
    side->ctrAmount = amount;
    side->ctrTimeoutBlock = timeoutBlock;
    side->requiredConfirmations = 6;
    side->lockType = SwapLockType::PTLC_HTLC_BRIDGE;
    std::memcpy(&side->hashLock, hashLock.data(), 32);
  }
  swap.alice.role = SwapRole::ALICE;
  swap.alice.ctrPubKey = bob.getClaimPubKey();
  swap.bob.role = SwapRole::BOB;
  std::memcpy(&swap.bob.adaptorSecret, swap.secret.data(), 32);
  return swap;
}

// What CTR_LOCKED carries from Alice to Bob.
static void deliverLockNotice(Swap& swap, const ChainClientResult& locked) {
  swap.alice.ctrLockTxId = locked.txId;
  swap.alice.ctrLockVout = locked.outputIndex;
  swap.alice.chainState = locked.chainState;
  swap.bob.ctrLockTxId = locked.txId;
  swap.bob.ctrLockVout = locked.outputIndex;
  swap.bob.chainState = locked.chainState;
}

static void setUpWallet(const Node& node) {
  Common::JsonValue result;
  std::string error;
  if (!node.rpc()("getwalletinfo", "[]", result, error)) node.call("createwallet", "[\"alice\"]", result);
  node.mine(150);  // coinbase maturity plus spendable funds
}

static void testHappyPathWithWithheldClaimTxid(const Node& node, IChainClient& alice, IChainClient& bob) {
  const std::string prefix = node.chain + " happy path: ";
  Swap swap = newSwap(node, bob, 50000000, node.tip() + 400);

  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success, prefix + "Alice locks (" + locked.error + ")");
  if (!locked.success) return;
  const ChainClientResult relocked = alice.lock(swap.alice);
  check(relocked.success && relocked.txId == locked.txId, prefix + "a repeated lock reuses the funding");
  deliverLockNotice(swap, locked);

  check(!bob.verifyLock(swap.bob).success, prefix + "unconfirmed lock refused");
  node.mine(6);
  const ChainClientResult verified = bob.verifyLock(swap.bob);
  check(verified.success && verified.confirmations >= 6, prefix + "lock verifies after 6 blocks (" + verified.error + ")");

  SwapParams shortAmount = swap.bob;
  shortAmount.ctrAmount = 50000001;
  check(!bob.verifyLock(shortAmount).success, prefix + "lock one unit short refused");
  SwapParams wrongOutput = swap.bob;
  wrongOutput.ctrLockVout = swap.bob.ctrLockVout == 0 ? 1 : 0;
  check(!bob.verifyLock(wrongOutput).success, prefix + "other output of the lock tx refused");
  SwapParams wrongHash = swap.bob;
  wrongHash.adaptorSecret.data[0] ^= 0x01;
  check(!bob.verifyLock(wrongHash).success, prefix + "lock for another hash refused");
  check(!alice.verifyLock(swap.bob).success, prefix + "lock not paying the verifier's key refused");

  SwapParams wrongSecret = swap.bob;
  wrongSecret.adaptorSecret.data[1] ^= 0x01;
  check(!bob.claim(wrongSecret).success, prefix + "claim with a wrong secret refused");

  const ChainClientResult claimed = bob.claim(swap.bob);
  check(claimed.success, prefix + "Bob claims (" + claimed.error + ")");
  if (!claimed.success) return;

  // Alice is never told the claim txid: she finds it on chain.
  node.mine(1);
  check(alice.tryExtractClaimedSecret(swap.alice) == encodeHex(swap.secret),
        prefix + "Alice recovers t from the chain without a claim txid");
  check(!alice.hasConfirmedClaim(swap.alice), prefix + "1 confirmation is not final");
  node.mine(5);
  check(alice.hasConfirmedClaim(swap.alice), prefix + "claim final at 6 confirmations");

  ChainClientResult claimStatus;
  bob.getTransactionDetails(claimed.txId, claimStatus);
  check(claimStatus.success && claimStatus.confirmations >= 6, prefix + "Bob sees his claim confirmed");

  // Reorg: drop the claim's block; it returns to the mempool, unconfirmed.
  const uint64_t claimHeight = node.tip() - 5;
  const std::string claimBlock = node.blockHashAt(claimHeight);
  Common::JsonValue ignored;
  node.call("invalidateblock", "[\"" + claimBlock + "\"]", ignored);
  check(!alice.hasConfirmedClaim(swap.alice), prefix + "reorged claim is not final");
  node.call("reconsiderblock", "[\"" + claimBlock + "\"]", ignored);
  check(alice.hasConfirmedClaim(swap.alice), prefix + "claim final again after the reorg resolves");

  // Restart: a fresh client rescans from the lock block.
  std::unique_ptr<IChainClient> restarted = node.client(regtestWif(std::vector<uint8_t>(32, 0x11)));
  SwapParams restartedView = swap.alice;
  check(restarted->tryExtractClaimedSecret(restartedView) == encodeHex(swap.secret),
        prefix + "restarted client rediscovers the claim");
}

static void testRefundAfterTimeout(const Node& node, IChainClient& alice, IChainClient& bob) {
  const std::string prefix = node.chain + " refund: ";
  Swap swap = newSwap(node, bob, 30000000, node.tip() + 20);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success, prefix + "Alice locks (" + locked.error + ")");
  if (!locked.success) return;
  deliverLockNotice(swap, locked);
  node.mine(1);
  check(!bob.verifyLock(swap.bob).success, prefix + "Bob refuses a lock without claim runway");
  const ChainClientResult lateClaim = bob.claim(swap.bob);
  check(!bob.claimDeadlineError(swap.bob).empty() && !lateClaim.success && lateClaim.nothingSent,
        prefix + "Bob never publishes t once Alice's refund is within the claim runway");
  check(!alice.refund(swap.alice).success, prefix + "refund before the timeout refused");

  node.mine(static_cast<int>(swap.alice.ctrTimeoutBlock - node.tip()));
  const ChainClientResult refunded = alice.refund(swap.alice);
  check(refunded.success, prefix + "refund at the timeout (" + refunded.error + ")");
  if (!refunded.success) return;
  node.mine(1);
  UtxoUnspentOutput refundOutput;
  std::string error;
  check(lookupUnspentOutput(node.rpc(), {refunded.txId, 0}, false, refundOutput, error) ==
                UtxoOutputLookup::UNSPENT &&
            refundOutput.value < 30000000 && refundOutput.value > 29000000,
        prefix + "refund pays the lock value minus fee");
  check(alice.tryExtractClaimedSecret(swap.alice).empty(), prefix + "a refund reveals no secret");
  check(!alice.hasConfirmedClaim(swap.alice), prefix + "a refund is not a claim");
}

struct PointSwap {
  SwapParams alice{};
  SwapParams bob{};
  Crypto::SecretKey secret{};
};

static PointSwap newPointSwap(const Node& node, IChainClient& bob, uint64_t amount, uint64_t timeoutBlock) {
  PointSwap swap;
  Crypto::cross_dleq_generate_scalar(swap.secret);
  Crypto::SecpPubKey adaptorPoint;
  Crypto::secp_point_from_ed_secret(swap.secret, adaptorPoint);
  for (SwapParams* side : {&swap.alice, &swap.bob}) {
    side->pair = node.pair();
    side->ctrAmount = amount;
    side->ctrTimeoutBlock = timeoutBlock;
    side->requiredConfirmations = 6;
    side->lockType = SwapLockType::PTLC;
    side->secpPubHex = Crypto::secpPubKeyToHex(adaptorPoint);
  }
  swap.alice.role = SwapRole::ALICE;
  swap.alice.ctrPubKey = bob.getClaimPubKey();
  swap.bob.role = SwapRole::BOB;
  swap.bob.adaptorSecret = swap.secret;
  return swap;
}

static void deliverPointLockNotice(PointSwap& swap, const ChainClientResult& locked) {
  for (SwapParams* side : {&swap.alice, &swap.bob}) {
    side->ctrLockTxId = locked.txId;
    side->ctrLockVout = locked.outputIndex;
    side->chainState = locked.chainState;
  }
}

static void checkClaimReplacement(IChainClient& bob, const SwapParams& pending, const std::string& prefix) {
  const ChainClientResult replaced = bob.bumpClaim(pending);
  check(replaced.success && replaced.txId != pending.ctrClaimTxId && replaced.feeChildTxId.empty(),
        prefix + "a pending claim is replaced at the next pre-signed fee (" + replaced.error + ")");
}

// Litecoin Core refuses replacements, so Bob chains children onto his claim.
static void checkClaimFeeChildren(const Node& node, IChainClient& bob, const SwapParams& pending,
                                  const std::string& prefix) {
  const ChainClientResult first = bob.bumpClaim(pending);
  check(first.success && first.txId == pending.ctrClaimTxId && isTxidHex(first.feeChildTxId),
        prefix + "a child raises the pending claim's fee (" + first.error + ")");
  const ChainClientResult second = bob.bumpClaim(pending);
  check(second.success && second.txId == pending.ctrClaimTxId && isTxidHex(second.feeChildTxId) &&
            second.feeChildTxId != first.feeChildTxId,
        prefix + "a second child extends the chain (" + second.error + ")");
  if (!second.success) return;
  Common::JsonValue entry;
  std::string error;
  check(node.rpc()("getmempoolentry", "[\"" + second.feeChildTxId + "\"]", entry, error) && entry.isObject() &&
            entry.contains("ancestorcount") && entry("ancestorcount").getInteger() == 3,
        prefix + "the node holds the claim and both children as one package (" + error + ")");
}

static void testPointLockHappyPath(const Node& node, IChainClient& alice, IChainClient& bob) {
  const std::string prefix = node.chain + " point lock: ";
  check(alice.supportsPurePtlc() && bob.supportsPurePtlc(), prefix + "both full nodes offer point locks");
  PointSwap swap = newPointSwap(node, bob, 40000000, node.tip() + 400);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success && isPointLockState(locked.chainState), prefix + "Alice locks to T_secp (" + locked.error + ")");
  if (!locked.success) return;
  const ChainClientResult relocked = alice.lock(swap.alice);
  check(relocked.success && relocked.txId == locked.txId, prefix + "a repeated lock reuses the funding");
  deliverPointLockNotice(swap, locked);

  check(!bob.verifyLock(swap.bob).success, prefix + "unconfirmed lock refused");
  node.mine(6);
  const ChainClientResult verified = bob.verifyLock(swap.bob);
  check(verified.success, prefix + "Bob verifies the lock and Alice's pre-signature (" + verified.error + ")");
  check(!alice.verifyLock(swap.bob).success, prefix + "a lock not paying the verifier's key is refused");

  PointSwap other = newPointSwap(node, bob, 40000000, swap.bob.ctrTimeoutBlock);
  SwapParams otherPoint = swap.bob;
  otherPoint.secpPubHex = other.bob.secpPubHex;
  check(!bob.verifyLock(otherPoint).success, prefix + "a pre-signature to another point is refused");
  SwapParams tampered = swap.bob;
  PointLockState state;
  decodePointLockState(tampered.chainState, state);
  state.claimOffers.front().presig.sPrime[31] ^= 1;
  tampered.chainState = encodePointLockState(state);
  check(!bob.verifyLock(tampered).success, prefix + "a changed pre-signature is refused");
  SwapParams wrongSecret = swap.bob;
  wrongSecret.adaptorSecret = other.secret;
  const ChainClientResult wrongClaim = bob.claim(wrongSecret);
  check(!wrongClaim.success && wrongClaim.nothingSent, prefix + "a claim with another scalar is never broadcast");

  const ChainClientResult claimed = bob.claim(swap.bob);
  check(claimed.success, prefix + "Bob's claim is accepted by the node (" + claimed.error + ")");
  if (!claimed.success) return;
  SwapParams pending = swap.bob;
  pending.ctrClaimTxId = claimed.txId;
  if (node.chain == "ltc") checkClaimFeeChildren(node, bob, pending, prefix);
  else checkClaimReplacement(bob, pending, prefix);
  node.mine(1);
  check(alice.tryExtractClaimedSecret(swap.alice) == Common::podToHex(swap.secret),
        prefix + "Alice recovers t from Bob's signature without a claim txid");
  check(!alice.hasConfirmedClaim(swap.alice), prefix + "1 confirmation is not final");
  node.mine(5);
  check(alice.hasConfirmedClaim(swap.alice), prefix + "claim final at 6 confirmations");
}

static void testPointLockRefund(const Node& node, IChainClient& alice, IChainClient& bob) {
  const std::string prefix = node.chain + " point refund: ";
  PointSwap swap = newPointSwap(node, bob, 30000000, node.tip() + 20);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success, prefix + "Alice locks (" + locked.error + ")");
  if (!locked.success) return;
  deliverPointLockNotice(swap, locked);
  node.mine(1);
  check(!bob.verifyLock(swap.bob).success, prefix + "Bob refuses a lock without claim runway");
  const ChainClientResult lateClaim = bob.claim(swap.bob);
  check(!lateClaim.success && lateClaim.nothingSent, prefix + "Bob never publishes t inside the claim runway");
  check(!alice.refund(swap.alice).success, prefix + "refund before the timeout refused");
  node.mine(static_cast<int>(swap.alice.ctrTimeoutBlock - node.tip()));
  const ChainClientResult refunded = alice.refund(swap.alice);
  check(refunded.success, prefix + "refund through the timeout leaf (" + refunded.error + ")");
  if (!refunded.success) return;
  node.mine(1);
  check(alice.tryExtractClaimedSecret(swap.alice).empty(), prefix + "a refund reveals no secret");
  check(!alice.hasConfirmedClaim(swap.alice), prefix + "a refund is not a claim");
}

int main(int argc, char** argv) {
  if (argc != 6) {
    std::fprintf(stderr, "usage: %s <btc|ltc|bch> <host> <port> <user> <pass>\n", argv[0]);
    return 2;
  }
  Node node{argv[1], argv[2], static_cast<uint16_t>(std::stoi(argv[3])), argv[4], argv[5]};
  setUpWallet(node);
  std::unique_ptr<IChainClient> alice = node.client(regtestWif(std::vector<uint8_t>(32, 0x11)));
  std::unique_ptr<IChainClient> bob = node.client(regtestWif(std::vector<uint8_t>(32, 0x22)));
  check(alice->readinessError().empty(), node.chain + " Alice ready (" + alice->readinessError() + ")");
  check(alice->isReadyForNewSwap(), node.chain + " Alice admits new swaps");
  check(!bob->getClaimPubKey().empty(), node.chain + " Bob has a claim key");

  testHappyPathWithWithheldClaimTxid(node, *alice, *bob);
  testRefundAfterTimeout(node, *alice, *bob);
  if (node.chain != "bch") {
    testPointLockHappyPath(node, *alice, *bob);
    testPointLockRefund(node, *alice, *bob);
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
