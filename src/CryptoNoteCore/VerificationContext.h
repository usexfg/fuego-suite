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
namespace CryptoNote
{
  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct tx_verification_context
  {
    bool m_should_be_relayed;
    bool m_verification_failed; //bad tx, should drop connection
    bool m_verification_impossible; //the transaction is related with an alternative blockchain
    bool m_added_to_pool;
    bool m_tx_fee_too_small;
  };

  struct block_verification_context
  {
    bool m_added_to_main_chain;
    bool m_verification_failed; //bad block, should drop connection
    bool m_marked_as_orphaned;
    bool m_already_exists;
    bool m_switched_to_alt_chain;
  };
}
