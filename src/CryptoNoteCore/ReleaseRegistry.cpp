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

#include "ReleaseRegistry.h"

#include <cassert>
#include <cstring>

#include "../crypto/hash.h"

namespace CryptoNote {

namespace {

void appendLe64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
  }
}

void appendLe16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

Crypto::Hash hashPair(const Crypto::Hash& left, const Crypto::Hash& right) {
  uint8_t combined[64];
  std::memcpy(combined, left.data, 32);
  std::memcpy(combined + 32, right.data, 32);
  Crypto::Hash parent;
  Crypto::cn_fast_hash(combined, sizeof(combined), parent);
  return parent;
}

}  // namespace

Crypto::Hash ReleaseRegistry::leafHash(const ReleaseRecord& rec) {
  // Precondition: metadata fits in RELEASE_METADATA_MAX. insert() enforces it;
  // this helper does not, and the 16-bit length field would silently truncate
  // anything past 65535 bytes, collapsing two distinct records into one leaf.
  assert(rec.metadata.size() <= RELEASE_METADATA_MAX);

  std::vector<uint8_t> preimage;
  preimage.reserve(32 + 32 + 8 + 2 + rec.metadata.size());
  preimage.insert(preimage.end(), rec.contentHash.data, rec.contentHash.data + 32);
  preimage.insert(preimage.end(), rec.signer.data, rec.signer.data + 32);
  appendLe64(preimage, rec.height);
  appendLe16(preimage, static_cast<uint16_t>(rec.metadata.size()));
  preimage.insert(preimage.end(), rec.metadata.begin(), rec.metadata.end());

  Crypto::Hash leaf;
  Crypto::cn_fast_hash(preimage.data(), preimage.size(), leaf);
  return leaf;
}

Crypto::Hash ReleaseRegistry::computeRootFromLeaves(const std::vector<Crypto::Hash>& leaves) const {
  if (leaves.empty()) {
    return Crypto::Hash();
  }

  std::vector<Crypto::Hash> level = leaves;
  while (level.size() > 1) {
    std::vector<Crypto::Hash> next;
    next.reserve((level.size() + 1) / 2);
    for (size_t i = 0; i < level.size(); i += 2) {
      const Crypto::Hash& left = level[i];
      const Crypto::Hash& right = (i + 1 < level.size()) ? level[i + 1] : level[i];
      next.push_back(hashPair(left, right));
    }
    level.swap(next);
  }
  return level.front();
}

Crypto::Hash ReleaseRegistry::computeRootUnlocked() const {
  if (m_rootDirty) {
    m_cachedRoot = computeRootFromLeaves(m_leaves);
    m_rootDirty = false;
  }
  return m_cachedRoot;
}

bool ReleaseRegistry::insert(const ReleaseRecord& rec) {
  if (rec.metadata.size() > RELEASE_METADATA_MAX) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_index.find(rec.contentHash) != m_index.end()) {
    return false;
  }

  // Ordered-by-contract: insertion order determines root determinism.
  // The feed must be ordered; this code does NOT enforce height monotonicity.
  m_index.emplace(rec.contentHash, m_records.size());
  m_records.push_back(rec);
  m_leaves.push_back(leafHash(rec));
  m_byHeight[rec.height].push_back(rec.contentHash);
  m_rootDirty = true;
  return true;
}

void ReleaseRegistry::eraseByHeight(uint64_t height) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_byHeight.find(height);
  if (it == m_byHeight.end()) {
    return;
  }

  for (const Crypto::Hash& contentHash : it->second) {
    auto idxIt = m_index.find(contentHash);
    if (idxIt == m_index.end()) {
      continue;
    }
    const size_t idx = idxIt->second;
    m_records.erase(m_records.begin() + static_cast<std::ptrdiff_t>(idx));
    m_leaves.erase(m_leaves.begin() + static_cast<std::ptrdiff_t>(idx));
    m_index.erase(idxIt);

    // Every record after the removed slot shifts down by one. Rebuild the
    // index rather than patch it: a stale slot number silently returns the
    // wrong record, and the proof would still verify against the wrong leaf.
    m_index.clear();
    for (size_t i = 0; i < m_records.size(); ++i) {
      m_index.emplace(m_records[i].contentHash, i);
    }
  }

  m_byHeight.erase(it);
  m_rootDirty = true;
}

std::optional<ReleaseRecord> ReleaseRegistry::findByContentHash(const Crypto::Hash& contentHash) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_index.find(contentHash);
  if (it == m_index.end()) {
    return std::nullopt;
  }
  return m_records[it->second];
}

ReleaseProof ReleaseRegistry::proof(const Crypto::Hash& contentHash) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  ReleaseProof out{};
  out.found = false;
  out.contentHash = contentHash;
  out.root = computeRootUnlocked();

  auto idxIt = m_index.find(contentHash);
  if (idxIt == m_index.end()) {
    return out;
  }

  const size_t leafIdx = idxIt->second;
  out.found = true;
  out.leaf = m_leaves[leafIdx];
  out.height = m_records[leafIdx].height;
  out.leafIndex = static_cast<uint32_t>(leafIdx);

  std::vector<Crypto::Hash> level = m_leaves;
  size_t idx = leafIdx;
  while (level.size() > 1) {
    size_t siblingIdx = (idx % 2 == 0)
        ? ((idx + 1 < level.size()) ? idx + 1 : idx)
        : idx - 1;
    out.path.push_back(level[siblingIdx]);

    std::vector<Crypto::Hash> next;
    next.reserve((level.size() + 1) / 2);
    for (size_t i = 0; i < level.size(); i += 2) {
      const Crypto::Hash& left = level[i];
      const Crypto::Hash& right = (i + 1 < level.size()) ? level[i + 1] : level[i];
      next.push_back(hashPair(left, right));
    }

    idx /= 2;
    level.swap(next);
  }

  return out;
}

Crypto::Hash ReleaseRegistry::root() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return computeRootUnlocked();
}

size_t ReleaseRegistry::count() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_leaves.size();
}

bool ReleaseRegistry::verify(const ReleaseProof& proof) {
  if (!proof.found) {
    return false;
  }

  Crypto::Hash current = proof.leaf;
  size_t idx = proof.leafIndex;

  for (const Crypto::Hash& sibling : proof.path) {
    if (idx % 2 == 0) {
      current = hashPair(current, sibling);
    } else {
      current = hashPair(sibling, current);
    }
    idx /= 2;
  }

  return current == proof.root;
}

}  // namespace CryptoNote
