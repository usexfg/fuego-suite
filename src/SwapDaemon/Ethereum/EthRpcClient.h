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

#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace XfgSwap {

struct EthTxReceipt {
  std::string txHash;
  std::string contractAddress;
  uint64_t blockNumber;
  bool success;            // status == 1
  uint64_t gasUsed;
};

class EthRpcClient {
public:
  EthRpcClient(const std::string& host, uint16_t port);

  // Basic queries
  bool getBlockNumber(uint64_t& blockNum);
  bool getBalance(const std::string& address, uint64_t& balanceWei);
  bool getTransactionReceipt(const std::string& txHash, EthTxReceipt& receipt);

  // Contract interaction
  // Deploy the HashedTimelock contract (returns tx hash)
  bool deployContract(const std::string& fromAddress,
                      const std::string& bytecode,
                      uint64_t gasLimit,
                      std::string& txHash);

  // Call a contract method (eth_sendTransaction for state-changing, eth_call for view)
  bool sendTransaction(const std::string& from, const std::string& to,
                       const std::string& data, uint64_t value,
                       uint64_t gasLimit, std::string& txHash);

  bool callContract(const std::string& to, const std::string& data, std::string& result);

  // Send raw signed transaction
  bool sendRawTransaction(const std::string& signedTxHex, std::string& txHash);

  // ─── HTLC operations ─────────────────────────────────────────────────────
  //
  // Deploy the XFG HashedTimelock ETH contract and return the contract address.
  // NOTE: throws std::runtime_error("EIP-155 signing not implemented") until
  // the secp256k1 library is linked.  Callers must catch and mark swap FAILED.
  //
  // hashLockHex:      64-char hex of keccak256(adaptor_secret).
  // recipientAddress: ETH address for the claim path.
  // timeoutBlock:     block number after which refund is valid.
  // valueWei:         ETH to lock (in wei).
  // fromAddress:      sender's ETH address.
  // On success sets contractAddress.
  bool deployHtlc(const std::string& fromAddress,
                  const std::string& recipientAddress,
                  const std::string& hashLockHex,
                  uint64_t timeoutBlock,
                  uint64_t valueWei,
                  std::string& contractAddress);

  // Verify the HTLC contract is deployed and holds the expected value.
  // NOTE: This uses eth_call (read-only) so it works even without signing.
  bool verifyLock(const std::string& contractAddress,
                  uint64_t expectedWei,
                  uint64_t minConfirmBlocks = 1);

  // Claim ETH from the HTLC by revealing the adaptor secret preimage.
  // NOTE: throws std::runtime_error("EIP-155 signing not implemented").
  bool claimHtlc(const std::string& fromAddress,
                 const std::string& contractAddress,
                 const std::string& preimageHex,
                 std::string& claimTxHash);

  // Refund ETH from the HTLC after timeout.
  // NOTE: throws std::runtime_error("EIP-155 signing not implemented").
  bool refundHtlc(const std::string& fromAddress,
                  const std::string& contractAddress,
                  std::string& refundTxHash);

private:
  std::string httpPost(const std::string& path, const std::string& body);
  std::string jsonRpc(const std::string& method, const std::string& params);

  std::string m_host;
  uint16_t m_port;
};

} // namespace XfgSwap
