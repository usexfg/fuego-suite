// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include <cstddef>
#include <cstdint>
#include <vector>
#include <CryptoNote.h>

using namespace CryptoNote;

namespace Common
{

    template <class It>
    inline BinaryArray::iterator append(BinaryArray &ba, It be, It en)
    {
        return ba.insert(ba.end(), be, en);
    }
    inline BinaryArray::iterator append(BinaryArray &ba, size_t add, BinaryArray::value_type va)
    {
        return ba.insert(ba.end(), add, va);
    }
    inline BinaryArray::iterator append(BinaryArray &ba, const BinaryArray &other)
    {
        return ba.insert(ba.end(), other.begin(), other.end() );
    }

} // namespace Common
