// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include <CryptoTypes.h>

namespace CryptoNote {

struct VaultKeypair {
    Crypto::SecretKey spendKey;
    Crypto::PublicKey  spendPub;
    Crypto::SecretKey  viewKey;
    Crypto::PublicKey  viewPub;
};

VaultKeypair deriveVaultKeys(const Crypto::Hash& genesisHash);

bool isVaultViewKey(const Crypto::PublicKey& key, const Crypto::PublicKey& vaultViewPub);

} // namespace CryptoNote
