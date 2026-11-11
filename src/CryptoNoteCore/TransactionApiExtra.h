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

#include "CryptoNoteFormatUtils.h"
#include "TransactionExtra.h"

namespace CryptoNote {

  class TransactionExtra {
  public:
    TransactionExtra() {}
    TransactionExtra(const std::vector<uint8_t>& extra) {
      parse(extra);        
    }

    bool parse(const std::vector<uint8_t>& extra) {
      fields.clear();
      return CryptoNote::parseTransactionExtra(extra, fields);
    }

    template <typename T>
    bool get(T& value) const {
      auto it = find(typeid(T));
      if (it == fields.end()) {
        return false;
      }
      value = boost::get<T>(*it);
      return true;
    }

    template <typename T>
    void set(const T& value) {
      auto it = find(typeid(T));
      if (it != fields.end()) {
        *it = value;
      } else {
        fields.push_back(value);
      }
    }

    template <typename T>
    void append(const T& value) {
      fields.push_back(value);
    }

    bool getPublicKey(Crypto::PublicKey& pk) const {
      CryptoNote::TransactionExtraPublicKey extraPk;
      if (!get(extraPk)) {
        return false;
      }
      pk = extraPk.publicKey;
      return true;
    }

    std::vector<uint8_t> serialize() const {
      std::vector<uint8_t> extra;
      writeTransactionExtra(extra, fields);
      return extra;
    }

  private:

    std::vector<CryptoNote::TransactionExtraField>::const_iterator find(const std::type_info& t) const {
      return std::find_if(fields.begin(), fields.end(), [&t](const CryptoNote::TransactionExtraField& f) { return t == f.type(); });
    }

    std::vector<CryptoNote::TransactionExtraField>::iterator find(const std::type_info& t) {
      return std::find_if(fields.begin(), fields.end(), [&t](const CryptoNote::TransactionExtraField& f) { return t == f.type(); });
    }

    std::vector<CryptoNote::TransactionExtraField> fields;
  };

}
