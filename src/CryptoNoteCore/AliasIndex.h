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

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../crypto/crypto.h"
#include "../crypto/hash.h"
#include "../CryptoNoteConfig.h"

namespace CryptoNote {

class ISerializer;

// @ Alias entry for on-chain alias registry
struct AliasEntry {
  std::string alias;            // "fuegodev" (regular)
  std::string ownerAddress;     // Stored for /get_alias resolution. Not returned by /get_all_aliases.
  Crypto::Hash aliasHash;       // cn_fast_hash(alias) for fast lookup
  Crypto::Hash addressHash;     // cn_fast_hash(address) for privacy
  uint8_t aliasType = 1;        // 0 = reserved (deprecated), 1 = Regular [a-z0-9&]
  uint32_t registeredBlock = 0;

  void serialize(ISerializer& s);
};

class AliasIndex {
public:
  AliasIndex();
  ~AliasIndex();

  // Registration
  bool registerAlias(const AliasEntry& entry);

  // Release (void/delete) an alias — removes from index
  // Caller must have verified ownership before calling this.
  bool removeAlias(const std::string& alias);

  // Transfer alias ownership — replaces addressHash mapping
  // Caller must have verified old ownership before calling this.
  // newAddressHash: cn_fast_hash(newOwnerAddress)
  bool replaceAliasOwnership(const std::string& alias,
                             const Crypto::Hash& newAddressHash);

  // Queries
  bool aliasExists(const std::string& alias) const;
  // Legacy string-address overloads (hash address as base58 string — v1 scheme).
  // Prefer the Hash overloads below for new code (v2 scheme: hash spend+view bytes).
  bool addressHasAlias(const std::string& address) const;
  std::optional<AliasEntry> getAliasByAddress(const std::string& address) const;
  // v2 hash-based overloads: caller computes cn_fast_hash(spendKey||viewKey).
  bool addressHasAliasByHash(const Crypto::Hash& addrHash) const;
  std::optional<AliasEntry> getAliasByAddressHash(const Crypto::Hash& addrHash) const;
  std::optional<AliasEntry> getAliasByName(const std::string& alias) const;
  std::vector<AliasEntry> getAllAliases() const;

  // State
  size_t size() const;

  // Cache persistence: without this, every registered/released/transferred
  // alias is wiped on a normal daemon restart (there is no on-disk record —
  // only rebuildCache(), which runs on cache-load failure, replays the chain
  // and repopulates this index).
  void serialize(ISerializer& s);

  // Validation helpers (static, usable by callers before registration)
  static bool isValidRegularAlias(const std::string& alias);

private:
  mutable std::mutex m_mutex;

  // Alias storage
  std::map<std::string, AliasEntry> m_aliases;          // alias -> entry
  std::map<std::string, std::string> m_addrHashToAlias; // cn_fast_hash(address) hex -> alias (no raw addresses stored)

  // Reserved alias names (registered at genesis / init)
  void reserveDevTeamAliases();
};

// Reorg-undo journal entry for a single AliasIndex mutation. The Blockchain
// records one of these per successful register/release/transfer while
// processing a block, and replays them via applyAliasUndo when that block is
// popped during a reorg — otherwise a reorged-out mutation permanently
// squats (or frees, or misattributes) the alias name.
struct AliasUndoOp {
  uint8_t opType = 0;               // 0 = register, 1 = release, 2 = transfer
  std::string alias;
  AliasEntry priorEntry;            // opType==1: full entry to restore on undo
  Crypto::Hash priorAddressHash{};  // opType==2: addressHash to revert to on undo
};

// Reverts `ops` against `index`, LAST-applied-first (LIFO). A block that
// registers then releases the same alias must undo as a unit in that order
// (undo the release, then undo the register) to land back at "never
// existed" — applying in forward order would leave it registered.
void applyAliasUndo(AliasIndex& index, const std::vector<AliasUndoOp>& ops);

}  // namespace CryptoNote
