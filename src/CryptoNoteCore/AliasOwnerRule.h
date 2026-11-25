// Copyright (c) 2017-2026 Fuego Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <string>

#include "CryptoNoteBasic.h"
#include "crypto/hash.h"

namespace CryptoNote {

class Currency;

// The 0xEA / 0xED identity of an owner: cn_fast_hash(spend key || view key).
Crypto::Hash aliasOwnerHash(const AccountPublicAddress& owner);

// F9: from v11, an alias registration names a funsa subaddress of this
// network as owner with addressHash bound to that owner's keys, and an alias
// transfer moves the alias to a funsa subaddress. Earlier blocks are unchanged.
bool validateAliasOwners(const Transaction& tx, uint8_t blockMajorVersion,
                         const Currency& currency, std::string* error);

}
