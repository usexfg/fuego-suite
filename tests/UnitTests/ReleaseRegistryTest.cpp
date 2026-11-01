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

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "CryptoNoteCore/ReleaseRegistry.h"

namespace {

Crypto::Hash filled(uint8_t byte) {
  Crypto::Hash h;
  std::memset(h.data, byte, sizeof(h.data));
  return h;
}

CryptoNote::ReleaseRecord makeRecord(uint8_t seed, uint64_t height) {
  CryptoNote::ReleaseRecord rec;
  rec.contentHash = filled(seed);
  rec.signer = filled(static_cast<uint8_t>(0xA0 + seed));
  rec.height = height;
  return rec;
}

}  // namespace

TEST(ReleaseRegistry, EmptyRootIsZeroAndLookupMisses) {
  CryptoNote::ReleaseRegistry reg;
  EXPECT_EQ(reg.root(), Crypto::Hash());
  EXPECT_EQ(reg.count(), 0u);
  EXPECT_FALSE(reg.findByContentHash(filled(1)).has_value());
  EXPECT_FALSE(reg.proof(filled(1)).found);
}

TEST(ReleaseRegistry, InsertBindsSignerAndHeightIntoTheLeaf) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord rec = makeRecord(1, 9);
  rec.metadata = {0x01, 0x02};
  ASSERT_TRUE(reg.insert(rec));

  auto found = reg.findByContentHash(rec.contentHash);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->signer, rec.signer);
  EXPECT_EQ(found->height, 9u);
  EXPECT_EQ(found->metadata.size(), 2u);

  // Same content, different signer: the leaves must not collide.
  CryptoNote::ReleaseRecord other = rec;
  other.signer = filled(0x55);
  EXPECT_NE(CryptoNote::ReleaseRegistry::leafHash(rec),
            CryptoNote::ReleaseRegistry::leafHash(other));

  // Same content and signer, different height: also must not collide.
  CryptoNote::ReleaseRecord later = rec;
  later.height = 10;
  EXPECT_NE(CryptoNote::ReleaseRegistry::leafHash(rec),
            CryptoNote::ReleaseRegistry::leafHash(later));

  // Metadata is part of the leaf, so it cannot be edited after the fact.
  CryptoNote::ReleaseRecord edited = rec;
  edited.metadata = {0x03};
  EXPECT_NE(CryptoNote::ReleaseRegistry::leafHash(rec),
            CryptoNote::ReleaseRegistry::leafHash(edited));
}

TEST(ReleaseRegistry, SecondContentHashDoesNotReplaceTheFirst) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord first = makeRecord(1, 1);
  CryptoNote::ReleaseRecord again = first;
  again.height = 2;
  again.signer = filled(0x77);

  ASSERT_TRUE(reg.insert(first));
  EXPECT_FALSE(reg.insert(again));
  EXPECT_EQ(reg.count(), 1u);

  auto found = reg.findByContentHash(first.contentHash);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->height, 1u);
  EXPECT_EQ(found->signer, first.signer);
}

TEST(ReleaseRegistry, ProofVerifiesForEveryLeafAndRejectsTampering) {
  CryptoNote::ReleaseRegistry reg;
  for (uint8_t i = 1; i <= 5; ++i) {
    ASSERT_TRUE(reg.insert(makeRecord(i, i)));
  }

  for (uint8_t i = 1; i <= 5; ++i) {
    auto p = reg.proof(filled(i));
    ASSERT_TRUE(p.found) << "leaf " << static_cast<int>(i);
    EXPECT_EQ(p.root, reg.root());
    EXPECT_TRUE(CryptoNote::ReleaseRegistry::verify(p)) << "leaf " << static_cast<int>(i);

    // Flipping any proof byte must break the fold.
    if (!p.path.empty()) {
      CryptoNote::ReleaseProof tampered = p;
      tampered.path[0].data[0] ^= 0xff;
      EXPECT_FALSE(CryptoNote::ReleaseRegistry::verify(tampered));
    }

    // A wrong root must not verify.
    CryptoNote::ReleaseProof wrongRoot = p;
    wrongRoot.root = filled(0xEE);
    EXPECT_FALSE(CryptoNote::ReleaseRegistry::verify(wrongRoot));
  }
}

TEST(ReleaseRegistry, SingleLeafProofIsEmptyPathAndVerifiesAgainstItself) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord rec = makeRecord(1, 4);
  ASSERT_TRUE(reg.insert(rec));

  auto p = reg.proof(rec.contentHash);
  ASSERT_TRUE(p.found);
  EXPECT_TRUE(p.path.empty());
  EXPECT_EQ(p.root, CryptoNote::ReleaseRegistry::leafHash(rec));
  EXPECT_EQ(p.leaf, p.root);
  EXPECT_TRUE(CryptoNote::ReleaseRegistry::verify(p));
}

TEST(ReleaseRegistry, EraseByHeightDropsOnlyThatHeight) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord a = makeRecord(1, 4);
  CryptoNote::ReleaseRecord b = makeRecord(2, 4);
  CryptoNote::ReleaseRecord c = makeRecord(3, 5);

  ASSERT_TRUE(reg.insert(a));
  ASSERT_TRUE(reg.insert(b));
  ASSERT_TRUE(reg.insert(c));
  ASSERT_EQ(reg.count(), 3u);

  reg.eraseByHeight(4);

  EXPECT_FALSE(reg.findByContentHash(a.contentHash).has_value());
  EXPECT_FALSE(reg.findByContentHash(b.contentHash).has_value());
  EXPECT_TRUE(reg.findByContentHash(c.contentHash).has_value());
  EXPECT_EQ(reg.count(), 1u);

  // The surviving record must still prove against the new root. A stale slot
  // number would return the wrong record and this would fail.
  auto p = reg.proof(c.contentHash);
  ASSERT_TRUE(p.found);
  EXPECT_EQ(p.height, 5u);
  EXPECT_TRUE(CryptoNote::ReleaseRegistry::verify(p));

  // Erasing an unknown height is a no-op.
  reg.eraseByHeight(999);
  EXPECT_EQ(reg.count(), 1u);
}

TEST(ReleaseRegistry, MetadataOver256IsRejected) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord rec = makeRecord(1, 1);
  rec.metadata.assign(CryptoNote::RELEASE_METADATA_MAX + 1, 0x7f);

  EXPECT_FALSE(reg.insert(rec));
  EXPECT_EQ(reg.count(), 0u);

  rec.metadata.assign(CryptoNote::RELEASE_METADATA_MAX, 0x7f);
  EXPECT_TRUE(reg.insert(rec));
  EXPECT_EQ(reg.count(), 1u);
}
