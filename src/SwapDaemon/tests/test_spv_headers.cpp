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

// SpvHeader parse + PoW verification using real BCH block data.
// SpvHeaderStore tests with synthetic regtest-difficulty headers.

#include <cassert>
#include <string>
#include <vector>
#include <iostream>
#include "SwapDaemon/Spv/SpvHeader.h"
#include "SwapDaemon/Spv/SpvHeaderStore.h"
#include "../BitcoinCash/HtlcScript.h"

using namespace XfgSwap;

// =============================================================================
// Helper: build a synthetic header with regtest difficulty (bits = 0x207fffff)
// =============================================================================
//
// bits = 0x207fffff => target is 0x7fffff * 2^208, which is enormous.
// Any hash will pass PoW with this target.

// Build a synthetic header with bits = 0x207fffff (regtest difficulty).
// Iterates nonces starting from `nonce` until one passes PoW.
// With this target ~50% of hashes pass, so this is fast.
static SpvHeader makeHeader(uint32_t version,
                            const std::vector<uint8_t>& prevHash,
                            const std::vector<uint8_t>& merkleRoot,
                            uint32_t time,
                            uint32_t nonce) {
  SpvHeader h;
  h.version = version;
  h.prevHash = prevHash;
  h.merkleRoot = merkleRoot;
  h.time = time;
  h.bits = 0x207fffff;
  h.nonce = nonce;
  while (!h.meetsPoW()) {
    ++h.nonce;
  }
  return h;
}

static std::vector<uint8_t> zeroHash() {
  return std::vector<uint8_t>(32, 0);
}

static std::vector<uint8_t> makeMerkle(uint8_t fill) {
  return std::vector<uint8_t>(32, fill);
}

// =============================================================================
// Existing SpvHeader tests (parse + PoW verification using real BCH block data)
// =============================================================================

static void test_header_parse() {
  // Real BCH block 586 header (80 bytes, hex)
  std::string headerHex =
      "01000000"
      "38babc9586a5fcd60713573494f4377e7c401c33aa24729a4f6cff46"
      "000000004d5969c0d10dcce60868fee4d4de80ba5ef38abaeed8a75daa63e48c"
      "963d7b19"
      "50476f49"
      "ffff001d"
      "2d979137";

  std::vector<uint8_t> raw = BchHtlcScript::hexToBytes(headerHex);
  assert(raw.size() == 80 && "Header must be exactly 80 bytes");

  SpvHeader h = SpvHeader::parse(raw);

  assert(h.hashDisplay() == "000000000d0d23516c5efd3af4eb951603bb30b2c93884b522a318b30e918ee7");
  assert(h.prevHashDisplay() == "0000000046ff6c4f9a7224aa331c407c7e37f49434571307d6fca58695bcba38");
  assert(h.merkleRootDisplay() == "197b3d968ce463aa5da7d8eeba8af35eba80ded4e4fe6808e6cc0dd1c069594d");
  assert(h.meetsPoW());

  assert(h.version == 1);
  assert(h.time == 1232029520);
  assert(h.bits == 0x1d00ffff);
  assert(h.nonce == 932288301);

  // Corrupt the nonce -> PoW should fail
  SpvHeader bad = h;
  bad.nonce = 0xdeadbeef;
  assert(!bad.meetsPoW());

  // Verify serialize round-trip
  std::vector<uint8_t> re = h.serialize();
  assert(re == raw);

  // Verify work() is non-zero
  assert(h.work() > 0);

  // Verify nBitsToTargetBE produces correct target for bits = 0x1d00ffff
  std::vector<uint8_t> target = SpvHeader::nBitsToTargetBE(0x1d00ffff);
  assert(target.size() == 32);
  assert(target[0] == 0x00);
  assert(target[1] == 0x00);
  assert(target[2] == 0x00);
  assert(target[3] == 0x00);
  assert(target[4] == 0xff);
  assert(target[5] == 0xff);
  for (size_t i = 6; i < 32; ++i) {
    assert(target[i] == 0x00);
  }

  std::cout << "All SpvHeader parse/PoW tests passed." << std::endl;
}

// =============================================================================
// SpvHeaderStore tests
// =============================================================================

// Test 1: addHeader rejects a header whose prevHash != tip.hash()
static void test_store_rejects_bad_prevhash() {
  SpvHeaderStore store;

  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  assert(store.addHeader(genesis));

  // Create a header with WRONG prevHash
  SpvHeader bad = makeHeader(1, std::vector<uint8_t>(32, 0xff), makeMerkle(0xbb), 1000000001, 0);
  assert(!store.addHeader(bad));  // must reject

  std::cout << "Test 1 (reject bad prevHash) passed." << std::endl;
}

