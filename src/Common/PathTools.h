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

namespace Common {

std::string NativePathToGeneric(const std::string& nativePath);

std::string GetPathDirectory(const std::string& path);
std::string GetPathFilename(const std::string& path);
void SplitPath(const std::string& path, std::string& directory, std::string& filename);

std::string CombinePath(const std::string& path1, const std::string& path2);
std::string GetExtension(const std::string& path);
std::string RemoveExtension(const std::string& path);
std::string ReplaceExtenstion(const std::string& path, const std::string& extension);
bool HasParentPath(const std::string& path);

}
