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

#if defined(__GNUC__)
#define INITIALIZER(name) __attribute__((constructor(101))) static void name(void)
#define FINALIZER(name) __attribute__((destructor(101))) static void name(void)
#define REGISTER_FINALIZER(name) ((void) 0)

#elif defined(_MSC_VER)
#include <assert.h>
#include <stdlib.h>
// http://stackoverflow.com/questions/1113409/attribute-constructor-equivalent-in-vc
// http://msdn.microsoft.com/en-us/library/bb918180.aspx
#pragma section(".CRT$XCT", read)
#define INITIALIZER(name) \
  static void __cdecl name(void); \
  __declspec(allocate(".CRT$XCT")) void (__cdecl *const _##name)(void) = &name; \
  static void __cdecl name(void)
#define FINALIZER(name) \
  static void __cdecl name(void)
#define REGISTER_FINALIZER(name) \
  do { \
    int _res = atexit(name); \
    assert(_res == 0); \
  } while (0);

#else
#error Unsupported compiler
#endif
