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

#include "ZanoAddress.h"
#include "Common/Base58.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

namespace XfgSwap {

std::string ZanoAddress::encode(const std::vector<uint8_t>& spendPub,
                                  const std::vector<uint8_t>& viewPub,
                                  uint64_t prefix) {
  if (spendPub.size() != 32 || viewPub.size() != 32) return "";
  // CryptoNote/Monero address payload = spend(32) || view(32); encode_addr
  // prepends the varint prefix and appends the keccak checksum.
  std::string data;
  data.reserve(64);
  data.append(reinterpret_cast<const char*>(spendPub.data()), 32);
  data.append(reinterpret_cast<const char*>(viewPub.data()), 32);
  return Tools::Base58::encode_addr(prefix, data);
}

bool ZanoAddress::sharedSpendPub(const std::vector<uint8_t>& A,
                                   const std::vector<uint8_t>& B,
                                   std::vector<uint8_t>& out) {
  if (A.size() != 32 || B.size() != 32) return false;
  ge_p3 a3, b3;
  if (ge_frombytes_vartime(&a3, A.data()) != 0) return false;
  if (ge_frombytes_vartime(&b3, B.data()) != 0) return false;
  ge_cached bCached;
  ge_p3_to_cached(&bCached, &b3);
  ge_p1p1 sum;
  ge_add(&sum, &a3, &bCached);   // A + B
  ge_p3 r3;
  ge_p1p1_to_p3(&r3, &sum);
  out.resize(32);
  ge_p3_tobytes(out.data(), &r3);
  return true;
}

} // namespace XfgSwap
