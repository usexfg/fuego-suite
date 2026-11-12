// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "CrossCurveDleq.h"

extern "C" {
size_t fuego_cross_curve_dleq_max_proof_size();
int32_t fuego_cross_curve_dleq_prove(const uint8_t* secret, uint8_t* proofOut, size_t proofCapacity,
                                     size_t* proofSizeOut, uint8_t* edPointOut, uint8_t* secpPointOut);
int32_t fuego_cross_curve_dleq_verify(const uint8_t* proof, size_t proofSize, const uint8_t* edPoint,
                                      const uint8_t* secpPoint);
}

namespace XfgSwap {
namespace CrossCurveDleq {

size_t maxProofSize() { return fuego_cross_curve_dleq_max_proof_size(); }

bool prove(const Crypto::SecretKey& t, Crypto::PublicKey& edPoint, Crypto::SecpPubKey& secpPoint,
           std::vector<uint8_t>& proof) {
  std::vector<uint8_t> buffer(maxProofSize());
  size_t size = 0;
  if (fuego_cross_curve_dleq_prove(reinterpret_cast<const uint8_t*>(&t), buffer.data(), buffer.size(), &size,
                                   reinterpret_cast<uint8_t*>(&edPoint), secpPoint.data.data()) != 1 ||
      size == 0 || size > buffer.size())
    return false;
  buffer.resize(size);
  proof.swap(buffer);
  return true;
}

bool verify(const Crypto::PublicKey& edPoint, const Crypto::SecpPubKey& secpPoint,
            const std::vector<uint8_t>& proof) {
  return !proof.empty() && proof.size() <= maxProofSize() &&
         fuego_cross_curve_dleq_verify(proof.data(), proof.size(), reinterpret_cast<const uint8_t*>(&edPoint),
                                       secpPoint.data.data()) == 1;
}

}  // namespace CrossCurveDleq
}  // namespace XfgSwap
