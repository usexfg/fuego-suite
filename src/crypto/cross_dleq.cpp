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

#include "cross_dleq.h"

#include <cstddef>
#include <cstdint>

extern "C" {
#include "random.h"
}

namespace Crypto {
namespace {

bool belowCrossCurveBound(const SecretKey& x) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&x);
  bool nonzero = false;
  for (size_t i = 0; i < 32; ++i) nonzero |= bytes[i] != 0;
  return nonzero && (bytes[31] & 0xF0) == 0;
}

}  // namespace

void cross_dleq_generate_scalar(SecretKey& x) {
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&x);
  do {
    generate_random_bytes(32, bytes);
    bytes[31] &= 0x0F;
  } while (!belowCrossCurveBound(x));
}

}  // namespace Crypto
