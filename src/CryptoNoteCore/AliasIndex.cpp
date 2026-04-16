// Copyright (c) 2018-2025, Fuego Developers
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

#include "AliasIndex.h"
#include "../CryptoNoteConfig.h"
#include "../Common/StringTools.h"
#include <algorithm>
#include <cctype>

namespace CryptoNote {

AliasIndex::AliasIndex() {
  reserveDevTeamAliases();
}

AliasIndex::~AliasIndex() {}

// Reserved aliases — cannot be registered by users.
// Permanently pre-allocated at genesis (block 0) to the Fuego Developer Fund address.
// Add new entries here to extend the reserved set; each must be exactly 8 chars.
// All entries must be lowercase — registerAlias normalises input to lowercase
// before comparing, so uppercase variants would never match.
static const struct { const char* name; uint8_t type; } RESERVED_ALIASES[] = {
  { "fuegoxfg", 1 },  // Reserved — Fuego project identity (regular alias)
  { "fuegodev", 1 },  // Reserved — Fuego developer fund  (regular alias)
};
static const size_t RESERVED_ALIASES_COUNT = sizeof(RESERVED_ALIASES) / sizeof(RESERVED_ALIASES[0]);

void AliasIndex::reserveDevTeamAliases() {
  // Reserve dev team aliases at genesis (block 0)
  // These are permanently owned by the Fuego Developer Fund address
  const std::string devAddress = CryptoNote::FUEGO_DEV_FUND_ADDRESS;

  for (size_t i = 0; i < RESERVED_ALIASES_COUNT; ++i) {
    const std::string name = RESERVED_ALIASES[i].name;
    AliasEntry entry;
    entry.alias = name;
    entry.ownerAddress = "";  // Not stored on-chain for privacy
    entry.aliasHash = Crypto::cn_fast_hash(name.data(), name.size());
    entry.addressHash = Crypto::cn_fast_hash(devAddress.data(), devAddress.size());
    entry.aliasType = RESERVED_ALIASES[i].type;
    entry.registeredBlock = 0;  // Genesis

    std::string addrHashHex = Common::podToHex(entry.addressHash);
    m_aliases[entry.alias] = entry;
    // Map by address hash — only one alias per address, use the first one
    if (m_addrHashToAlias.find(addrHashHex) == m_addrHashToAlias.end()) {
      m_addrHashToAlias[addrHashHex] = entry.alias;
    }
  }
}

// ============================================================================
// VALIDATION HELPERS
// ============================================================================

// Regular alias: exactly 8 characters from [a-z 0-9 &]
bool AliasIndex::isValidRegularAlias(const std::string& alias) {
  if (alias.length() != 8) return false;
  for (char c : alias) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '&');
    if (!ok) return false;
  }
  return true;
}

// ============================================================================
// REGISTRATION
// ============================================================================

bool AliasIndex::registerAlias(const AliasEntry& entry) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Normalize alias to lowercase so lookups and storage are case-consistent.
  // Callers may pass mixed-case input; we canonicalize here so the index
  // never stores two aliases that differ only in case.
  AliasEntry normalizedEntry = entry;
  std::transform(normalizedEntry.alias.begin(), normalizedEntry.alias.end(),
                 normalizedEntry.alias.begin(), ::tolower);
  // Recompute aliasHash from the normalized (lowercase) form so hash lookups
  // are consistent regardless of the caller's capitalization.
  normalizedEntry.aliasHash = Crypto::cn_fast_hash(
      normalizedEntry.alias.data(), normalizedEntry.alias.size());

  // Reject reserved aliases first — before any other validation.
  for (size_t i = 0; i < RESERVED_ALIASES_COUNT; ++i) {
    if (normalizedEntry.alias == RESERVED_ALIASES[i].name) {
      return false;  // Reserved — cannot be registered by users
    }
  }

  // Reject duplicate alias names
  if (m_aliases.find(normalizedEntry.alias) != m_aliases.end()) {
    return false;  // Alias already taken
  }

  // Check address hash does not already have an alias (no raw address stored)
  std::string addrHashHex = Common::podToHex(normalizedEntry.addressHash);
  if (m_addrHashToAlias.find(addrHashHex) != m_addrHashToAlias.end()) {
    return false;  // Address already has an alias
  }

  // Validate alias format based on type
  if (normalizedEntry.aliasType == 0) {
    return false;  // Elderfier aliases no longer supported
  } else if (normalizedEntry.aliasType == 1) {
    if (!isValidRegularAlias(normalizedEntry.alias)) {
      return false;
    }
  } else {
    return false;  // Unknown alias type
  }

  // Store the alias — reverse map uses addressHash hex, not raw address
  m_aliases[normalizedEntry.alias] = normalizedEntry;
  m_addrHashToAlias[addrHashHex] = normalizedEntry.alias;
  return true;
}

