// Copyright (c) 2012-2018 The CryptoNote developers
// Copyright (c) 2017-2022 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful- but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You are encouraged to redistribute it and/or modify it
// under the terms of the GNU General Public License v3 or later
// versions as published by the Free Software Foundation.
// You should receive a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>

#pragma once

#include <sstream>

namespace CryptoNote {

class IStreamSerializable {

public:
  virtual void save(std::ostream& os) = 0;
  virtual void load(std::istream& in) = 0;
};

}
