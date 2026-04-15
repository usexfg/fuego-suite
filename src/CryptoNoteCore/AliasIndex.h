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

// @ Alias entry for on-chain alias registry
struct AliasEntry {
  std::string alias;            // "FUEGOXFG" (EFier) or "fuegodev" (regular)
  std::string ownerAddress;     // Always empty — not stored on-chain. Use addressHash for privacy-preserving lookup.
  Crypto::Hash aliasHash;       // cn_fast_hash(alias) for fast lookup
  Crypto::Hash addressHash;     // cn_fast_hash(address) for privacy
  uint8_t aliasType = 0;        // 0 = Elderfier [A-Z0-9&], 1 = Regular [a-z0-9&]
  uint32_t registeredBlock = 0;
};

class AliasIndex {
public:
  AliasIndex();
  ~AliasIndex();

  // Registration
  bool registerAlias(const AliasEntry& entry);

  // Queries
  bool aliasExists(const std::string& alias) const;
  bool addressHasAlias(const std::string& address) const;
  std::optional<AliasEntry> getAliasByName(const std::string& alias) const;
  std::optional<AliasEntry> getAliasByAddress(const std::string& address) const;
  std::vector<AliasEntry> getAllAliases() const;

  // State
  size_t size() const;

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

}  // namespace CryptoNote
