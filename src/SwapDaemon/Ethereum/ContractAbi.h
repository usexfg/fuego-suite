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
#include "../../crypto/hash.h"

namespace XfgSwap {
namespace EthAbi {

// Encode a function selector: first 4 bytes of keccak256(signature)
std::string functionSelector(const std::string& signature);

// ABI-encode parameters for lock(address,bytes32,uint256)
std::string encodeLock(const std::string& recipientAddr, const Crypto::Hash& hashLock, uint64_t timeoutBlock);

// ABI-encode parameters for claim(bytes32,bytes32)
std::string encodeClaim(const std::string& contractId, const Crypto::Hash& preimage);

// ABI-encode parameters for refund(bytes32)
std::string encodeRefund(const std::string& contractId);

// ABI-encode parameters for getContract(bytes32) -- view call
std::string encodeGetContract(const std::string& contractId);

// Decode getContract response
struct ContractInfo {
  std::string sender;
  std::string recipient;
  uint64_t amount;
  Crypto::Hash hashLock;
  uint64_t timeoutBlock;
  bool claimed;
  bool refunded;
  Crypto::Hash preimage;
};
bool decodeGetContract(const std::string& hexData, ContractInfo& info);

// Helper: pad a 20-byte address to 32 bytes (left-pad with zeros)
std::string padAddress(const std::string& addr);

// Helper: encode uint256 as 32-byte hex
std::string encodeUint256(uint64_t value);

// Helper: encode bytes32 from Crypto::Hash
std::string encodeBytes32(const Crypto::Hash& h);

} // namespace EthAbi
} // namespace XfgSwap
