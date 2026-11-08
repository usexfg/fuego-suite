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

// EVM transport gate (remediation plan R9): the real EthChainClient against an
// anvil node with HashedTimelock deployed at its CREATE2 address.
//
//   test_evm_anvil_gate <rpc url> <registry address>
//
// Keys are anvil's published development accounts 0 (Alice) and 1 (Bob).

#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include "SwapDaemon/Crypto/Secp256k1Signer.h"
#include "SwapDaemon/Ethereum/EthChainClient.h"
#include "SwapDaemon/Ethereum/EthRpcClient.h"
#include "SwapDaemon/SwapHashLock.h"
#include "SwapDaemon/SwapTimelock.h"
#include "crypto/crypto.h"
#include "crypto/cross_dleq.h"
#include "crypto/hash.h"
#include "crypto/secp_adaptor.h"
#include <HTTP/httplib.h>

#include <openssl/rand.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  std::printf("%s  %s\n", condition ? "ok  " : "FAIL", label.c_str());
  if (!condition) ++g_failures;
}

static const char* const ALICE_KEY = "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
static const char* const BOB_KEY = "59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d";
static const uint64_t ANVIL_CHAIN_ID = 31337;
static const char* const ONE_ETHER = "1000000000000000000";

static std::string lowercase(std::string text) {
  for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

static std::string addressOf(const std::string& keyHex) {
  std::array<uint8_t, 32> key{};
  std::vector<uint8_t> raw;
  Common::fromHex(keyHex, raw);
  std::copy(raw.begin(), raw.end(), key.begin());
  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  const std::vector<uint8_t> pub = signer.derivePublicKey(key);
  Crypto::Hash digest;
  Crypto::cn_fast_hash(pub.data() + 1, 64, digest);
  return "0x" + Common::toHex(reinterpret_cast<const uint8_t*>(&digest) + 12, 20);
}

class AnvilNode {
public:
  explicit AnvilNode(const std::string& url) : m_client(url) {}

  Common::JsonValue call(const std::string& method, const std::string& params) {
    const std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + method + "\",\"params\":" + params + "}";
    auto response = m_client.Post("/", body, "application/json");
    if (!response || response->status != 200) return Common::JsonValue();
    return Common::JsonValue::fromString(response->body)("result");
  }
  void mine(int blocks) { call("anvil_mine", "[\"0x" + hex(blocks) + "\"]"); }
  uint64_t tip() {
    const Common::JsonValue result = call("eth_blockNumber", "[]");
    return result.isString() ? std::stoull(result.getString(), nullptr, 16) : 0;
  }

private:
  static std::string hex(uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%llx", static_cast<unsigned long long>(value));
    return buffer;
  }
  httplib::Client m_client;
};

static std::unique_ptr<EthChainClient> clientFor(const std::string& url, const std::string& registry,
                                                 const std::string& keyHex,
                                                 const std::string& pointRegistry = CANONICAL_PTLC_REGISTRY_ADDRESS) {
  const std::string address = addressOf(keyHex);
  auto rpc = std::make_unique<EthRpcClient>(url, 0, keyHex, address, ANVIL_CHAIN_ID);
  rpc->setHtlcRegistry(registry);
  rpc->setPtlcRegistry(pointRegistry);
  return std::make_unique<EthChainClient>(std::move(rpc), address, "ETH");
}

struct Swap {
  SwapParams alice{};
  SwapParams bob{};
  Crypto::SecretKey secret{};
};

static Swap newSwap(const std::string& bobAddress, uint64_t timeoutBlock) {
  Swap swap;
  RAND_bytes(reinterpret_cast<unsigned char*>(&swap.secret), sizeof(swap.secret));
  Crypto::Hash hashLock;
  Common::podFromHex(ethHashLockHex(swap.secret), hashLock);
  for (SwapParams* side : {&swap.alice, &swap.bob}) {
    side->pair = SwapPair::ETH;
    side->ctrAmount = AtomicAmount(ONE_ETHER);
    side->ctrAddress = bobAddress;
    side->ctrTimeoutBlock = timeoutBlock;
    side->requiredConfirmations = 6;
    side->lockType = SwapLockType::PTLC_HTLC_BRIDGE;
    side->hashLock = hashLock;
  }
  swap.alice.role = SwapRole::ALICE;
  swap.bob.role = SwapRole::BOB;
  swap.bob.adaptorSecret = swap.secret;
  return swap;
}

static void testHappyPath(AnvilNode& node, EthChainClient& alice, EthChainClient& bob, const std::string& bobAddress) {
  uint64_t window = 0;
  minimumCounterpartyWindowBlocks(SwapPair::ETH, 6, window);
  Swap swap = newSwap(bobAddress, node.tip() + window + 100);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success, "happy path: Alice locks one ether (" + locked.error + ")");
  if (!locked.success) return;
  swap.alice.ctrLockTxId = swap.bob.ctrLockTxId = locked.txId;

  const ChainClientResult verified = bob.verifyLock(swap.bob);
  check(verified.success, "happy path: Bob verifies the lock (" + verified.error + ")");
  SwapParams otherHash = swap.bob;
  otherHash.adaptorSecret.data[0] ^= 0x01;
  check(!bob.verifyLock(otherHash).success, "happy path: a lock for another hash is refused");
  SwapParams otherRecipient = swap.bob;
  otherRecipient.ctrAddress = addressOf(ALICE_KEY);
  check(!bob.verifyLock(otherRecipient).success, "happy path: a lock paying another address is refused");
  SwapParams moreThanLocked = swap.bob;
  moreThanLocked.ctrAmount = AtomicAmount("1000000000000000001");
  check(!bob.verifyLock(moreThanLocked).success, "happy path: a lock one wei short is refused");

  check(bob.claimDeadlineError(swap.bob).empty(), "happy path: the claim has its runway");
  const ChainClientResult claimed = bob.claim(swap.bob);
  check(claimed.success, "happy path: Bob claims (" + claimed.error + ")");
  if (!claimed.success) return;
  check(alice.tryExtractClaimedSecret(swap.alice).empty(), "happy path: a claim under six blocks deep is not final");
  node.mine(6);
  check(alice.tryExtractClaimedSecret(swap.alice) == Common::podToHex(swap.secret),
        "happy path: Alice reads t from the registry without the claim txid");
  check(alice.hasConfirmedClaim(swap.alice), "happy path: the claim is final at six blocks");
  check(!bob.claimDeadlineError(swap.bob).empty(), "happy path: a claimed lock is not claimed again");
}

