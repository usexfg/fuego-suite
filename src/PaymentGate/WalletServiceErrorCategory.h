// Copyright (c) 2017-2025 Fuego Developers
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

#include <string>
#include <system_error>

namespace CryptoNote {
namespace error {

enum class WalletServiceErrorCode {
  WRONG_KEY_FORMAT = 1,
  WRONG_PAYMENT_ID_FORMAT,
  WRONG_HASH_FORMAT,
  OBJECT_NOT_FOUND,
  DUPLICATE_KEY,
  KEYS_NOT_DETERMINISTIC
};

// custom category:
class WalletServiceErrorCategory : public std::error_category {
public:
  static WalletServiceErrorCategory INSTANCE;

  virtual const char* name() const throw() override {
    return "WalletServiceErrorCategory";
  }

  virtual std::error_condition default_error_condition(int ev) const throw() override {
    return std::error_condition(ev, *this);
  }

  virtual std::string message(int ev) const override {
    WalletServiceErrorCode code = static_cast<WalletServiceErrorCode>(ev);

    switch (code) {
      case WalletServiceErrorCode::WRONG_KEY_FORMAT: return "Wrong key format";
      case WalletServiceErrorCode::WRONG_PAYMENT_ID_FORMAT: return "Wrong payment id format";
      case WalletServiceErrorCode::WRONG_HASH_FORMAT: return "Wrong block id format";
      case WalletServiceErrorCode::OBJECT_NOT_FOUND: return "Requested object not found";
      case WalletServiceErrorCode::DUPLICATE_KEY: return "Duplicate key";
      case WalletServiceErrorCode::KEYS_NOT_DETERMINISTIC: return "Keys are non-deterministic";
      default: return "Unknown error";
    }
  }

private:
  WalletServiceErrorCategory() {
  }
};

} //namespace error
} //namespace CryptoNote

inline std::error_code make_error_code(CryptoNote::error::WalletServiceErrorCode e) {
  return std::error_code(static_cast<int>(e), CryptoNote::error::WalletServiceErrorCategory::INSTANCE);
}

namespace std {

template <>
struct is_error_code_enum<CryptoNote::error::WalletServiceErrorCode>: public true_type {};

}
