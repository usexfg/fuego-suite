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

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../crypto/hash.h"

namespace CryptoNote {

// A release registration: one audio file, one signer, one height.
//
// The leaf is not contentHash alone. Hashing only the content would make two
// different signers of the same file collapse into one leaf, and a later
// metadata edit would be invisible to the proof. The leaf binds all of it.
struct ReleaseRecord {
  Crypto::Hash contentHash;   // SHA-256 of the audio file, computed by the caller
  Crypto::Hash signer;        // 32-byte Ed25519 public key that signed the registration
  uint64_t height;            // chain height the registration landed at
  std::vector<uint8_t> metadata;  // opaque, max RELEASE_METADATA_MAX
};

struct ReleaseProof {
  bool found;
  Crypto::Hash contentHash;
  Crypto::Hash leaf;
  Crypto::Hash root;
  uint64_t height;
  uint32_t leafIndex;
  std::vector<Crypto::Hash> path;
};

// Maximum accepted metadata payload. Anything larger is rejected rather than
// truncated: a truncated leaf would prove something the signer never signed.
constexpr size_t RELEASE_METADATA_MAX = 256;

// Append-only merkle index of release registrations.
//
// Pairing rule matches CommitmentIndex: cn_fast_hash over a 64-byte
// concatenation, with an odd trailing node duplicated. Leaves are
// leafHash(record), not contentHash.
//
// This class does no chain I/O. It is fed by the chain layer and read by RPC.
class ReleaseRegistry {
public:
  static Crypto::Hash leafHash(const ReleaseRecord& rec);
  static bool verify(const ReleaseProof& proof);

  // Returns false when metadata exceeds RELEASE_METADATA_MAX, or when the
  // content hash is already registered. The first registration wins; a second
  // claim on the same bytes does not replace it.
  bool insert(const ReleaseRecord& rec);

  // Drops every record registered at this height. Used by block disconnect.
  void eraseByHeight(uint64_t height);

  std::optional<ReleaseRecord> findByContentHash(const Crypto::Hash& contentHash) const;
  ReleaseProof proof(const Crypto::Hash& contentHash) const;
  Crypto::Hash root() const;
  size_t count() const;

private:
  Crypto::Hash computeRootUnlocked() const;
  Crypto::Hash computeRootFromLeaves(const std::vector<Crypto::Hash>& leaves) const;

  mutable std::mutex m_mutex;
  std::vector<Crypto::Hash> m_leaves;
  std::vector<ReleaseRecord> m_records;
  std::unordered_map<Crypto::Hash, size_t> m_index;
  std::map<uint64_t, std::vector<Crypto::Hash>> m_byHeight;

  // Root is recomputed lazily and cached. Every proof request used to rebuild
  // the whole tree, which made an unauthenticated GET cost O(leaves).
  mutable Crypto::Hash m_cachedRoot;
  mutable bool m_rootDirty = true;
};

}  // namespace CryptoNote
