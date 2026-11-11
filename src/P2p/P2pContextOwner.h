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

#include <list>
#include <memory>

namespace CryptoNote {

class P2pContext;

class P2pContextOwner {
public:

  typedef std::list<std::unique_ptr<P2pContext>> ContextList;

  P2pContextOwner(P2pContext* ctx, ContextList& contextList);
  P2pContextOwner(P2pContextOwner&& other);
  P2pContextOwner(const P2pContextOwner& other) = delete;
  ~P2pContextOwner();

  P2pContext& get();
  P2pContext* operator -> ();

private:

  ContextList& contextList;
  ContextList::iterator contextIterator;
};

}
