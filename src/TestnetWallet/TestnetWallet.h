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

#include "SimpleWallet/SimpleWallet.h"

namespace CryptoNote
{
  /************************************************************************/
  /*         TestnetWallet - Extends simple_wallet for testnet            */
  /*    Inherits all mainnet functionality + testnet-specific features    */
  /************************************************************************/
  class testnet_wallet : public simple_wallet {
  public:
    testnet_wallet(System::Dispatcher& dispatcher, const CryptoNote::Currency& currency, Logging::LoggerManager& log);

  protected:
    // Testnet-specific deposit command overrides
    // These add burn, cold support to testnet
    void register_testnet_commands();

  private:
    // Testnet deposit implementations
    bool burn(const std::vector<std::string> &args);
    bool cold(const std::vector<std::string> &args);
    // bool elderking_ceremony(const std::vector<std::string> &args);  // Removed
    // bool unstake(const std::vector<std::string> &args);  // Removed
    bool list_burns(const std::vector<std::string> &args);

    // swapxfg launcher override (passes --testnet)
    bool swap_tui(const std::vector<std::string> &args);
  };
}
