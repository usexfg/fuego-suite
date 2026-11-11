// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include <vector>
#include <map>
#include <string>
#include <ostream>
#include <istream>
#include <ctime>

#include "crypto/hash.h"
#include "crypto/crypto.h"
#include "crypto/chacha8.h"

namespace CryptoNote {
class AccountBase;
class ISerializer;
}

namespace CryptoNote {

class WalletUserTransactionsCache;

// Maker-side pre-lock material for an AFK (non-interactive) swap offer.
// Persisted inside the wallet file (password-encrypted) so a wallet restart
// does not orphan in-flight AFK offers. The secret lets the maker claim the
// counterparty HTLC; the pre-sig lets the maker verify the taker's
// proof-of-claim.
struct AfkLockSecret {
  Crypto::SecretKey secret;
  Crypto::Signature preSig;
  uint64_t amount;
  uint32_t timeout_hours;
  uint8_t pair;
  time_t timestamp;

  AfkLockSecret() : amount(0), timeout_hours(0), pair(0), timestamp(0) {}
  AfkLockSecret(const Crypto::SecretKey& s, const Crypto::Signature& ps, uint64_t a, uint32_t t, uint8_t p)
    : secret(s), preSig(ps), amount(a), timeout_hours(t), pair(p), timestamp(std::time(nullptr)) {}
};

class WalletLegacySerializer {
public:
  WalletLegacySerializer(CryptoNote::AccountBase& account, WalletUserTransactionsCache& transactionsCache,
                         std::map<std::string, AfkLockSecret>& afkLocks);

  // Legacy ctor (no AFK persistence) for callers that do not manage AFK locks.
  WalletLegacySerializer(CryptoNote::AccountBase& account, WalletUserTransactionsCache& transactionsCache);

  void serialize(std::ostream& stream, const std::string& password, bool saveDetailed, const std::string& cache);
  void deserialize(std::istream& stream, const std::string& password, std::string& cache);
  bool deserialize(std::istream& stream, const std::string& password);  

private:
  void saveKeys(CryptoNote::ISerializer& serializer);
  void loadKeys(CryptoNote::ISerializer& serializer);

  // AFK lock secrets: appended as an optional trailing section inside the
  // password-encrypted payload. Old wallets have no section (read gracefully);
  // old binaries ignore trailing bytes they never read.
  void saveAfkLocks(std::string& blob);
  void loadAfkLocks(const std::string& blob);

  Crypto::chacha8_iv encrypt(const std::string& plain, const std::string& password, std::string& cipher);
  void decrypt(const std::string& cipher, std::string& plain, Crypto::chacha8_iv iv, const std::string& password);

  CryptoNote::AccountBase& account;
  WalletUserTransactionsCache& transactionsCache;
  std::map<std::string, AfkLockSecret>* afkLocks;  // may be null (legacy ctor)
  const uint32_t walletSerializationVersion;
};

} //namespace CryptoNote
