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

// Generates the XFG keypair that xfg-swapd's swap config expects in
// `xfg_secret_key`. Shared by fire_wallet and test_wallet so both binaries
// print byte-identical output.
//
// Deliberately handled before wallet initialisation: this key belongs to the
// swap daemon, not to any wallet, so it must be obtainable on a machine with
// no wallet file, no password, and no running node.

#pragma once

#include <iostream>
#include <string>

#include "crypto/crypto.h"
#include "Common/StringTools.h"

namespace SwapKeyGen {

// True when this invocation asks for the key generator. Only argv[1] is
// examined, so `fire_wallet --testnet gen_swap_key` does not trigger it.
inline bool isRequested(int argc, char* argv[]) {
  return argc >= 2 && std::string(argv[1]) == "gen_swap_key";
}

inline void print() {
  Crypto::SecretKey secret;
  Crypto::PublicKey publicKey;
  Crypto::generate_keys(publicKey, secret);

  const std::string secretHex = Common::podToHex(secret);
  const std::string publicHex = Common::podToHex(publicKey);

  std::cout <<
    "XFG swap key generated.\n"
    "\n"
    "  xfg_secret_key    " << secretHex << "\n"
    "  public spend key  " << publicHex << "\n"
    "\n"
    "Add this line to your swap config:\n"
    "  \"xfg_secret_key\": \"" << secretHex << "\",\n"
    "\n"
    "Notes:\n"
    "  - Use THIS key, not your main wallet spend key. xfg-swapd signs offers\n"
    "    and XFG swap outputs with it, so a dedicated key limits what a\n"
    "    compromised daemon can move. Fund it only with what swaps need.\n"
    "  - The same key derives the swap database's at-rest encryption key.\n"
    "    Changing it later makes existing swap state unreadable.\n"
    "  - Back it up. Fuego derives the matching view key from the spend key,\n"
    "    so this single value restores the keypair.\n"
    << std::endl;
}

} // namespace SwapKeyGen