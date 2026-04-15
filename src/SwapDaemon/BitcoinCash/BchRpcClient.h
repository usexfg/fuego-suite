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

struct BchUtxo {
  std::string txid;
  uint32_t vout;
  uint64_t satoshis;
  std::string scriptPubKey;
  uint32_t confirmations;
};

struct BchTxInfo {
  std::string txid;
  uint32_t confirmations;
  uint64_t blockHeight;
  bool inMempool;
};

class BchRpcClient {
public:
  BchRpcClient(const std::string& host, uint16_t port,
               const std::string& rpcUser, const std::string& rpcPassword);

  // Basic queries
  bool getBlockCount(uint64_t& height);
  bool getTransaction(const std::string& txid, BchTxInfo& info);
  bool getBalance(const std::string& address, uint64_t& satoshis);

  // UTXO queries
  bool listUnspent(const std::string& address, std::vector<BchUtxo>& utxos);

  // Raw transaction
  bool sendRawTransaction(const std::string& rawTxHex, std::string& txid);
  bool decodeRawTransaction(const std::string& rawTxHex, std::string& jsonResult);

  // Address utilities
  bool validateAddress(const std::string& address, bool& isValid);

  // Import address for watching (no private key)
  bool importAddress(const std::string& address, const std::string& label, bool rescan);

  // ─── HTLC operations ─────────────────────────────────────────────────────
  //
  // Lock BCH in a P2SH HTLC.
  // hashLockSha256Hex: 64-char hex of SHA256(adaptor_secret) — NOT RIPEMD160.
  // recipientAddress: BCH address for claim path.
  // senderWif:        WIF-encoded private key of the sender.
  // timeoutBlock:     block height after which refund is valid.
  // amountSatoshis:   BCH to lock.
  // On success sets lockTxId.
  bool lockHtlc(const std::string& senderWif,
                const std::string& recipientAddress,
                const std::string& hashLockSha256Hex,
                uint32_t timeoutBlock,
                uint64_t amountSatoshis,
                std::string& lockTxId);

  // Verify that an HTLC locking transaction is confirmed on-chain.
  // htlcAddress: the P2SH address that should hold the funds.
  // minConfirms: minimum confirmations required (default 1).
  bool verifyLock(const std::string& htlcAddress,
                  uint64_t expectedSatoshis,
                  uint32_t minConfirms = 1);

  // Claim BCH from an HTLC by revealing the preimage (adaptor secret).
  // claimerWif:     WIF key of the recipient.
  // htlcTxid:       txid of the locking transaction.
  // htlcVout:       output index of the HTLC output.
  // htlcAmount:     satoshis locked in the HTLC.
  // redeemScriptHex: hex-encoded redeem script.
  // preimageHex:    32-byte hex adaptor secret.
  // destAddress:    where to send the claimed BCH.
  bool claim(const std::string& claimerWif,
             const std::string& htlcTxid,
             uint32_t htlcVout,
             uint64_t htlcAmount,
             const std::string& redeemScriptHex,
             const std::string& preimageHex,
             const std::string& destAddress,
             std::string& claimTxId);

  // Refund BCH from an HTLC after the timeout has elapsed.
  // senderWif:      WIF key of the original sender.
  // htlcTxid:       txid of the locking transaction.
  // htlcVout:       output index of the HTLC output.
  // htlcAmount:     satoshis locked.
  // redeemScriptHex: hex-encoded redeem script.
  // destAddress:    where to send the refunded BCH.
  bool refundHtlc(const std::string& senderWif,
                  const std::string& htlcTxid,
                  uint32_t htlcVout,
                  uint64_t htlcAmount,
                  const std::string& redeemScriptHex,
                  const std::string& destAddress,
                  std::string& refundTxId);

private:
  std::string rpcCall(const std::string& method, const std::string& params);
  std::string httpPost(const std::string& body);
  std::string base64Encode(const std::string& input);

  std::string m_host;
  uint16_t m_port;
  std::string m_rpcUser;
  std::string m_rpcPassword;
  std::string m_authHeader;  // "Basic base64(user:pass)"
};

} // namespace XfgSwap
