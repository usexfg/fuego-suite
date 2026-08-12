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

struct ZanoTransferResult {
  std::string txHash;
  uint64_t fee;
  bool success;
  std::string error;
};

struct ZanoTxInfo {
  std::string txHash;
  uint64_t amount;
  uint32_t confirmations;
  bool inPool;
};

class ZanoRpcClient {
public:
  ZanoRpcClient(const std::string& daemonHost, uint16_t daemonPort,
                  const std::string& walletHost, uint16_t walletPort);

  // Daemon RPC (zanod)
  bool getHeight(uint64_t& height);
  bool getTransaction(const std::string& txHash, ZanoTxInfo& info);

  // Wallet RPC (zano-wallet-rpc / simplewallet)
  // Create a shared 2-of-2 address from Alice's and Bob's spend/view PUBLIC
  // keys (all 64-char hex). Pure local ed25519 math (no RPC):
  //   shared spend pub = A_spend + B_spend ; shared view pub = A_view + B_view
  // then encode as a Zano address with the given network prefix.
  // (The matching shared *secret* view key = a_view + b_view is assembled by
  // the negotiation layer so both parties can scan; that wiring is separate.)
  bool createSharedAddress(const std::string& aliceSpendPub, const std::string& bobSpendPub,
                           const std::string& aliceViewPub, const std::string& bobViewPub,
                           std::string& sharedAddress,
                           uint64_t networkPrefix = 197 /* Zano mainnet */);

  // Transfer ZANO to the shared address
  bool transferToShared(const std::string& address, uint64_t amount, ZanoTransferResult& result);

  // Sweep the shared address (once both keys are known).
  // walletName : per-swap suffix so concurrent swaps don't collide on the
  //              temp from-keys wallet (empty → a shared default name).
  // restoreHeight : wallet scan start height (0 → from genesis; slow).
  // targetHeight  : if > 0, poll the wallet's get_height until it reaches this
  //                 (the daemon height) BEFORE sweeping; if 0, poll until the
  //                 scan height stabilizes. NEVER sweep an unsynced wallet.
  // networkPrefix : network tag used to derive the from-keys wallet's primary
  //                 address (some zano-wallet-rpc builds reject an empty
  //                 address in generate_from_keys). Must match the wallet-rpc's
  //                 network (197 mainnet).
  virtual bool sweepSharedAddress(const std::string& spendKeyHex, const std::string& viewKeyHex,
                                   const std::string& destAddress, ZanoTransferResult& result,
                                   const std::string& walletName = "",
                                   uint64_t restoreHeight = 0,
                                   uint64_t targetHeight = 0,
                                   uint64_t networkPrefix = 197 /* Zano mainnet */);

  // Check if an address has received funds. Requires viewKeyHex so a temporary
  // watch-only wallet can be opened for THAT address (never trusts whatever
  // wallet happens to be open on the RPC).
  bool checkAddressBalance(const std::string& address,
                           const std::string& viewKeyHex,
                           uint64_t& balance, uint64_t& unlocked,
                           uint64_t restoreHeight = 0);

  // Open a temporary watch-only wallet for address+viewKey, refresh, then
  // close. Used by checkAddressBalance / verifyLock.
  bool openWatchOnly(const std::string& address, const std::string& viewKeyHex,
                     const std::string& walletName, uint64_t restoreHeight = 0);

  // ─── Adaptor-signature lock/claim/refund ──────────────────────────────────
  //
  // For the ZANO/XFG atomic swap the ZANO side uses a view-key adaptor scheme
  // (no on-chain script; lock = send to shared address, reveal = spend key).

  // Lock ZANO by transferring to the shared 2-of-2 address.
  bool lockAdaptor(const std::string& sharedAddress,
                   uint64_t amountAtoms,
                   ZanoTransferResult& result);

  // Verify the shared address holds the expected unlocked amount.
  // viewKeyHex must be the shared view secret for `sharedAddress`.
  bool verifyLock(const std::string& sharedAddress,
                   const std::string& viewKeyHex,
                   uint64_t expectedAtoms);

  bool checkReserveProof(const std::string& address, const std::string& message,
                         const std::string& signature, bool& good, uint64_t& total);

  bool claimAdaptor(const std::string& aliceSpendKeyHex,
                    const std::string& bobSpendKeyHex,
                    const std::string& adaptorSecretHex,
                    const std::string& viewKeyHex,
                    const std::string& destAddress,
                    ZanoTransferResult& result);

  // Refund ZANO from the shared address cooperatively (both keys present).
  // Used when the swap times out before Alice claims on counterparty chain.
  bool refundAdaptor(const std::string& aliceShareHex,
                     const std::string& bobShareHex,
                     const std::string& viewKeyHex,
                     const std::string& destAddress,
                     ZanoTransferResult& result);

protected:
  // Wallet-RPC seam: all zano-wallet-rpc calls route through here so tests
  // can override with canned responses + assert the call sequence. Default
  // wraps jsonRpc to the wallet host/port.
  virtual std::string walletRpc(const std::string& method, const std::string& params);

  // Delay between sync polls in sweepSharedAddress. Default sleeps ~1.5s so a
  // fresh from-keys wallet has time to scan; tests override to a no-op.
  virtual void syncPollDelay();

private:
  std::string httpPost(const std::string& host, uint16_t port, const std::string& path, const std::string& body);
  std::string jsonRpc(const std::string& host, uint16_t port, const std::string& method, const std::string& params);

  std::string m_daemonHost;
  uint16_t m_daemonPort;
  std::string m_walletHost;
  uint16_t m_walletPort;
};

} // namespace XfgSwap
