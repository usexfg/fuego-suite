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
#include <array>

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
  // Construct with JSON-RPC endpoint.
  EthRpcClient(const std::string& host, uint16_t port);

  // Construct with JSON-RPC endpoint + signer credentials.
  // privKeyHex: 64-char hex of the 32-byte secp256k1 private key.
  // signerAddress: "0x..." Ethereum address derived from privKeyHex (caller must
  //   derive and supply; see Secp256k1Signer::derivePublicKey + keccak256(pubkey[1..]).
  // chainId: EIP-155 chain ID (1 = mainnet, 11155111 = Sepolia, etc.).
  EthRpcClient(const std::string& host, uint16_t port,
               const std::string& privKeyHex,
               const std::string& signerAddress,
               uint64_t chainId);

  // Basic queries
  bool getBlockNumber(uint64_t& blockNum);
  bool getBalance(const std::string& address, uint64_t& balanceWei);
  bool getTransactionReceipt(const std::string& txHash, EthTxReceipt& receipt);
  bool getNonce(const std::string& address, uint64_t& nonce);

  // Contract interaction
  // Deploy the HashedTimelock contract (returns tx hash)
  bool deployContract(const std::string& fromAddress,
                      const std::string& bytecode,
                      uint64_t gasLimit,
                      std::string& txHash);

  // Call a contract method (state-changing; signs and broadcasts raw tx)
  bool sendTransaction(const std::string& from, const std::string& to,
                       const std::string& data, uint64_t value,
                       uint64_t gasLimit, std::string& txHash);

  bool callContract(const std::string& to, const std::string& data, std::string& result);

  // Send raw signed transaction
  bool sendRawTransaction(const std::string& signedTxHex, std::string& txHash);

  // ─── HTLC operations ─────────────────────────────────────────────────────
  //
  // Deploy the XFG HashedTimelock ETH contract and return the contract address.
  //
  // hashLockHex:      64-char hex of keccak256(adaptor_secret).
  // recipientAddress: ETH address for the claim path.
  // timeoutBlock:     block number after which refund is valid.
  // valueWei:         ETH to lock (in wei).
  // fromAddress:      sender's ETH address (must match signer address).
  // On success sets contractAddress.
  bool deployHtlc(const std::string& fromAddress,
                  const std::string& recipientAddress,
                  const std::string& hashLockHex,
                  uint64_t timeoutBlock,
                  uint64_t valueWei,
                  std::string& contractAddress);

  // Verify the HTLC contract is deployed and holds the expected value.
  // Read-only (eth_call), does not require a private key.
  bool verifyLock(const std::string& contractAddress,
                  uint64_t expectedWei,
                  uint64_t minConfirmBlocks = 1);

  // Claim ETH from the HTLC by revealing the adaptor secret preimage.
  bool claimHtlc(const std::string& fromAddress,
                 const std::string& contractAddress,
                 const std::string& preimageHex,
                 std::string& claimTxHash);

  // Refund ETH from the HTLC after timeout.
  bool refundHtlc(const std::string& fromAddress,
                  const std::string& contractAddress,
                  std::string& refundTxHash);

private:
  std::string httpPost(const std::string& path, const std::string& body);
  std::string jsonRpc(const std::string& method, const std::string& params);

  // Sign an EIP-155 transaction and broadcast it via eth_sendRawTransaction.
  // to: 20-byte address (empty for contract deploy).
  // data: calldata bytes.
  // valueWei: ETH value to send.
  // gasLimit: gas limit.
  // Returns the tx hash on success.
  // Throws std::runtime_error if the signer private key is not configured.
  bool signAndSend(const std::vector<uint8_t>& to,
                   const std::vector<uint8_t>& data,
                   uint64_t valueWei,
                   uint64_t gasLimit,
                   std::string& txHash);

  // Build a signed raw EIP-155 transaction.
  std::vector<uint8_t> buildSignedTx(uint64_t nonce,
                                     uint64_t gasPriceWei,
                                     uint64_t gasLimit,
                                     const std::vector<uint8_t>& to,
                                     uint64_t valueWei,
                                     const std::vector<uint8_t>& data);

  // Set the pre-compiled HashedTimelock contract bytecode (hex, no 0x prefix).
  // Must be called before deployHtlc if deploying a new contract.
  void setHtlcBytecode(const std::string& bytecodeHex) { m_htlcBytecode = bytecodeHex; }

  std::string m_host;
  uint16_t    m_port;

  // Signing credentials — empty if not configured.
  std::array<uint8_t, 32> m_privKey;   // zeroed if not configured
  std::string              m_signerAddress;
  uint64_t                 m_chainId = 0;
  bool                     m_hasSigner = false;

  // Pre-compiled HTLC contract bytecode (hex, no 0x prefix).
  std::string m_htlcBytecode;
};

} // namespace XfgSwap
