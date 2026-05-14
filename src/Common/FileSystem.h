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

#include <string>
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
    #ifdef _MSC_VER
        #include <direct.h>
    #else
        #include <sys/stat.h>
    #endif
#endif

namespace Common {

/**
 * Check if a file exists
 * @param path File path to check
 * @return true if file exists
 */
inline bool fileExists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

/**
 * Create a directory
 * @param path Directory path to create
 * @return true if directory was created or already exists
 */
inline bool createDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    
    // Try to create the directory
    #ifdef _WIN32
        #ifdef _MSC_VER
            return _mkdir(path.c_str()) == 0;
        #else
            return mkdir(path.c_str()) == 0;
        #endif
    #else
        return mkdir(path.c_str(), 0755) == 0;
    #endif
}

} // namespace Common