static void testRefund(AnvilNode& node, EthChainClient& alice, EthChainClient& bob, const std::string& bobAddress) {
  Swap swap = newSwap(bobAddress, node.tip() + 10);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success, "refund: Alice locks with a short timeout (" + locked.error + ")");
  if (!locked.success) return;
  swap.alice.ctrLockTxId = swap.bob.ctrLockTxId = locked.txId;
  check(!bob.verifyLock(swap.bob).success, "refund: Bob refuses a lock without claim runway");
  check(!bob.claimDeadlineError(swap.bob).empty(), "refund: Bob is told not to publish t");
  check(!alice.refund(swap.alice).success, "refund: refund before the timeout refused");
  node.mine(12);
  const ChainClientResult refunded = alice.refund(swap.alice);
  check(refunded.success, "refund: refund after the timeout (" + refunded.error + ")");
  node.mine(6);
  check(alice.tryExtractClaimedSecret(swap.alice).empty(), "refund: a refund reveals no secret");
}

struct PointSwap {
  SwapParams alice{};
  SwapParams bob{};
  Crypto::SecretKey secret{};
};

static PointSwap newPointSwap(const std::string& bobAddress, uint64_t timeoutBlock) {
  PointSwap swap;
  Crypto::cross_dleq_generate_scalar(swap.secret);
  Crypto::SecpPubKey secpPoint;
  Crypto::secp_point_from_ed_secret(swap.secret, secpPoint);
  for (SwapParams* side : {&swap.alice, &swap.bob}) {
    side->pair = SwapPair::ETH;
    side->ctrAmount = AtomicAmount(ONE_ETHER);
    side->ctrAddress = bobAddress;
    side->ctrTimeoutBlock = timeoutBlock;
    side->requiredConfirmations = 6;
    side->lockType = SwapLockType::PTLC;
    side->secpPubHex = Crypto::secpPubKeyToHex(secpPoint);
  }
  swap.alice.role = SwapRole::ALICE;
  swap.bob.role = SwapRole::BOB;
  swap.bob.adaptorSecret = swap.secret;
  return swap;
}

