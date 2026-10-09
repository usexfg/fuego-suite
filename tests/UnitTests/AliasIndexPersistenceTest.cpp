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

#include "gtest/gtest.h"

#include "CryptoNoteCore/AliasIndex.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "Common/MemoryInputStream.h"
#include "Common/VectorOutputStream.h"
#include "crypto/hash.h"

using namespace CryptoNote;

namespace {

Crypto::Hash hashOf(const std::string& s) {
  Crypto::Hash h;
  Crypto::cn_fast_hash(s.data(), s.size(), h);
  return h;
}

AliasEntry makeEntry(const std::string& alias, const std::string& owner, uint32_t block) {
  AliasEntry e;
  e.alias = alias;
  e.ownerAddress = owner;
  e.aliasHash = hashOf(alias);
  e.addressHash = hashOf(owner);
  e.aliasType = 1;
  e.registeredBlock = block;
  return e;
}

// Bug: AliasIndex has no serialize() and is never wired into BlockCacheSerializer,
// so every registered alias is wiped on a normal daemon restart. This exercises
// the persistence path directly: serialize an AliasIndex to bytes, load those
// bytes into a fresh instance, and confirm every registered alias survives.
TEST(AliasIndexPersistence, SurvivesSerializeRoundTrip) {
  AliasIndex original;
  ASSERT_TRUE(original.registerAlias(makeEntry("abcdefg1", "addrOne", 100)));
  ASSERT_TRUE(original.registerAlias(makeEntry("abcdefg2", "addrTwo", 200)));
  size_t originalSize = original.size();

  std::vector<uint8_t> buffer;
  {
    Common::VectorOutputStream out(buffer);
    BinaryOutputStreamSerializer s(out);
    serialize(original, s);
  }

  AliasIndex restored;
  {
    Common::MemoryInputStream in(buffer.data(), buffer.size());
    BinaryInputStreamSerializer s(in);
    serialize(restored, s);
  }

  ASSERT_EQ(restored.size(), originalSize);

  auto e1 = restored.getAliasByName("abcdefg1");
  ASSERT_TRUE(e1.has_value());
  EXPECT_EQ(e1->ownerAddress, "addrOne");
  EXPECT_EQ(e1->registeredBlock, 100u);

  auto e2 = restored.getAliasByName("abcdefg2");
  ASSERT_TRUE(e2.has_value());
  EXPECT_EQ(e2->ownerAddress, "addrTwo");
}

// Round-tripping must not duplicate the two hardcoded reserved aliases that
// AliasIndex's own constructor already seeds on both sides.
TEST(AliasIndexPersistence, RestoreDoesNotDuplicateReservedAliases) {
  AliasIndex original;
  size_t reservedOnly = original.size();

  std::vector<uint8_t> buffer;
  {
    Common::VectorOutputStream out(buffer);
    BinaryOutputStreamSerializer s(out);
    serialize(original, s);
  }

  AliasIndex restored;
  {
    Common::MemoryInputStream in(buffer.data(), buffer.size());
    BinaryInputStreamSerializer s(in);
    serialize(restored, s);
  }

  EXPECT_EQ(restored.size(), reservedOnly);
}

// Bug: popTransaction/popBlock have no reversal for alias register/release/
// transfer, so a reorged-out block leaves a phantom mutation permanently in
// place. These exercise the new undo-journal primitive in isolation.

TEST(AliasIndexUndo, RegisterUndoRemovesAlias) {
  AliasIndex index;
  ASSERT_TRUE(index.registerAlias(makeEntry("newalias", "addrNew", 50)));
  ASSERT_TRUE(index.aliasExists("newalias"));

  AliasUndoOp op;
  op.opType = 0;  // register -> undo removes it
  op.alias = "newalias";

  applyAliasUndo(index, {op});
  EXPECT_FALSE(index.aliasExists("newalias"));
}

TEST(AliasIndexUndo, ReleaseUndoRestoresPriorEntry) {
  AliasIndex index;
  ASSERT_TRUE(index.registerAlias(makeEntry("released", "addrRel", 60)));
  auto before = index.getAliasByName("released");
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(index.removeAlias("released"));
  ASSERT_FALSE(index.aliasExists("released"));

  AliasUndoOp op;
  op.opType = 1;  // release -> undo restores the entry as it was
  op.alias = "released";
  op.priorEntry = *before;

  applyAliasUndo(index, {op});
  auto restored = index.getAliasByName("released");
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->ownerAddress, "addrRel");
  EXPECT_EQ(restored->registeredBlock, 60u);
}

