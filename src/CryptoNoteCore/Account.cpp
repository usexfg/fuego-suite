// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>

#include "Account.h"
#include "CryptoNoteSerialization.h"
#include "crypto/keccak.c"

namespace CryptoNote {
//-----------------------------------------------------------------
AccountBase::AccountBase() {
  setNull();
}
//-----------------------------------------------------------------
void AccountBase::setNull() {
  m_keys = AccountKeys();
}
//-----------------------------------------------------------------
void AccountBase::generateViewFromSpend(Crypto::SecretKey &spend, Crypto::SecretKey &viewSecret, Crypto::PublicKey &viewPublic) {
  Crypto::SecretKey viewKeySeed;
  keccak((uint8_t *)&spend, sizeof(spend), (uint8_t *)&viewKeySeed, sizeof(viewKeySeed));
  Crypto::generate_keys_from_seed(viewPublic, viewSecret, viewKeySeed);
}
//-----------------------------------------------------------------
void AccountBase::generate() {
  Crypto::generate_keys(m_keys.address.spendPublicKey, m_keys.spendSecretKey);
  generateViewFromSpend(m_keys.spendSecretKey, m_keys.viewSecretKey, m_keys.address.viewPublicKey);
  m_creation_timestamp = time(NULL);
}
//-----------------------------------------------------------------
const AccountKeys &AccountBase::getAccountKeys() const {
  return m_keys;
}

void AccountBase::setAccountKeys(const AccountKeys &keys) {
  m_keys = keys;
}
//-----------------------------------------------------------------

void AccountBase::serialize(ISerializer &s) {
  s(m_keys, "m_keys");
  s(m_creation_timestamp, "m_creation_timestamp");
}
}
