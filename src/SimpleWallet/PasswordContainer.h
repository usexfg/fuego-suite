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

#include <string>
#include <vector>
#include <cstdint>

namespace Tools
{
  class SecureBuffer {
  public:
    SecureBuffer() = default;
    ~SecureBuffer() { clear(); }

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    SecureBuffer(SecureBuffer&& other) noexcept
      : m_data(other.m_data), m_size(other.m_size) {
      other.m_data = nullptr;
      other.m_size = 0;
    }

    SecureBuffer& operator=(SecureBuffer&& other) noexcept {
      if (this != &other) {
        clear();
        m_data = other.m_data;
        m_size = other.m_size;
        other.m_data = nullptr;
        other.m_size = 0;
      }
      return *this;
    }

    void assign(const char* src, size_t len);
    void assign(const std::string& src);
    void clear();
    bool empty() const { return m_size == 0; }
    const char* data() const { return m_data ? m_data : ""; }
    size_t size() const { return m_size; }
    std::string toString() const { return std::string(data(), size()); }

  private:
    char* m_data = nullptr;
    size_t m_size = 0;
  };

  class PasswordContainer
  {
  public:
    static const size_t max_password_size = 1024;

    PasswordContainer();
    PasswordContainer(std::string&& password);
    PasswordContainer(PasswordContainer&& rhs);
    ~PasswordContainer();

    void clear();
    bool empty() const { return m_empty; }
    const std::string password() const { return m_password.toString(); }
    void password(std::string&& val);
    bool read_password();

  private:
    bool read_from_file();
    bool read_from_tty();

  private:
    bool m_empty;
    SecureBuffer m_password;
  };
}
