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

#include "Common/StringTools.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"


namespace CryptoNote {
  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  template<class t_array>
  struct array_hasher
  {
    size_t operator()(const t_array& val) const
    {
      return boost::hash_range(&val.data[0], &val.data[sizeof(val.data)]);
    }
  };

  /************************************************************************/
  /* CryptoNote helper functions                                          */
  /************************************************************************/
  uint64_t getPenalizedAmount(uint64_t amount, size_t medianSize, size_t currentBlockSize);
  std::string getAccountAddressAsStr(uint64_t prefix, const AccountPublicAddress& adr);
  bool parseAccountAddressString(uint64_t& prefix, AccountPublicAddress& adr, const std::string& str);
  bool is_coinbase(const Transaction& tx);

  bool operator ==(const CryptoNote::Transaction& a, const CryptoNote::Transaction& b);
  bool operator ==(const CryptoNote::Block& a, const CryptoNote::Block& b);
}

template <class T>
std::ostream &print256(std::ostream &o, const T &v) {
  return o << "<" << Common::podToHex(v) << ">";
}

bool parse_hash256(const std::string& str_hash, Crypto::Hash& hash);

namespace Crypto {
  inline std::ostream &operator <<(std::ostream &o, const Crypto::PublicKey &v) { return print256(o, v); }
  inline std::ostream &operator <<(std::ostream &o, const Crypto::SecretKey &v) { return print256(o, v); }
  inline std::ostream &operator <<(std::ostream &o, const Crypto::KeyDerivation &v) { return print256(o, v); }
  inline std::ostream &operator <<(std::ostream &o, const Crypto::KeyImage &v) { return print256(o, v); }
  inline std::ostream &operator <<(std::ostream &o, const Crypto::Signature &v) { return print256(o, v); }
  inline std::ostream &operator <<(std::ostream &o, const Crypto::Hash &v) { return print256(o, v); }
}
