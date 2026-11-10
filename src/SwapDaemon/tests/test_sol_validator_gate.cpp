// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
// SOL point-lock gate (CROSS_CURVE_PLAN X4): drives SolChainClient against
// solana-test-validator running the xfg_htlc program at its declare_id.
//
// usage: test_sol_validator_gate <host> <port> <program id> <alice keypair.json> <bob keypair.json>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include "SwapDaemon/Crypto/Base58Std.h"
#include "SwapDaemon/Solana/SolChainClient.h"
#include "SwapDaemon/SwapTimelock.h"
#include "crypto/cross_dleq.h"

using namespace XfgSwap;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  std::printf("%s  %s\n", condition ? "ok  " : "FAIL", label.c_str());
  if (!condition) ++g_failures;
}

constexpr uint64_t ONE_SOL = 1000000000;
constexpr int FINALITY_WAIT_SECONDS = 90;

struct Validator {
  std::string host;
  uint16_t port = 0;
  std::string programId;

  std::unique_ptr<SolChainClient> client(const std::string& keypairBase58, const std::string& program) const {
    return std::unique_ptr<SolChainClient>(
        new SolChainClient(std::unique_ptr<SolRpcClient>(new SolRpcClient(host, port, program)), keypairBase58));
  }

  uint64_t slot() const {
    SolRpcClient rpc(host, port, programId);
    uint64_t current = 0;
    rpc.getSlot(current);
    return current;
  }

  void waitForSlot(uint64_t target) const {
    while (slot() < target) std::this_thread::sleep_for(std::chrono::milliseconds(400));
  }
};

static bool waitUntil(const std::function<bool()>& ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(FINALITY_WAIT_SECONDS);
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) return true;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return false;
}

static std::string keypairFromFile(const std::string& path) {
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  const Common::JsonValue bytes = Common::JsonValue::fromString(text.str());
  std::vector<uint8_t> keypair;
  for (size_t i = 0; i < bytes.size(); ++i) keypair.push_back(static_cast<uint8_t>(bytes[i].getInteger()));
  return Base58Std::encode(keypair);
}

struct PointSwap {
  SwapParams alice{};
  SwapParams bob{};
  Crypto::SecretKey secret{};
};

static PointSwap newPointSwap(const std::string& bobAddress, uint64_t timeoutSlot) {
  PointSwap swap;
  Crypto::cross_dleq_generate_scalar(swap.secret);
  Crypto::PublicKey adaptorPoint;
  Crypto::secret_key_to_public_key(swap.secret, adaptorPoint);
  for (SwapParams* side : {&swap.alice, &swap.bob}) {
    side->pair = SwapPair::SOL;
    side->ctrAmount = AtomicAmount(ONE_SOL);
    side->ctrAddress = bobAddress;
    side->ctrTimeoutBlock = timeoutSlot;
    side->requiredConfirmations = 1;
    side->lockType = SwapLockType::PTLC;
    side->adaptorPoint = adaptorPoint;
  }
  swap.alice.role = SwapRole::ALICE;
  swap.bob.role = SwapRole::BOB;
  swap.bob.adaptorSecret = swap.secret;
  return swap;
}

static void deliverLockNotice(PointSwap& swap, const ChainClientResult& locked) {
  for (SwapParams* side : {&swap.alice, &swap.bob}) {
    side->ctrLockTxId = locked.txId;
    side->chainState = locked.chainState;
  }
}

static bool escrowReadable(SolChainClient& client, const SwapParams& params) {
  return client.claimDeadlineError(params).find("cannot read") == std::string::npos;
}

static void testPointLockSupport(const Validator& validator, SolChainClient& alice, const std::string& aliceKeypair) {
  check(alice.pointLockSupport() == IChainClient::PointLockSupport::AVAILABLE,
        "point locks are available with the xfg_htlc program loaded");
  const std::string nothingDeployed = Base58Std::encode(std::vector<uint8_t>(32, 0x07));
  check(validator.client(aliceKeypair, nothingDeployed)->pointLockSupport() == IChainClient::PointLockSupport::NONE,
        "a program id with nothing deployed offers no point lock");
}