TEST(AliasIndexUndo, TransferUndoRestoresAddressAndHash) {
  AliasIndex index;
  ASSERT_TRUE(index.registerAlias(makeEntry("xferred1", "addrOld", 70)));
  AliasEntry prior = *index.getAliasByName("xferred1");
  Crypto::Hash newHash = hashOf("addrNewOwner");
  ASSERT_TRUE(index.replaceAliasOwnership("xferred1", newHash, "addrNewOwner"));
  ASSERT_EQ(index.getAliasByName("xferred1")->addressHash, newHash);
  EXPECT_EQ(index.getAliasByName("xferred1")->ownerAddress, "addrNewOwner");
  EXPECT_FALSE(index.getAliasByAddressHash(prior.addressHash).has_value());
  EXPECT_TRUE(index.getAliasByAddressHash(newHash).has_value());

  AliasUndoOp op;
  op.opType = 2;  // transfer -> undo restores prior owner
  op.alias = "xferred1";
  op.priorEntry = prior;

  applyAliasUndo(index, {op});
  EXPECT_EQ(index.getAliasByName("xferred1")->addressHash, prior.addressHash);
  EXPECT_EQ(index.getAliasByName("xferred1")->ownerAddress, prior.ownerAddress);
  EXPECT_TRUE(index.getAliasByAddressHash(prior.addressHash).has_value());
  EXPECT_FALSE(index.getAliasByAddressHash(newHash).has_value());
}

TEST(AliasIndexUndo, TransferUndoSurvivesSerialization) {
  AliasIndex index;
  ASSERT_TRUE(index.registerAlias(makeEntry("xferred2", "addrBefore", 71)));
  AliasUndoOp original;
  original.opType = 2;
  original.alias = "xferred2";
  original.priorEntry = *index.getAliasByName(original.alias);
  ASSERT_TRUE(index.replaceAliasOwnership(original.alias, hashOf("addrAfter"), "addrAfter"));

  std::vector<uint8_t> buffer;
  {
    Common::VectorOutputStream out(buffer);
    BinaryOutputStreamSerializer s(out);
    serialize(original, s);
  }
  AliasUndoOp restored;
  {
    Common::MemoryInputStream in(buffer.data(), buffer.size());
    BinaryInputStreamSerializer s(in);
    serialize(restored, s);
  }
  applyAliasUndo(index, {restored});
  auto entry = index.getAliasByName(original.alias);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->ownerAddress, "addrBefore");
  EXPECT_EQ(entry->addressHash, original.priorEntry.addressHash);
}

// A block that registers then releases the same alias must undo as a unit,
// in LIFO order: undoing release first (restore), then undoing register
// (remove) — landing back at "never existed". Applying the same ops in
// forward order instead would leave the alias registered (wrong).
TEST(AliasIndexUndo, MultipleOpsUndoneInReverseOrder) {
  AliasIndex index;
  ASSERT_TRUE(index.registerAlias(makeEntry("lifoalia", "addrLifo", 80)));
  auto beforeRelease = index.getAliasByName("lifoalia");
  ASSERT_TRUE(index.removeAlias("lifoalia"));

  AliasUndoOp regOp;
  regOp.opType = 0;
  regOp.alias = "lifoalia";
  AliasUndoOp relOp;
  relOp.opType = 1;
  relOp.alias = "lifoalia";
  relOp.priorEntry = *beforeRelease;

  applyAliasUndo(index, {regOp, relOp});
  EXPECT_FALSE(index.aliasExists("lifoalia"));
}

}  // namespace
