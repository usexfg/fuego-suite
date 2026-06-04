// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <vector>

extern "C" {
void generate_random_bytes(size_t n, void *result);
}

namespace Randomize
{
    inline void randomBytes(size_t n, uint8_t *result)
    {
        generate_random_bytes(n, result);
    }

    inline std::vector<uint8_t> randomBytes(size_t n)
    {
        std::vector<uint8_t> result(n);
        generate_random_bytes(n, result.data());
        return result;
    }

    template <typename T>
    T randomValue()
    {
        T result;
        generate_random_bytes(sizeof(T), &result);
        return result;
    }

    template <typename T>
    T randomValue(T min, T max)
    {
        T range = max - min + 1;
        if (range == 0) {
            T result;
            generate_random_bytes(sizeof(T), &result);
            return result;
        }
        T result;
        T maxValid = (std::numeric_limits<T>::max)() - ((std::numeric_limits<T>::max)() % range + 1) % range;
        if (maxValid == 0) maxValid = (std::numeric_limits<T>::max)();
        do {
            generate_random_bytes(sizeof(T), &result);
        } while (result > maxValid);
        return min + (result % range);
    }
}