static void testPointLockHappyPath(const Validator& validator, SolChainClient& alice, SolChainClient& bob,
                                   const std::string& aliceAddress, const std::string& bobAddress) {
  uint64_t window = 0;
  minimumCounterpartyWindowBlocks(SwapPair::SOL, 1, window);
  PointSwap swap = newPointSwap(bobAddress, validator.slot() + window + 600);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success && locked.chainState.rfind("ptlc:", 0) == 0,
        "point lock: Alice locks one SOL to the XFG adaptor point (" + locked.error + ")");
  if (!locked.success) return;
  deliverLockNotice(swap, locked);
  check(waitUntil([&] { return bob.verifyLock(swap.bob).success; }),
        "point lock: Bob verifies the finalized escrow (" + bob.verifyLock(swap.bob).error + ")");

  PointSwap other = newPointSwap(bobAddress, swap.bob.ctrTimeoutBlock);
  SwapParams otherPoint = swap.bob;
  otherPoint.adaptorPoint = other.bob.adaptorPoint;
  check(!bob.verifyLock(otherPoint).success, "point lock: an escrow to another point is refused");
  SwapParams otherRecipient = swap.bob;
  otherRecipient.ctrAddress = aliceAddress;
  check(!bob.verifyLock(otherRecipient).success, "point lock: an escrow paying another address is refused");
  SwapParams wrongSecret = swap.bob;
  wrongSecret.adaptorSecret = other.secret;
  const ChainClientResult wrongClaim = bob.claim(wrongSecret);
  check(!wrongClaim.success && wrongClaim.nothingSent, "point lock: a claim with another scalar is never sent");
  check(bob.claimDeadlineError(swap.bob).empty(), "point lock: the claim has its runway");

  const ChainClientResult claimed = bob.claim(swap.bob);
  check(claimed.success, "point lock: Bob claims by proving t (" + claimed.error + ")");
  if (!claimed.success) return;
  std::string extracted;
  check(waitUntil([&] { return !(extracted = alice.tryExtractClaimedSecret(swap.alice)).empty(); }) &&
            extracted == Common::podToHex(swap.secret),
        "point lock: Alice reads the t that opens the XFG adaptor point from the finalized escrow");
  check(alice.hasConfirmedClaim(swap.alice), "point lock: the claim is final");
  check(!bob.claimDeadlineError(swap.bob).empty(), "point lock: a settled escrow is not claimed again");
}

static void testPointLockRefund(const Validator& validator, SolChainClient& alice, SolChainClient& bob,
                                const std::string& bobAddress) {
  PointSwap swap = newPointSwap(bobAddress, validator.slot() + 60);
  const ChainClientResult locked = alice.lock(swap.alice);
  check(locked.success, "point refund: Alice locks with a short timeout (" + locked.error + ")");
  if (!locked.success) return;
  deliverLockNotice(swap, locked);
  check(waitUntil([&] { return escrowReadable(bob, swap.bob); }), "point refund: the escrow is finalized");
  check(!bob.verifyLock(swap.bob).success, "point refund: Bob refuses an escrow without claim runway");
  const ChainClientResult lateClaim = bob.claim(swap.bob);
  check(!lateClaim.success && lateClaim.nothingSent, "point refund: Bob never publishes t inside the claim runway");
  check(!alice.refund(swap.alice).success, "point refund: refund before the timeout refused");
  validator.waitForSlot(swap.alice.ctrTimeoutBlock + 2);
  const ChainClientResult refunded = alice.refund(swap.alice);
  check(refunded.success, "point refund: refund after the timeout (" + refunded.error + ")");
  check(waitUntil([&] { return bob.claimDeadlineError(swap.bob).find("settled") != std::string::npos; }),
        "point refund: the refund is final");
  check(alice.tryExtractClaimedSecret(swap.alice).empty(), "point refund: a refund reveals no secret");
}

int main(int argc, char** argv) {
  if (argc != 6) {
    std::fprintf(stderr, "usage: %s <host> <port> <program id> <alice keypair.json> <bob keypair.json>\n", argv[0]);
    return 2;
  }
  const Validator validator{argv[1], static_cast<uint16_t>(std::stoi(argv[2])), argv[3]};
  const std::string aliceKeypair = keypairFromFile(argv[4]);
  const std::string bobKeypair = keypairFromFile(argv[5]);
  auto alice = validator.client(aliceKeypair, validator.programId);
  auto bob = validator.client(bobKeypair, validator.programId);
  const std::string aliceAddress = alice->getReceiveAddress();
  const std::string bobAddress = bob->getReceiveAddress();
  check(!aliceAddress.empty() && !bobAddress.empty(), "both keypairs load");

  testPointLockSupport(validator, *alice, aliceKeypair);
  testPointLockHappyPath(validator, *alice, *bob, aliceAddress, bobAddress);
  testPointLockRefund(validator, *alice, *bob, bobAddress);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
