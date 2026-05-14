// Copyright (c) 2017-2022, Fuego Developers
// Copyright (c) 2012-2016, CryptoNote Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
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

enum class BlockchainExplorerErrorCodes : int {
  NOT_INITIALIZED = 1,
  ALREADY_INITIALIZED,
  INTERNAL_ERROR,
  REQUEST_ERROR
};

class BlockchainExplorerErrorCategory : public std::error_category {
public:
  static BlockchainExplorerErrorCategory INSTANCE;

  virtual const char* name() const throw() override {
    return "BlockchainExplorerErrorCategory";
  }

  virtual std::error_condition default_error_condition(int ev) const throw() override {
    return std::error_condition(ev, *this);
  }

  virtual std::string message(int ev) const override {
    switch (ev) {
      case static_cast<int>(BlockchainExplorerErrorCodes::NOT_INITIALIZED):     return "Object was not initialized";
      case static_cast<int>(BlockchainExplorerErrorCodes::ALREADY_INITIALIZED): return "Object has been already initialized";
      case static_cast<int>(BlockchainExplorerErrorCodes::INTERNAL_ERROR):      return "Internal error";
      case static_cast<int>(BlockchainExplorerErrorCodes::REQUEST_ERROR):       return "Error in request parameters";
      default:                                                                  return "Unknown error";
    }
  }

private:
  BlockchainExplorerErrorCategory() {
  }
};

} //namespace error
} //namespace CryptoNote

inline std::error_code make_error_code(CryptoNote::error::BlockchainExplorerErrorCodes e) {
  return std::error_code(static_cast<int>(e), CryptoNote::error::BlockchainExplorerErrorCategory::INSTANCE);
}

