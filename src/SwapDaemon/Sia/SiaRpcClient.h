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

struct SiaUtxo {
  std::string txid;
  uint32_t vout;
  uint64_t hastings;  // 1 SC = 10^24 hastings
  std::string address;
  uint32_t confirmations;
};

struct SiaTxInfo {
  std::string txid;
  uint32_t confirmations;
  uint64_t blockHeight;
  bool inMempool;
};

struct SiaConsensus {
  uint64_t height;
  std::string currentBlockId;
  std::string previousBlockId;
  uint64_t target;
};

class SiaRpcClient {
public:
  SiaRpcClient(const std::string& host, uint16_t port,
               const std::string& apiPassword = "");

  // Basic queries
  bool getBlockCount(uint64_t& height);
  bool getConsensus(SiaConsensus& consensus);
  bool getTransaction(const std::string& txid, SiaTxInfo& info);
  bool getBalance(const std::string& address, uint64_t& hastings);

  // UTXO queries
  bool listUnspent(const std::string& address, std::vector<SiaUtxo>& utxos);

  // Raw transaction
  bool getRawTransaction(const std::string& txid, std::string& rawTxHex);
  bool sendRawTransaction(const std::string& rawTxHex, std::string& txid);

  // Address utilities
  bool validateAddress(const std::string& address, bool& isValid);

  // Import address for watching (no private key)
  bool importAddress(const std::string& address, const std::string& label, bool rescan);

  // ─── HTLC operations ─────────────────────────────────────────────────────
  //
  // Lock SC in a P2SH HTLC (using Sia v2 opcodes).
  // hashLockSha256Hex: 64-char hex of SHA256(adaptor_secret) — NOT RIPEMD160.
  // recipientAddress: Sia address for claim path.
  // senderWif:        Ed25519 private key of the sender.
  // timeoutBlock:     block height after which refund is valid.
  // amountHastings:   SC to lock (in hastings, 1 SC = 10^24 hastings).
  // On success sets lockTxId.
  bool lockHtlc(const std::string& senderPrivKeyHex,
                const std::string& recipientAddress,
                const std::string& hashLockSha256Hex,
                uint32_t timeoutBlock,
                uint64_t amountHastings,
                std::string& lockTxId,
                std::string& redeemScriptHex);

  // Verify that an HTLC locking transaction is confirmed on-chain.
  // htlcAddress: the P2SH address that should hold the funds.
  // minConfirms: minimum confirmations required (default 1).
  bool verifyLock(const std::string& htlcAddress,
                  uint64_t expectedHastings,
                  uint32_t minConfirms = 1);

  // Claim SC from an HTLC by revealing the preimage (adaptor secret).
  // claimerPrivKeyHex: Ed25519 private key of the recipient.
  // htlcTxid:       txid of the locking transaction.
  // htlcVout:       output index of the HTLC output.
  // htlcAmount:     hastings locked in the HTLC.
  // redeemScriptHex: hex-encoded redeem script.
  // preimageHex:    32-byte hex adaptor secret.
  // destAddress:    where to send the claimed SC.
  bool claim(const std::string& claimerPrivKeyHex,
             const std::string& htlcTxid,
             uint32_t htlcVout,
             uint64_t htlcAmount,
             const std::string& redeemScriptHex,
             const std::string& preimageHex,
             const std::string& destAddress,
             std::string& claimTxId);

  // Refund SC from an HTLC after the timeout has elapsed.
  // senderPrivKeyHex: Ed25519 private key of the original sender.
  // htlcTxid:       txid of the locking transaction.
  // htlcVout:       output index of the HTLC output.
  // htlcAmount:     hastings locked.
  // redeemScriptHex: hex-encoded redeem script.
  // timeoutBlock:   CLTV timeout block height for nLocktime.
  // destAddress:    where to send the refunded SC.
  bool refundHtlc(const std::string& senderPrivKeyHex,
                  const std::string& htlcTxid,
                  uint32_t htlcVout,
                  uint64_t htlcAmount,
                  const std::string& redeemScriptHex,
                  uint32_t timeoutBlock,
                  const std::string& destAddress,
                  std::string& refundTxId);

private:
  // REST API helpers
  std::string httpGet(const std::string& path);
  std::string httpPost(const std::string& path, const std::string& body);
  std::string httpPut(const std::string& path, const std::string& body);
  std::string httpDelete(const std::string& path);

  // JSON parsing helpers
  bool parseJsonString(const std::string& json, const std::string& key, std::string& value);
  bool parseJsonUint64(const std::string& json, const std::string& key, uint64_t& value);
  bool parseJsonUint32(const std::string& json, const std::string& key, uint32_t& value);

  std::string m_host;
  uint16_t m_port;
  std::string m_apiPassword;  // optional API password for siad
  std::string m_baseUrl;      // http://host:port
};

} // namespace XfgSwap
