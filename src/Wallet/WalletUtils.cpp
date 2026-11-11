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

#include "WalletUtils.h"

#include "CryptoNote.h"
#include "crypto/crypto.h"
#include "Wallet/WalletErrors.h"

namespace CryptoNote {

bool validateAddress(const std::string& address, const CryptoNote::Currency& currency) {
  CryptoNote::AccountPublicAddress ignore;
  return currency.parseAccountAddressString(address, ignore);
}

void throwIfKeysMissmatch(const Crypto::SecretKey& secretKey, const Crypto::PublicKey& expectedPublicKey, const std::string& message) {
  Crypto::PublicKey pub;
  bool r = Crypto::secret_key_to_public_key(secretKey, pub);
  if (!r || expectedPublicKey != pub) {
    throw std::system_error(make_error_code(CryptoNote::error::WRONG_PASSWORD), message);
  }
}

}
