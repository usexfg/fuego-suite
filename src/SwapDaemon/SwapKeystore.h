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
#include <string>

namespace XfgSwap {

// Named secrets (chain WIFs and private keys, the XFG swap key) encrypted at
// rest: scrypt(passphrase) → AES-256-GCM over a JSON object, the file header
// bound as associated data. swap_config.json then holds "keystore:<name>"
// references instead of plaintext keys.
using KeystoreSecrets = std::map<std::string, std::string>;

bool loadKeystore(const std::string& path, const std::string& passphrase,
                  KeystoreSecrets& secrets, std::string& error);
// Writes atomically with owner-only permissions.
bool saveKeystore(const std::string& path, const std::string& passphrase,
                  const KeystoreSecrets& secrets, std::string& error);

// XFG_SWAPD_KEYSTORE_PASSPHRASE, or the first line of the file named by
// XFG_SWAPD_KEYSTORE_PASSPHRASE_FILE.
bool keystorePassphraseFromEnvironment(std::string& passphrase, std::string& error);

// Replaces every JSON string literal "keystore:<name>" in `configJson` with
// the named secret. Unknown names are an error; so is a secret that is not
// plain printable ASCII without quotes or backslashes.
bool resolveKeystoreReferences(std::string& configJson, const KeystoreSecrets& secrets,
                               std::string& error);
bool hasKeystoreReferences(const std::string& configJson);

}  // namespace XfgSwap
