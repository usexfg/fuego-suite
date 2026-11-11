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

#include "INode.h"

#include <string>

namespace PaymentService {

class NodeFactory {
public:
  static CryptoNote::INode* createNode(const std::string& daemonAddress, uint16_t daemonPort);
  static CryptoNote::INode* createNodeStub();
private:
  NodeFactory();
  ~NodeFactory();

  CryptoNote::INode* getNode(const std::string& daemonAddress, uint16_t daemonPort);

  static NodeFactory factory;
};

} //namespace PaymentService
