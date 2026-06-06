// Copyright (c) 2017-2026 Fuego Developers
//
// Regression test for the Solana HTLC account-reference bug.
//
// The xfg_htlc program stores state at a PDA:
//   find_program_address([b"xfg_htlc", sender_pubkey, hash_lock]).
// claim()/verifyLock() must reference THAT account. The old daemon stored the
// lock-tx *signature* (64 bytes, base58 ~88 chars) as the reference; passing it
// to claim made base58Decode != 32 bytes and the claim tx failed to build, so
// the SOL leg could never complete.
//
// This pins the derivation that lock() now records as result.htlcAddress:
//   - non-empty, account-sized (<= 44 base58 chars, never signature-sized),
//   - deterministic,
//   - sensitive to sender and to hash_lock (real PDA seeds).

#include <cassert>
#include <iostream>
#include <string>

#include "SwapDaemon/Solana/SolRpcClient.h"
#include "SwapDaemon/SwapHashLock.h"
#include "crypto/crypto.h"

using namespace XfgSwap;

int main() {
  // Valid 32-byte base58 keys (no network used by deriveHtlcAddress).
  const std::string programId = "11111111111111111111111111111111";           // System program (32 zero bytes)
  const std::string senderA   = "So11111111111111111111111111111111111111112"; // wrapped-SOL mint (32 bytes)
  const std::string senderB   = "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA";  // token program (32 bytes)

  SolRpcClient client("127.0.0.1", 8899, programId);

  // Two distinct hashlocks from two secrets.
  Crypto::PublicKey T1, T2;
  Crypto::SecretKey s1, s2;
  Crypto::generate_keys(T1, s1);
  Crypto::generate_keys(T2, s2);
  const std::string hl1 = solHashLockHex(s1);
  const std::string hl2 = solHashLockHex(s2);

  const std::string pdaA1 = client.deriveHtlcAddress(senderA, hl1);

  // 1. Non-empty + account-sized (a base58 pubkey is <= 44 chars; a tx
  //    signature is ~87-88 chars — the value the old code wrongly stored).
  assert(!pdaA1.empty() && "HTLC PDA must derive");
  assert(pdaA1.size() <= 44 && "PDA must be account-sized, not signature-sized");
  std::cout << "  [1] deriveHtlcAddress non-empty, account-sized (" << pdaA1.size() << " chars)\n";

  // 2. Deterministic.
  assert(client.deriveHtlcAddress(senderA, hl1) == pdaA1 && "PDA must be deterministic");
  std::cout << "  [2] deterministic\n";

  // 3. Sensitive to hash_lock.
  assert(client.deriveHtlcAddress(senderA, hl2) != pdaA1 && "different hash_lock -> different PDA");
  std::cout << "  [3] hash_lock-sensitive\n";

  // 4. Sensitive to sender.
  assert(client.deriveHtlcAddress(senderB, hl1) != pdaA1 && "different sender -> different PDA");
  std::cout << "  [4] sender-sensitive\n";

  // 5. Rejects malformed inputs.
  assert(client.deriveHtlcAddress("not-base58!", hl1).empty() && "bad sender -> empty");
  assert(client.deriveHtlcAddress(senderA, "deadbeef").empty() && "short hashlock -> empty");
  std::cout << "  [5] rejects malformed sender / hashlock\n";

  std::cout << "=== test_sol_htlc_address: 5/5 checks passed ===\n";
  return 0;
}
