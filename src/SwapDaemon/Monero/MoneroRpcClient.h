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

struct MoneroTransferResult {
  std::string txHash;
  uint64_t fee;
  bool success;
  std::string error;
};

struct MoneroTxInfo {
  std::string txHash;
  uint64_t amount;
  uint32_t confirmations;
  bool inPool;
};

class MoneroRpcClient {
public:
  MoneroRpcClient(const std::string& daemonHost, uint16_t daemonPort,
                  const std::string& walletHost, uint16_t walletPort);

  // Daemon RPC (monerod)
  bool getHeight(uint64_t& height);
  bool getTransaction(const std::string& txHash, MoneroTxInfo& info);

  // Wallet RPC (monero-wallet-rpc)
  // Create a shared 2-of-2 address from Alice's and Bob's view/spend keys
  bool createSharedAddress(const std::string& aliceSpendPub, const std::string& bobSpendPub,
                           const std::string& aliceViewPub, const std::string& bobViewPub,
                           std::string& sharedAddress);

  // Transfer XMR to the shared address
  bool transferToShared(const std::string& address, uint64_t amount, MoneroTransferResult& result);

  // Sweep the shared address (once both keys are known)
  bool sweepSharedAddress(const std::string& spendKeyHex, const std::string& viewKeyHex,
                          const std::string& destAddress, MoneroTransferResult& result);

  // Check if an address has received funds
  bool checkAddressBalance(const std::string& address, uint64_t& balance, uint64_t& unlocked);

  // ─── Adaptor-signature lock/claim/refund ──────────────────────────────────
  //
  // For the XMR/XFG atomic swap the XMR side uses a view-key adaptor scheme
  // (no on-chain script; lock = send to shared address, reveal = spend key).
  // All methods below are stubs pending the full CLSAG adaptor implementation.

  // Lock XMR by transferring to the shared 2-of-2 address.
  // adaptorPointHex: Bob's adaptor point T = t*G (hex), used to derive the lock.
  // amountPiconero:  amount to lock.
  // On success sets lockTxHash.
  bool lockAdaptor(const std::string& sharedAddress,
                   uint64_t amountPiconero,
                   MoneroTransferResult& result);

  // Verify the shared address holds the expected amount (unlocked).
  bool verifyLock(const std::string& sharedAddress,
                  uint64_t expectedPiconero);

  // Claim XMR from the shared address by providing both spend keys.
  // This is equivalent to sweepSharedAddress with the adaptor secret applied.
  // adaptorSecretHex: the 32-byte adaptor secret t (hex).
  bool claimAdaptor(const std::string& aliceSpendKeyHex,
                    const std::string& bobSpendKeyHex,
                    const std::string& viewKeyHex,
                    const std::string& destAddress,
                    MoneroTransferResult& result);

  // Refund XMR from the shared address cooperatively (both keys present).
  // Used when the swap times out before Alice claims on counterparty chain.
  bool refundAdaptor(const std::string& spendKeyHex,
                     const std::string& viewKeyHex,
                     const std::string& destAddress,
                     MoneroTransferResult& result);

private:
  std::string httpPost(const std::string& host, uint16_t port, const std::string& path, const std::string& body);
  std::string jsonRpc(const std::string& host, uint16_t port, const std::string& method, const std::string& params);

  std::string m_daemonHost;
  uint16_t m_daemonPort;
  std::string m_walletHost;
  uint16_t m_walletPort;
};

} // namespace XfgSwap