static void testPointLockHappyPath(AnvilNode& node, EthChainClient& alice, EthChainClient& bob,
                                   const std::string& bobAddress) {
  uint64_t window = 0;
  minimumCounterpartyWindowBlocks(SwapPair::ETH, 6, window);
  PointSwap swap = newPointSwap(bobAddress, node.tip() + window + 100);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success, "point lock: Alice locks one ether to T_secp (" + locked.error + ")");
  if (!locked.success) return;
  swap.alice.ctrLockTxId = swap.bob.ctrLockTxId = locked.txId;
  const ChainClientResult verified = bob.verifyLock(swap.bob);
  check(verified.success, "point lock: Bob verifies the lock (" + verified.error + ")");
  PointSwap other = newPointSwap(bobAddress, swap.bob.ctrTimeoutBlock);
  SwapParams otherPoint = swap.bob;
  otherPoint.secpPubHex = other.bob.secpPubHex;
  check(!bob.verifyLock(otherPoint).success, "point lock: a lock to another point is refused");
  SwapParams wrongSecret = swap.bob;
  wrongSecret.adaptorSecret = other.secret;
  check(!bob.claim(wrongSecret).success, "point lock: a claim with another scalar is refused on chain");
  check(bob.claimDeadlineError(swap.bob).empty(), "point lock: the claim has its runway");
  const ChainClientResult claimed = bob.claim(swap.bob);
  check(claimed.success, "point lock: Bob claims with t (" + claimed.error + ")");
  if (!claimed.success) return;
  check(alice.tryExtractClaimedSecret(swap.alice).empty(), "point lock: a claim under six blocks deep is not final");
  node.mine(6);
  const std::string extracted = alice.tryExtractClaimedSecret(swap.alice);
  Crypto::SecretKey recovered;
  Crypto::PublicKey recoveredPoint, expectedPoint;
  check(extracted == Common::podToHex(swap.secret) && Common::podFromHex(extracted, recovered) &&
            Crypto::secret_key_to_public_key(recovered, recoveredPoint) &&
            Crypto::secret_key_to_public_key(swap.secret, expectedPoint) && recoveredPoint == expectedPoint,
        "point lock: Alice reads the t that opens the XFG adaptor point, without the claim txid");
  check(!bob.claimDeadlineError(swap.bob).empty(), "point lock: a claimed lock is not claimed again");
}

static void testPointLockRefund(AnvilNode& node, EthChainClient& alice, EthChainClient& bob,
                                const std::string& bobAddress) {
  PointSwap swap = newPointSwap(bobAddress, node.tip() + 10);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success, "point refund: Alice locks with a short timeout (" + locked.error + ")");
  if (!locked.success) return;
  swap.alice.ctrLockTxId = swap.bob.ctrLockTxId = locked.txId;
  check(!bob.verifyLock(swap.bob).success, "point refund: Bob refuses a lock without claim runway");
  check(!alice.refund(swap.alice).success, "point refund: refund before the timeout refused");
  node.mine(12);
  const ChainClientResult refunded = alice.refund(swap.alice);
  check(refunded.success, "point refund: refund after the timeout (" + refunded.error + ")");
  node.mine(6);
  check(alice.tryExtractClaimedSecret(swap.alice).empty(), "point refund: a refund reveals no secret");
}

static void testPointLockSupport(const std::string& url, const std::string& registry, EthChainClient& alice) {
  check(alice.pointLockSupport() == IChainClient::PointLockSupport::AVAILABLE,
        "point locks are available with the pinned PointTimelock deployed");
  auto wrongCode = clientFor(url, registry, ALICE_KEY, registry);
  check(wrongCode->pointLockSupport() == IChainClient::PointLockSupport::UNKNOWN &&
            !wrongCode->readinessError().empty(),
        "a point registry holding other code blocks swaps instead of falling back to a hash lock");
  auto noCode = clientFor(url, registry, ALICE_KEY, "0x00000000000000000000000000000000000000aa");
  check(noCode->pointLockSupport() == IChainClient::PointLockSupport::NONE,
        "a chain without the PointTimelock offers no point lock");
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <rpc url> <registry address>\n", argv[0]);
    return 2;
  }
  const std::string url = argv[1];
  const std::string registry = argv[2];
  AnvilNode node(url);
  const Common::JsonValue code = node.call("eth_getCode", "[\"" + registry + "\",\"latest\"]");
  check(code.isString() && code.getString().size() > 2, "HashedTimelock is deployed at " + registry);
  auto alice = clientFor(url, registry, ALICE_KEY);
  auto bob = clientFor(url, registry, BOB_KEY);
  const std::string bobAddress = addressOf(BOB_KEY);
  const Common::JsonValue accounts = node.call("eth_accounts", "[]");
  check(accounts.isArray() && accounts.size() > 1 && lowercase(accounts[1].getString()) == bobAddress,
        "the derived Bob address is anvil account 1");
  const Common::JsonValue pointCode = node.call("eth_getCode", "[\"" + std::string(CANONICAL_PTLC_REGISTRY_ADDRESS) + "\",\"latest\"]");
  check(pointCode.isString() && pointCode.getString().size() > 2,
        std::string("PointTimelock is deployed at ") + CANONICAL_PTLC_REGISTRY_ADDRESS);
  testHappyPath(node, *alice, *bob, bobAddress);
  testRefund(node, *alice, *bob, bobAddress);
  testPointLockSupport(url, registry, *alice);
  testPointLockHappyPath(node, *alice, *bob, bobAddress);
  testPointLockRefund(node, *alice, *bob, bobAddress);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