// Test 2: rejects headers below checkpoint height
static void test_store_rejects_below_checkpoint() {
  SpvHeaderStore store;
  store.anchor(100, "00000000000000000000000000000000000000000000000000000000000000aa");

  SpvHeader h = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);

  // Header at height 50 (below checkpoint) — should be rejected
  assert(!store.addHeaderAtHeight(h, 50));

  std::cout << "Test 2 (reject below checkpoint) passed." << std::endl;
}

// Test 3: chain selection follows greater cumulative work
static void test_store_chain_selection() {
  SpvHeaderStore store;

  // Build genesis
  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  assert(store.addHeaderAtHeight(genesis, 0));

  // Chain A: 2 headers extending genesis
  // Use genesis.hash() (internal LE) as prevHash for the child header
  SpvHeader a1 = makeHeader(1, genesis.hash(), makeMerkle(0xa1), 1000000001, 1);
  assert(store.addHeader(a1));

  SpvHeader a2 = makeHeader(1, a1.hash(), makeMerkle(0xa2), 1000000002, 2);
  assert(store.addHeader(a2));

  // Tip is now a2 at height 2
  uint64_t tipH;
  std::string tipHash;
  assert(store.bestTip(tipH, tipHash));
  assert(tipH == 2);
  assert(tipHash == a2.hashDisplay());

  // Fork B: 3 headers from genesis (more work via more blocks)
  SpvHeader b1 = makeHeader(1, genesis.hash(), makeMerkle(0xb1), 1000000001, 10);
  assert(store.addHeaderAtHeight(b1, 1));

  SpvHeader b2 = makeHeader(1, b1.hash(), makeMerkle(0xb2), 1000000002, 11);
  assert(store.addHeaderAtHeight(b2, 2));

  SpvHeader b3 = makeHeader(1, b2.hash(), makeMerkle(0xb3), 1000000003, 12);
  assert(store.addHeaderAtHeight(b3, 3));

  // Heavier branch (3 blocks from genesis) should win
  assert(store.bestTip(tipH, tipHash));
  assert(tipH == 3);
  assert(tipHash == b3.hashDisplay());

  std::cout << "Test 3 (chain selection) passed." << std::endl;
}

// Test 4: after a heavier branch arrives, depthOf recomputes against new tip
static void test_store_depth_after_reorg() {
  SpvHeaderStore store;

  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  assert(store.addHeaderAtHeight(genesis, 0));

  // Build a chain: genesis -> h1 -> h2
  SpvHeader h1 = makeHeader(1, genesis.hash(), makeMerkle(0x11), 1000000001, 1);
  assert(store.addHeader(h1));

  SpvHeader h2 = makeHeader(1, h1.hash(), makeMerkle(0x22), 1000000002, 2);
  assert(store.addHeader(h2));

  // Tip is at height 2. depthOf(0) = 3, depthOf(1) = 2, depthOf(2) = 1
  assert(store.depthOf(0) == 3);
  assert(store.depthOf(1) == 2);
  assert(store.depthOf(2) == 1);

  // Build a heavier fork: genesis -> f1 -> f2 -> f3 (3 headers)
  SpvHeader f1 = makeHeader(1, genesis.hash(), makeMerkle(0xf1), 1000000001, 10);
  assert(store.addHeaderAtHeight(f1, 1));

  SpvHeader f2 = makeHeader(1, f1.hash(), makeMerkle(0xf2), 1000000002, 11);
  assert(store.addHeaderAtHeight(f2, 2));

  SpvHeader f3 = makeHeader(1, f2.hash(), makeMerkle(0xf3), 1000000003, 12);
  assert(store.addHeaderAtHeight(f3, 3));

  // After reorg: tip is at height 3 (f3). depthOf(0) = 4
  assert(store.depthOf(0) == 4);

  // Verify best tip
  uint64_t tipH;
  std::string tipHash;
  assert(store.bestTip(tipH, tipHash));
  assert(tipH == 3);
  assert(tipHash == f3.hashDisplay());

  std::cout << "Test 4 (depth after reorg) passed." << std::endl;
}

// Test 5: merkleRootAt returns correct root
static void test_store_merkle_root_at() {
  SpvHeaderStore store;

  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  assert(store.addHeaderAtHeight(genesis, 0));

  std::vector<uint8_t> root;
  assert(store.merkleRootAt(0, root));
  assert(root == makeMerkle(0xaa));

  // Non-existent height
  assert(!store.merkleRootAt(99, root));

  std::cout << "Test 5 (merkleRootAt) passed." << std::endl;
}

