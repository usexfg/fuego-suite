// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "subaddress.h"

#include <vector>

extern "C" {
#include "crypto-ops.h"
}

namespace Crypto {
namespace {

bool addScalarBaseToPoint(const PublicKey& B, const EllipticCurveScalar& m, PublicKey& D) {
  ge_p3 B_p3;
  if (ge_frombytes_vartime(&B_p3, reinterpret_cast<const unsigned char*>(&B)) != 0) {
    return false;
  }

  ge_p3 mG_p3;
  ge_scalarmult_base(&mG_p3, reinterpret_cast<const unsigned char*>(&m));

  ge_cached mG_cached;
  ge_p3_to_cached(&mG_cached, &mG_p3);

  ge_p1p1 D_p1p1;
  ge_add(&D_p1p1, &B_p3, &mG_cached);

  ge_p3 D_p3;
  ge_p1p1_to_p3(&D_p3, &D_p1p1);
  ge_p3_tobytes(reinterpret_cast<unsigned char*>(&D), &D_p3);
  return true;
}

void hashIndexScalar(const char* domain, size_t domainLen, const SecretKey& a,
                     uint32_t major, uint32_t minor, EllipticCurveScalar& m) {
  std::vector<uint8_t> pre(domainLen + sizeof(SecretKey) + 8);
  memcpy(pre.data(), domain, domainLen);
  memcpy(pre.data() + domainLen, &a, sizeof(SecretKey));
  memcpy(pre.data() + domainLen + sizeof(SecretKey), &major, 4);
  memcpy(pre.data() + domainLen + sizeof(SecretKey) + 4, &minor, 4);
  hash_to_scalar(pre.data(), pre.size(), m);
}

} // namespace

bool scalarmultKeyNoCofactor(const PublicKey& point, const SecretKey& scalar, PublicKey& result) {
  if (sc_check(reinterpret_cast<const unsigned char*>(&scalar)) != 0) {
    return false;
  }
  ge_p3 P_p3;
  if (ge_frombytes_vartime(&P_p3, reinterpret_cast<const unsigned char*>(&point)) != 0) {
    return false;
  }
  ge_p2 R_p2;
  ge_scalarmult(&R_p2, reinterpret_cast<const unsigned char*>(&scalar), &P_p3);
  ge_tobytes(reinterpret_cast<unsigned char*>(&result), &R_p2);
  return true;
}

bool subaddressTxPubKey(const SecretKey& txSecret, const PublicKey& subSpendPublicKey, PublicKey& txPubKey) {
  return scalarmultKeyNoCofactor(subSpendPublicKey, txSecret, txPubKey);
}

bool janusCheck(const PublicKey& txPubKey, const PublicKey& janusBase, const SecretKey& subSpendSecret) {
  PublicKey expected;
  if (!scalarmultKeyNoCofactor(janusBase, subSpendSecret, expected)) {
    return false;
  }
  return memcmp(&expected, &txPubKey, sizeof(PublicKey)) == 0;
}

SubAddressKeys deriveSubAddressKeys(
    const SecretKey&  a,
    const PublicKey&  B,
    const SecretKey*  b,
    uint32_t major,
    uint32_t minor)
{
  SubAddressKeys out{};
  out.hasSpendSecretKey = (b != nullptr);
  out.version = SUBADDRESS_VERSION_V1;

#ifdef _MSC_VER
  #pragma pack(push, 1)
#endif
  struct
#ifdef __GNUC__
  __attribute__((packed))
#endif
  PreImage {
    char     domain[7];
    SecretKey a;
    uint32_t i;
    uint32_t j;
  } pre;
#ifdef _MSC_VER
  #pragma pack(pop)
#endif

  memcpy(pre.domain, "Sublime", 7);
  pre.a = a;
  pre.i = major;
  pre.j = minor;

  EllipticCurveScalar m;
  hash_to_scalar(&pre, sizeof(pre), m);

  if (!addScalarBaseToPoint(B, m, out.spendPublicKey)) {
    return out;
  }

  if (!secret_key_to_public_key(a, out.viewPublicKey)) {
    return out;
  }

  if (b != nullptr) {
    sc_add(reinterpret_cast<unsigned char*>(&out.spendSecretKey),
           reinterpret_cast<const unsigned char*>(b),
           reinterpret_cast<const unsigned char*>(&m));
  }

  return out;
}

SubAddressKeys deriveSubAddressKeysV2(
    const SecretKey&  a,
    const PublicKey&  B,
    const SecretKey*  b,
    uint32_t major,
    uint32_t minor)
{
  SubAddressKeys out{};
  out.hasSpendSecretKey = (b != nullptr);
  out.version = SUBADDRESS_VERSION_V2;

  EllipticCurveScalar m;
  hashIndexScalar("FuegoSubAddrV2", 14, a, major, minor, m);

  if (!addScalarBaseToPoint(B, m, out.spendPublicKey)) {
    return out;
  }

  if (!scalarmultKeyNoCofactor(out.spendPublicKey, a, out.viewPublicKey)) {
    return out;
  }

  if (b != nullptr) {
    sc_add(reinterpret_cast<unsigned char*>(&out.spendSecretKey),
           reinterpret_cast<const unsigned char*>(b),
           reinterpret_cast<const unsigned char*>(&m));
  }

  return out;
}

} // namespace Crypto
