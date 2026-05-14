// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
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
//
// Solana JSON-RPC client for XFG/SOL atomic swap HTLC interaction.
//
// Talks to a Solana validator node (solana-test-validator for dev,
// devnet/mainnet for production) to lock/claim/refund SOL via the
// xfg_htlc Anchor program.

#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace XfgSwap {

// Solana account info returned by getAccountInfo.
struct SolAccountInfo {
  uint64_t lamports;
  std::string owner;       // program that owns this account (base58)
  std::string dataBase64;  // account data, base64 encoded
  bool executable;
  uint64_t rentEpoch;
};

// Parsed HTLC state from on-chain account data.
struct SolHtlcInfo {
  std::string sender;       // base58 pubkey
  std::string recipient;    // base58 pubkey
  uint64_t amount;          // lamports
  std::string hashLock;     // 32-byte hex (Keccak-256)
  uint64_t timeoutSlot;
  bool claimed;
  bool refunded;
  std::string preimage;     // 32-byte hex (set after claim, else all zeros)
};

// Result of a transaction send.
struct SolTxResult {
  std::string signature;    // base58 tx signature
  bool confirmed;
  std::string error;        // empty on success
};

class SolRpcClient {
public:
  // host: Solana RPC endpoint (e.g., "http://127.0.0.1" for local,
  //        "https://api.devnet.solana.com" for devnet)
  // port: RPC port (8899 for local validator, 443 for HTTPS endpoints)
  // programId: deployed xfg_htlc program ID (base58)
  SolRpcClient(const std::string& host, uint16_t port,
               const std::string& programId);

  // ─── Basic queries ──────────────────────────────────────────────

  // Get current slot height.
  bool getSlot(uint64_t& slot);

  // Get SOL balance for a pubkey (in lamports).
  bool getBalance(const std::string& pubkey, uint64_t& lamports);

  // Get account info (raw).
  bool getAccountInfo(const std::string& pubkey, SolAccountInfo& info);

  // Check if a transaction is confirmed.
  // Returns false on RPC failure; sets confirmed=true/false.
  bool getSignatureStatus(const std::string& signature, bool& confirmed);

  // ─── HTLC operations ───────────────────────────────────────────
  //
  // These build, sign, and send transactions that call the xfg_htlc
  // Anchor program.  The senderSecretKey is the Solana Ed25519
  // private key (64 bytes, base58 encoded like Solana CLI keypair).

  // Lock SOL into an HTLC.
  //
  // senderSecretKey: 64-byte Solana keypair (base58)
  // recipientPubkey: recipient's Solana pubkey (base58)
  // hashLock:        Keccak-256(adaptor_secret), 32 bytes hex
  // timeoutSlot:     slot after which refund is allowed
  // amountLamports:  SOL to lock (in lamports, 1 SOL = 1e9 lamports)
  //
  // On success, sets result.signature and result.confirmed.
  bool lock(const std::string& senderSecretKey,
            const std::string& recipientPubkey,
            const std::string& hashLockHex,
            uint64_t timeoutSlot,
            uint64_t amountLamports,
            SolTxResult& result);

  // Claim locked SOL by revealing the preimage (adaptor secret).
  //
  // claimerSecretKey: recipient's Solana keypair (base58)
  // htlcAccount:      HTLC state account pubkey (base58)
  // preimageHex:      adaptor secret t, 32 bytes hex
  bool claim(const std::string& claimerSecretKey,
             const std::string& htlcAccount,
             const std::string& preimageHex,
             SolTxResult& result);

  // Refund locked SOL after timeout.
  //
  // senderSecretKey: original sender's Solana keypair (base58)
  // htlcAccount:     HTLC state account pubkey (base58)
  bool refund(const std::string& senderSecretKey,
              const std::string& htlcAccount,
              SolTxResult& result);

  // ─── HTLC queries ──────────────────────────────────────────────

  // Derive the HTLC PDA address from sender pubkey + hashlock.
  // Uses seeds: [b"xfg_htlc", sender_pubkey, hash_lock]
  std::string deriveHtlcAddress(const std::string& senderPubkey,
                                const std::string& hashLockHex);

  // Derive the vault PDA address from HTLC account pubkey.
  // Uses seeds: [b"xfg_htlc", htlc_pubkey]
  std::string deriveVaultAddress(const std::string& htlcPubkey);

  // Read and parse HTLC state from the on-chain account.
  bool getHtlcState(const std::string& htlcAccount, SolHtlcInfo& info);

  // Watch for a Claimed event on an HTLC account.
  // Polls getHtlcState until claimed==true or timeout.
  // On success, sets preimageHex to the revealed adaptor secret.
  // pollIntervalMs: how often to poll (default 2000ms)
  // maxWaitMs: give up after this long (default 600000ms = 10 min)
  bool waitForClaim(const std::string& htlcAccount,
                    std::string& preimageHex,
                    uint32_t pollIntervalMs = 2000,
                    uint64_t maxWaitMs = 600000);

  // Verify that a SOL HTLC is on-chain, holds the expected amount, and is
  // not yet claimed or refunded.
  // htlcAccount:      HTLC state account pubkey (base58).
  // expectedLamports: expected locked amount.
  bool verifyLock(const std::string& htlcAccount,
                  uint64_t expectedLamports);

private:
  // JSON-RPC call to Solana node.
  std::string jsonRpc(const std::string& method, const std::string& params);

  // HTTP POST to RPC endpoint.
  std::string httpPost(const std::string& body);

  // Hex/base58/base64 helpers.
  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);
  static std::string base64Encode(const std::vector<uint8_t>& data);
  static std::vector<uint8_t> base64Decode(const std::string& encoded);

  // Parse Anchor account discriminator + HtlcState from raw account data.
  bool parseHtlcState(const std::vector<uint8_t>& data, SolHtlcInfo& info);

  // Build a Solana transaction calling the HTLC program instruction.
  // Returns serialized tx bytes ready for sendTransaction.
  std::vector<uint8_t> buildLockTx(const std::string& senderSecretKey,
                                    const std::string& recipientPubkey,
                                    const std::vector<uint8_t>& hashLock,
                                    uint64_t timeoutSlot,
                                    uint64_t amountLamports,
                                    const std::string& recentBlockhash);

  std::vector<uint8_t> buildClaimTx(const std::string& claimerSecretKey,
                                     const std::string& htlcAccount,
                                     const std::vector<uint8_t>& preimage,
                                     const std::string& recentBlockhash);

  std::vector<uint8_t> buildRefundTx(const std::string& senderSecretKey,
                                      const std::string& htlcAccount,
                                      const std::string& recentBlockhash);

  // Get a recent blockhash for transaction signing.
  bool getRecentBlockhash(std::string& blockhash);

  // Send a serialized transaction.
  bool sendAndConfirmTransaction(const std::vector<uint8_t>& txBytes,
                                  SolTxResult& result);

  std::string m_host;
  uint16_t m_port;
  std::string m_programId;  // xfg_htlc program ID (base58)
  std::string m_rpcUrl;     // full URL: host:port
};

} // namespace XfgSwap
