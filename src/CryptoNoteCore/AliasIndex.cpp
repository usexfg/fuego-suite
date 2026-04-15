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

namespace CryptoNote {

AliasIndex::AliasIndex() {
  reserveDevTeamAliases();
}

AliasIndex::~AliasIndex() {}

void AliasIndex::reserveDevTeamAliases() {
  // Reserve dev team aliases at genesis (block 0)
  // These are permanently owned by the Fuego Developer Fund address
  const std::string devAddress = CryptoNote::FUEGO_DEV_FUND_ADDRESS;

  struct ReservedAlias {
    std::string name;
    uint8_t type;  // 0 = Elderfier, 1 = Regular
  };

  const ReservedAlias reserved[] = {
    { "FUEGOXFG", 0 },
    { "fuegoxfg", 1 },
    { "FUEGODEV", 0 },
    { "fuegodev", 1 },
  };

  for (const auto& r : reserved) {
    AliasEntry entry;
    entry.alias = r.name;
    entry.ownerAddress = "";  // Not stored on-chain for privacy
    entry.aliasHash = Crypto::cn_fast_hash(r.name.data(), r.name.size());
    entry.addressHash = Crypto::cn_fast_hash(devAddress.data(), devAddress.size());
    entry.aliasType = r.type;
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

  // Special handling for case-sensitive aliases that cannot coexist
  if (entry.alias == "winslayer" || entry.alias == "WINSLAYER" ||
      entry.alias == "galapagos" || entry.alias == "GALAPAGOS") {
    // Check if the opposite case version already exists
    std::string opposite_case;
    if (entry.alias == "winslayer") {
      opposite_case = "WINSLAYER";
    } else if (entry.alias == "WINSLAYER") {
      opposite_case = "winslayer";
    } else if (entry.alias == "LOUDMINING") {
      // LOUDMINING has no lowercase counterpart
      opposite_case = "";
    } else if (entry.alias == "galapagos") {
      opposite_case = "GALAPAGOS";
    } else if (entry.alias == "GALAPAGOS") {
      opposite_case = "galapagos";
    }

    // Check if opposite case version already exists
    auto it = m_aliases.find(opposite_case);
    if (it != m_aliases.end()) {
      return false;  // Opposite case version already registered
    }

    // Also check if same case version already exists
    if (m_aliases.find(entry.alias) != m_aliases.end()) {
      return false;  // Same case version already registered
    }
  } else {
    // Regular alias handling
    if (m_aliases.find(entry.alias) != m_aliases.end()) {
      return false;  // Alias already taken
    }
  }

  // Check address hash does not already have an alias (no raw address stored)
  std::string addrHashHex = Common::podToHex(entry.addressHash);
  if (m_addrHashToAlias.find(addrHashHex) != m_addrHashToAlias.end()) {
    return false;  // Address already has an alias
  }

  // Validate alias format based on type
  if (entry.aliasType == 0) {
    return false;  // Elderfier aliases no longer supported
  } else if (entry.aliasType == 1) {
    if (!isValidRegularAlias(entry.alias)) {
      return false;
    }
  } else {
    return false;  // Unknown alias type
  }

  // Store the alias — reverse map uses addressHash hex, not raw address
  m_aliases[entry.alias] = entry;
  m_addrHashToAlias[addrHashHex] = entry.alias;
  return true;
}

bool AliasIndex::voidAlias(const std::string& ownerAddress) {
  std::lock_guard<std::mutex> lock(m_mutex);

  Crypto::Hash addrHash = Crypto::cn_fast_hash(ownerAddress.data(), ownerAddress.size());
  std::string addrHashHex = Common::podToHex(addrHash);

  auto alias_it = m_addrHashToAlias.find(addrHashHex);
  if (alias_it == m_addrHashToAlias.end()) {
    return false;  // No alias for this address
  }

  std::string aliasName = alias_it->second;
  m_aliases.erase(aliasName);
  m_addrHashToAlias.erase(alias_it);
  return true;
}

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

  auto it = m_aliases.find(alias);
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
