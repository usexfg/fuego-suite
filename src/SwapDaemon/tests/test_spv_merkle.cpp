// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

// Merkle proof verification test using real BCH block data.
//
// Block 586 (BCH mainnet):
//   hash: 000000000d0d23516c5efd3af4eb951603bb30b2c93884b522a318b30e918ee7
//   merkleRoot: 197b3d968ce463aa5da7d8eeba8af35eba80ded4e4fe6808e6cc0dd1c069594d
//   tx count: 3
//
// Transaction 1 (non-coinbase):
//   txid: 4d6edbeb62735d45ff1565385a8b0045f066055c9425e21540ea7a8060f08bf2
//   pos: 1
//   merkle branch (Electrum get_merkle format, deepest first):
//     [0] d45724bacd1480b0c94d363ebf59f844fb54e60cdfda0cd38ef67154e9d0bc43  (tx0, left)
//     [1] ca851cc1acd01b82667c83fd88914b9a3e1a0a99fbf2b83c89cc79ea29d0c5f0  (h22, right)
//
// These were verified by independently computing the merkle root from
// the raw block data (parsed from blockchair.com BCH API).

#include <cassert>
#include <string>
#include <vector>
#include <iostream>
#include "SwapDaemon/Spv/SpvMerkle.h"

using namespace XfgSwap;

int main() {
  // Real BCH block 586 data
  std::string txidDisplay = "4d6edbeb62735d45ff1565385a8b0045f066055c9425e21540ea7a8060f08bf2";
  std::vector<std::string> branchDisplay = {
    "d45724bacd1480b0c94d363ebf59f844fb54e60cdfda0cd38ef67154e9d0bc43",  // tx0 (coinbase)
    "ca851cc1acd01b82667c83fd88914b9a3e1a0a99fbf2b83c89cc79ea29d0c5f0",  // h22 = dsha256(tx2||tx2)
  };
  uint32_t pos = 1;
  std::string merkleRootDisplay = "197b3d968ce463aa5da7d8eeba8af35eba80ded4e4fe6808e6cc0dd1c069594d";

  // Positive test: compute root and verify it matches
  std::string computed = SpvMerkle::computeRootHexDisplay(txidDisplay, branchDisplay, pos);
  assert(computed == merkleRootDisplay && "Merkle root must match known BCH block");

  // Negative test: tamper with a branch hash -> must NOT match
  auto bad = branchDisplay;
  bad[0][0] = (bad[0][0] == 'a' ? 'b' : 'a');
  assert(SpvMerkle::computeRootHexDisplay(txidDisplay, bad, pos) != merkleRootDisplay);

  // Also test with the coinbase tx at pos=0 (same block)
  std::string coinbaseTxid = "d45724bacd1480b0c94d363ebf59f844fb54e60cdfda0cd38ef67154e9d0bc43";
  std::vector<std::string> coinbaseBranch = {
    "4d6edbeb62735d45ff1565385a8b0045f066055c9425e21540ea7a8060f08bf2",  // tx1
    "ca851cc1acd01b82667c83fd88914b9a3e1a0a99fbf2b83c89cc79ea29d0c5f0",  // h22
  };
  uint32_t coinbasePos = 0;

  std::string coinbaseComputed = SpvMerkle::computeRootHexDisplay(
      coinbaseTxid, coinbaseBranch, coinbasePos);
  assert(coinbaseComputed == merkleRootDisplay && "Coinbase merkle root must also match");

  // Empty branch: tx is the only transaction (pos=0, no branches)
  // In this case the root IS the txid itself
  std::string soloComputed = SpvMerkle::computeRootHexDisplay(coinbaseTxid, {}, 0);
  assert(soloComputed == coinbaseTxid && "Empty branch returns the txid itself");

  std::cout << "All SpvMerkle tests passed." << std::endl;
  return 0;
}
