// Copyright (c) 2017-2026 Fuego Developers
//
// Unit tests for the XMR RPC correctness fixes (spec 2026-06-06 safe slice):
//   - verifyLock requires UNLOCKED balance (not locked).
//   - sweepSharedAddress syncs (poll get_height) BEFORE sweep_all, uses a
//     per-swap wallet name + restore_height, and never sweeps an unsynced wallet.
// Driven by TestMoneroWalletRpc (canned responses + call-sequence recorder).

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "TestMoneroWalletRpc.h"

using namespace XfgSwap;

static bool before(const std::vector<std::string>& seq,
                   const std::string& a, const std::string& b) {
  auto ia = std::find(seq.begin(), seq.end(), a);
  auto ib = std::find(seq.begin(), seq.end(), b);
  return ia != seq.end() && ib != seq.end() && (ia - seq.begin()) < (ib - seq.begin());
}

int main() {
  // Realistic 64-char hex keys (sweepSharedAddress now validates the keys and
  // derives the from-keys wallet address from them before any RPC).
  const std::string SK(64, 'a');
  const std::string VK(64, 'b');

  // 1. verifyLock: unlocked-only.
  {
    TestMoneroWalletRpc w;
    w.queue("get_balance", "{\"result\":{\"balance\":1000000,\"unlocked_balance\":0}}");
    assert(w.verifyLock("addr", 500000) == false && "locked-only must NOT verify");

    TestMoneroWalletRpc w2;
    w2.queue("get_balance", "{\"result\":{\"balance\":1000000,\"unlocked_balance\":1000000}}");
    assert(w2.verifyLock("addr", 500000) == true && "unlocked covering amount verifies");
    std::cout << "  [1] verifyLock unlocked-only OK\n";
  }

  // 2. sweep: generate -> poll get_height until synced -> sweep_all; per-swap
  //    wallet name + restore_height in generate_from_keys params.
  {
    TestMoneroWalletRpc w;
    w.queue("generate_from_keys", "{\"result\":{}}");
    w.queue("get_height", "{\"result\":{\"height\":5}}");    // < target
    w.queue("get_height", "{\"result\":{\"height\":10}}");   // == target -> synced
    w.queue("sweep_all", "{\"result\":{\"tx_hash_list\":[\"deadbeef\"],\"fee_list\":[100]}}");

    MoneroTransferResult r;
    bool ok = w.sweepSharedAddress(SK, VK, "dest", r,
                                   /*walletName*/ "swap123",
                                   /*restoreHeight*/ 7,
                                   /*targetHeight*/ 10);
    assert(ok && "sweep should succeed once synced");
    assert(r.txHash == "deadbeef");

    auto seq = w.methodSeq();
    assert(before(seq, "generate_from_keys", "get_height") && "generate before sync poll");
    assert(before(seq, "get_height", "sweep_all") && "must poll get_height before sweep");
    assert(before(seq, "generate_from_keys", "sweep_all"));

    const std::string gen = w.paramsFor("generate_from_keys");
    assert(gen.find("swap_sweep_swap123") != std::string::npos && "per-swap wallet name");
    assert(gen.find("\"restore_height\":7") != std::string::npos && "restore height passed");
    std::cout << "  [2] sweep: generate -> get_height(sync) -> sweep_all; name+restore OK\n";
  }

  // 3. never sweeps an unsynced wallet (height stays below target).
  {
    TestMoneroWalletRpc w;
    w.queue("generate_from_keys", "{\"result\":{}}");
    w.defaultResp = "{\"result\":{\"height\":5}}";  // every get_height < target 10
    MoneroTransferResult r;
    bool ok = w.sweepSharedAddress(SK, VK, "dest", r, "s", 0, /*targetHeight*/ 10);
    assert(!ok && "must NOT sweep when wallet never reaches target height");
    auto seq = w.methodSeq();
    assert(std::find(seq.begin(), seq.end(), "sweep_all") == seq.end() &&
           "no sweep_all when unsynced");
    std::cout << "  [3] never sweeps an unsynced wallet OK\n";
  }

  std::cout << "=== test_xmr_sweep_sequence: passed ===\n";
  return 0;
}