// Test 6: bestTip returns false on empty store
static void test_store_empty_best_tip() {
  SpvHeaderStore store;
  uint64_t h;
  std::string hash;
  assert(!store.bestTip(h, hash));

  std::cout << "Test 6 (empty store bestTip) passed." << std::endl;
}

// Test 7: addHeaderAtHeight at checkpoint+1 must link to checkpoint hash
static void test_store_checkpoint_link() {
  SpvHeaderStore store;
  std::string cpHash = "00000000000000000000000000000000000000000000000000000000000000cc";
  store.anchor(10, cpHash);

  // Try to add header at height 11 with wrong prevHash
  SpvHeader wrong = makeHeader(1, std::vector<uint8_t>(32, 0xdd), makeMerkle(0xdd), 1000000000, 0);
  assert(!store.addHeaderAtHeight(wrong, 11));

  // Correct prevHash: must produce prevHashDisplay() == cpHash.
  // prevHashDisplay() reverses internal bytes, so we need the reversed form.
  std::vector<uint8_t> cpInternal = BchHtlcScript::hexToBytes(cpHash);
  std::reverse(cpInternal.begin(), cpInternal.end());
  SpvHeader correct = makeHeader(1, cpInternal, makeMerkle(0xee), 1000000000, 0);
  assert(correct.prevHashDisplay() == cpHash);
  assert(store.addHeaderAtHeight(correct, 11));

  std::cout << "Test 7 (checkpoint link validation) passed." << std::endl;
}

// Test 8: depthOf returns 0 for height > tip
static void test_store_depth_beyond_tip() {
  SpvHeaderStore store;

  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  assert(store.addHeaderAtHeight(genesis, 0));

  assert(store.depthOf(5) == 0);

  std::cout << "Test 8 (depth beyond tip) passed." << std::endl;
}

// Test 9: headerAtHeight returns correct header on best chain
static void test_store_header_at_height() {
  SpvHeaderStore store;

  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  assert(store.addHeaderAtHeight(genesis, 0));

  SpvHeader h1 = makeHeader(1, genesis.hash(), makeMerkle(0x11), 1000000001, 1);
  assert(store.addHeader(h1));

  SpvHeader got;
  assert(store.headerAtHeight(0, got));
  assert(got.hashDisplay() == genesis.hashDisplay());

  assert(store.headerAtHeight(1, got));
  assert(got.hashDisplay() == h1.hashDisplay());

  assert(!store.headerAtHeight(2, got));

  std::cout << "Test 9 (headerAtHeight) passed." << std::endl;
}

// Test 10: addHeader rejects when parent is not on best chain
static void test_store_rejects_non_best_chain_parent() {
  SpvHeaderStore store;

  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  assert(store.addHeaderAtHeight(genesis, 0));

  // Build chain A: genesis -> a1 -> a2 (main chain)
  SpvHeader a1 = makeHeader(1, genesis.hash(), makeMerkle(0xa1), 1000000001, 1);
  assert(store.addHeader(a1));

  SpvHeader a2 = makeHeader(1, a1.hash(), makeMerkle(0xa2), 1000000002, 2);
  assert(store.addHeader(a2));

  // Build fork B: genesis -> b1
  SpvHeader b1 = makeHeader(1, genesis.hash(), makeMerkle(0xb1), 1000000001, 10);
  assert(store.addHeaderAtHeight(b1, 1));

  // Now a2 is on the best chain. Try to add a header extending b1 via addHeader().
  // b1 is NOT on the best chain, so addHeader should reject.
  SpvHeader b2 = makeHeader(1, b1.hash(), makeMerkle(0xb2), 1000000002, 11);
  assert(!store.addHeader(b2));

  std::cout << "Test 10 (reject non-best-chain parent) passed." << std::endl;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  // Existing SpvHeader tests
  test_header_parse();

  // SpvHeaderStore tests
  test_store_rejects_bad_prevhash();
  test_store_rejects_below_checkpoint();
  test_store_chain_selection();
  test_store_depth_after_reorg();
  test_store_merkle_root_at();
  test_store_empty_best_tip();
  test_store_checkpoint_link();
  test_store_depth_beyond_tip();
  test_store_header_at_height();
  test_store_rejects_non_best_chain_parent();

  std::cout << "All SpvHeaderStore tests passed." << std::endl;
  return 0;
}
