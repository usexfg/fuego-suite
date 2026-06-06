// Copyright (c) 2017-2026 Fuego Developers
//
// MANUAL on-chain e2e for the SOL HTLC leg (NOT a CI unit test — requires a
// running solana-test-validator with xfg_htlc deployed + funded keypairs).
//
// Drives the daemon's real SolRpcClient through lock -> claim against the
// deployed program, proving the three SOL fixes end-to-end:
//   1. hashlock = keccak256(secret)        (SwapHashLock::solHashLockHex)
//   2. lock reference = HTLC PDA           (SolTxResult.htlcAddress)
//   3. claim transfers via signed system CPI (program + buildClaimTx)
//
// Usage:
//   test_sol_e2e <senderSecB58> <recipSecB58> <recipPubB58> <programId> \
//                <amountLamports> <timeoutSlot>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "SwapDaemon/Solana/SolRpcClient.h"
#include "SwapDaemon/SwapHashLock.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"

using namespace XfgSwap;

int main(int argc, char** argv) {
  if (argc < 7) {
    std::cerr << "usage: test_sol_e2e <senderSec> <recipSec> <recipPub> "
                 "<programId> <amountLamports> <timeoutSlot>\n";
    return 2;
  }
  const std::string senderSec = argv[1];
  const std::string recipSec  = argv[2];
  const std::string recipPub  = argv[3];
  const std::string progId    = argv[4];
  const uint64_t amount  = std::strtoull(argv[5], nullptr, 10);
  const uint64_t timeout = std::strtoull(argv[6], nullptr, 10);

  SolRpcClient sol("127.0.0.1", 8899, progId);

  // Adaptor secret t + the FIXED hashlock keccak256(t).
  Crypto::PublicKey T;
  Crypto::SecretKey secret;
  Crypto::generate_keys(T, secret);
  const std::string hashLock  = solHashLockHex(secret);
  const std::string secretHex = Common::podToHex(secret);
  std::cout << "adaptor secret t = " << secretHex << "\n"
            << "hashlock keccak256(t) = " << hashLock << "\n"
            << "adaptor point T      = " << Common::podToHex(T) << "  (the OLD buggy hashlock)\n\n";

  // ── LOCK ──
  SolTxResult lr;
  bool lok = sol.lock(senderSec, recipPub, hashLock, timeout, amount, lr);
  std::cout << "LOCK   ok=" << lok << " confirmed=" << lr.confirmed
            << "\n  htlcPDA=" << lr.htlcAddress
            << "\n  sig=" << lr.signature
            << "\n  err=" << lr.error << "\n\n";
  if (!lok || lr.htlcAddress.empty()) { std::cerr << "LOCK FAILED\n"; return 1; }

  // ── CLAIM (reveal t) ──
  SolTxResult cr;
  bool cok = sol.claim(recipSec, lr.htlcAddress, secretHex, cr);
  std::cout << "CLAIM  ok=" << cok << " confirmed=" << cr.confirmed
            << "\n  sig=" << cr.signature
            << "\n  err=" << cr.error << "\n\n";
  if (!cok) { std::cerr << "CLAIM FAILED\n"; return 1; }

  std::cout << "=== SOL HTLC lock -> claim COMPLETED on-chain ===\n";
  return 0;
}
