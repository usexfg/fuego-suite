// Copyright (c) 2017-2025 Elderfire Privacy Council
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

#include <string>
#include <cstdint>

#include <Common/StringView.h>

namespace CryptoNote {

class ISerializer {
public:

  enum SerializerType {
    INPUT,
    OUTPUT
  };

  virtual ~ISerializer() {}

  virtual SerializerType type() const = 0;

  virtual bool beginObject(Common::StringView name) = 0;
  virtual void endObject() = 0;
  virtual bool beginArray(size_t& size, Common::StringView name) = 0;
  virtual void endArray() = 0;

  virtual bool operator()(uint8_t& value, Common::StringView name) = 0;
  virtual bool operator()(int16_t& value, Common::StringView name) = 0;
  virtual bool operator()(uint16_t& value, Common::StringView name) = 0;
  virtual bool operator()(int32_t& value, Common::StringView name) = 0;
  virtual bool operator()(uint32_t& value, Common::StringView name) = 0;
  virtual bool operator()(int64_t& value, Common::StringView name) = 0;
  virtual bool operator()(uint64_t& value, Common::StringView name) = 0;
  virtual bool operator()(double& value, Common::StringView name) = 0;
  virtual bool operator()(bool& value, Common::StringView name) = 0;
  virtual bool operator()(std::string& value, Common::StringView name) = 0;
  
  // read/write binary block
  virtual bool binary(void* value, size_t size, Common::StringView name) = 0;
  virtual bool binary(std::string& value, Common::StringView name) = 0;

  template<typename T>
  bool operator()(T& value, Common::StringView name);
};

template<typename T>
bool ISerializer::operator()(T& value, Common::StringView name) {
  return serialize(value, name, *this);
}

template<typename T>
bool serialize(T& value, Common::StringView name, ISerializer& serializer) {
  if (!serializer.beginObject(name)) {
    return false;
  }

  serialize(value, serializer);
  serializer.endObject();
  return true;
}

template<typename T>
void serialize(T& value, ISerializer& serializer) {
  value.serialize(serializer);
}

#ifdef __clang__
template<> inline
bool ISerializer::operator()(size_t& value, Common::StringView name) {
  return operator()(*reinterpret_cast<uint64_t*>(&value), name);
}
#endif

#define KV_MEMBER(member) s(member, #member);

}
