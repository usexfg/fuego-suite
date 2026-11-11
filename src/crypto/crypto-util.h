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

// Copyright (c) 2013-2018
// Frank Denis <j at pureftpd dot org>
// See https://github.com/jedisct1/libsodium/blob/master/LICENSE for details

#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif
// We borrow from https://libsodium.org/
void sodium_memzero(void *pnt, size_t length);
int sodium_compare(const void *a1, const void *a2, size_t length);

#if defined(__cplusplus)
}
#endif