// DEPRECATED: voidAlias has no authentication model — any caller could void any alias.
// It is unreachable from all RPC surfaces and has no callers in the codebase.
// Removed from public interface (AliasIndex.h). Retained here as dead code for reference
// until a signed-revocation path (sign(alias_name, owner_spend_key)) is designed.
// DO NOT call this function — it is not declared in the header.
// TODO: implement signed-revocation auth before re-exposing this via RPC.

// ============================================================================
// QUERIES
// ============================================================================

bool AliasIndex::aliasExists(const std::string& alias) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_aliases.find(alias) != m_aliases.end();
}

bool AliasIndex::addressHasAlias(const std::string& address) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  Crypto::Hash addrHash = Crypto::cn_fast_hash(address.data(), address.size());
  return m_addrHashToAlias.find(Common::podToHex(addrHash)) != m_addrHashToAlias.end();
}

std::optional<AliasEntry> AliasIndex::getAliasByName(const std::string& alias) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Normalize to lowercase so lookups are case-insensitive (aliases are stored
  // in lowercase canonical form since registerAlias normalizes on insertion).
  std::string normalizedAlias = alias;
  std::transform(normalizedAlias.begin(), normalizedAlias.end(),
                 normalizedAlias.begin(), ::tolower);

  auto it = m_aliases.find(normalizedAlias);
  if (it != m_aliases.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<AliasEntry> AliasIndex::getAliasByAddress(const std::string& address) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  Crypto::Hash addrHash = Crypto::cn_fast_hash(address.data(), address.size());
  auto alias_it = m_addrHashToAlias.find(Common::podToHex(addrHash));
  if (alias_it == m_addrHashToAlias.end()) {
    return std::nullopt;
  }

  auto entry_it = m_aliases.find(alias_it->second);
  if (entry_it != m_aliases.end()) {
    return entry_it->second;
  }
  return std::nullopt;
}

// v2 hash-based query: caller supplies cn_fast_hash(spendKey||viewKey).
// These overloads are preferred over the string-address versions for new code
// because they are independent of the address encoding scheme.
bool AliasIndex::addressHasAliasByHash(const Crypto::Hash& addrHash) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_addrHashToAlias.find(Common::podToHex(addrHash)) != m_addrHashToAlias.end();
}

std::optional<AliasEntry> AliasIndex::getAliasByAddressHash(const Crypto::Hash& addrHash) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto alias_it = m_addrHashToAlias.find(Common::podToHex(addrHash));
  if (alias_it == m_addrHashToAlias.end()) {
    return std::nullopt;
  }
  auto entry_it = m_aliases.find(alias_it->second);
  if (entry_it != m_aliases.end()) {
    return entry_it->second;
  }
  return std::nullopt;
}

std::vector<AliasEntry> AliasIndex::getAllAliases() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<AliasEntry> result;
  result.reserve(m_aliases.size());
  for (const auto& pair : m_aliases) {
    result.push_back(pair.second);
  }
  return result;
}

// ============================================================================
// STATE
// ============================================================================



size_t AliasIndex::size() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_aliases.size();
}

}  // namespace CryptoNote
