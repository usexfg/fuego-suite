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

#include "UtxoFullNode.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {

// How the hash-time-lock script is committed to on chain and signed.
//   P2WSH:       BTC, LTC. Witness spend, BIP143 sighash, SIGHASH_ALL.
//   P2SH_FORKID: BCH. scriptSig spend, BIP143-style sighash, SIGHASH_ALL|FORKID.
enum class UtxoHtlcEncoding { P2WSH, P2SH_FORKID };

// OP_IF OP_SHA256 <hashLock> OP_EQUALVERIFY <recipient> OP_CHECKSIG
// OP_ELSE <timeoutBlock> OP_CHECKLOCKTIMEVERIFY OP_DROP <sender> OP_CHECKSIG OP_ENDIF
struct UtxoHtlcTerms {
  std::vector<uint8_t> hashLock;         // SHA256(t), 32 bytes
  std::vector<uint8_t> recipientPubKey;  // compressed, claims with t
  std::vector<uint8_t> senderPubKey;     // compressed, refunds after timeout
  uint32_t timeoutBlock = 0;
};

std::vector<uint8_t> buildHtlcScript(const UtxoHtlcTerms& terms);
// Accepts only the exact byte layout buildHtlcScript produces.
bool parseHtlcScript(const std::vector<uint8_t>& script, UtxoHtlcTerms& terms);
std::vector<uint8_t> htlcScriptPubKey(UtxoHtlcEncoding encoding, const std::vector<uint8_t>& script);
std::string htlcFundingAddress(UtxoHtlcEncoding encoding, const UtxoNetworkParams& network,
                               const std::vector<uint8_t>& script);

std::vector<uint8_t> sha256Digest(const std::vector<uint8_t>& data);
bool decodeWif(const UtxoNetworkParams& network, const std::string& wif,
               std::array<uint8_t, 32>& key);
bool compressedPublicKey(const std::array<uint8_t, 32>& key, std::vector<uint8_t>& pubKey);
bool isCompressedPublicKey(const std::vector<uint8_t>& pubKey);
std::string p2pkhAddress(const UtxoNetworkParams& network, const std::vector<uint8_t>& pubKey);
std::vector<uint8_t> p2pkhScriptForPubKey(const std::vector<uint8_t>& pubKey);
bool p2pkhScriptForAddress(const UtxoNetworkParams& network, const std::string& address,
                           std::vector<uint8_t>& script);

struct UtxoHtlcSpend {
  UtxoOutpoint lock;
  uint64_t lockValue = 0;
  std::vector<uint8_t> htlcScript;
  std::array<uint8_t, 32> signingKey{};
  std::vector<uint8_t> destinationScript;
  uint64_t fee = 0;
};

// Claim: nLockTime 0, nSequence 0xFFFFFFFD. Refund: nLockTime = timeout,
// nSequence 0xFFFFFFFE (CHECKLOCKTIMEVERIFY needs a non-final sequence).
bool buildHtlcClaim(UtxoHtlcEncoding encoding, const UtxoHtlcSpend& spend,
                    const std::vector<uint8_t>& preimage, std::vector<uint8_t>& rawTx,
                    std::string& error);
bool buildHtlcRefund(UtxoHtlcEncoding encoding, const UtxoHtlcSpend& spend,
                     std::vector<uint8_t>& rawTx, std::string& error);
uint64_t htlcSpendVirtualSize(UtxoHtlcEncoding encoding, bool claimPath);

// One P2PKH output spent back to the same key: the child that raises an
// unconfirmed parent's fee where nodes refuse replacements (UtxoFeeBump).
struct P2pkhSelfSpend {
  UtxoOutpoint input;
  uint64_t inputValue = 0;
  uint64_t fee = 0;
  std::array<uint8_t, 32> signingKey{};
  std::vector<uint8_t> publicKey;  // compressed; the input and the output pay its P2PKH
};

constexpr uint64_t P2PKH_SELF_SPEND_SIZE = 192;  // with the longest low-S signature

bool buildP2pkhSelfSpend(UtxoHtlcEncoding encoding, const P2pkhSelfSpend& spend, std::vector<uint8_t>& rawTx);

bool spendsOutpoint(const std::vector<uint8_t>& rawTx, const UtxoOutpoint& outpoint);

// Output `vout` of a serialized transaction (legacy or segwit encoding).
bool readTransactionOutput(const std::vector<uint8_t>& rawTx, uint32_t vout, uint64_t& value,
                           std::vector<uint8_t>& scriptPubKey);

// The witness of the input of `rawTx` that spends `lock`; false when no input spends it.
bool spendWitness(const std::vector<uint8_t>& rawTx, const UtxoOutpoint& lock,
                  std::vector<std::vector<uint8_t>>& witness);

// The scriptSig pushes of the input of `rawTx` that spends `lock` (non-witness
// transactions); false when no input spends it or its scriptSig is not a
// sequence of plain data pushes.
bool spendScriptSig(const std::vector<uint8_t>& rawTx, const UtxoOutpoint& lock,
                    std::vector<std::vector<uint8_t>>& items);

// The preimage revealed by the input of `rawTx` that spends `lock` through the
// claim branch of `htlcScript`, only if its SHA256 equals the script's hash lock.
bool extractHtlcPreimage(UtxoHtlcEncoding encoding, const std::vector<uint8_t>& rawTx,
                         const UtxoOutpoint& lock, const std::vector<uint8_t>& htlcScript,
                         std::vector<uint8_t>& preimage);

}  // namespace XfgSwap
