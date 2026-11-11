#include "PasswordContainer.h"

#include <iostream>
#include <memory.h>
#include <stdio.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/mman.h>
#endif

namespace Tools
{
  namespace
  {
    bool is_cin_tty();
  }

  void SecureBuffer::assign(const char* src, size_t len) {
    clear();
    if (len == 0) return;
    m_data = new char[len];
#if !defined(_WIN32)
    mlock(m_data, len);
#endif
    memcpy(m_data, src, len);
    m_size = len;
  }

  void SecureBuffer::assign(const std::string& src) {
    assign(src.data(), src.size());
  }

  void SecureBuffer::clear() {
    if (m_data && m_size > 0) {
      volatile char* p = m_data;
      for (size_t i = 0; i < m_size; ++i) {
        p[i] = 0;
      }
#if !defined(_WIN32)
      munlock(m_data, m_size);
#endif
      delete[] m_data;
    }
    m_data = nullptr;
    m_size = 0;
  }

  PasswordContainer::PasswordContainer()
    : m_empty(true)
  {
  }

  PasswordContainer::PasswordContainer(std::string&& password)
    : m_empty(false)
  {
    m_password.assign(password);
    volatile char* p = const_cast<char*>(password.data());
    for (size_t i = 0; i < password.size(); ++i) p[i] = 0;
  }

  PasswordContainer::PasswordContainer(PasswordContainer&& rhs)
    : m_empty(rhs.m_empty)
    , m_password(std::move(rhs.m_password))
  {
    rhs.m_empty = true;
  }

  PasswordContainer::~PasswordContainer()
  {
    clear();
  }

  void PasswordContainer::clear()
  {
    m_password.clear();
    m_empty = true;
  }

  void PasswordContainer::password(std::string&& val) {
    m_password.assign(val);
    volatile char* p = const_cast<char*>(val.data());
    for (size_t i = 0; i < val.size(); ++i) p[i] = 0;
    m_empty = false;
  }

  bool PasswordContainer::read_password()
  {
    clear();

    bool r;
    if (is_cin_tty())
    {
      std::cout << "password: ";
      r = read_from_tty();
    }
    else
    {
      r = read_from_file();
    }

    if (r)
    {
      m_empty = false;
    }
    else
    {
      clear();
    }

    return r;
  }

  bool PasswordContainer::read_from_file()
  {
    std::string buf;
    buf.reserve(max_password_size);
    for (size_t i = 0; i < max_password_size; ++i)
    {
      char ch = static_cast<char>(std::cin.get());
      if (std::cin.eof() || ch == '\n' || ch == '\r')
      {
        break;
      }
      else if (std::cin.fail())
      {
        return false;
      }
      else
      {
        buf.push_back(ch);
      }
    }

    m_password.assign(buf);
    if (!buf.empty()) {
      volatile char* p = &buf[0];
      for (size_t i = 0; i < buf.size(); ++i) p[i] = 0;
    }
    return true;
  }

#if defined(_WIN32)

  namespace
  {
    bool is_cin_tty()
    {
      return 0 != _isatty(_fileno(stdin));
    }
  }

  bool PasswordContainer::read_from_tty()
  {
    const char BACKSPACE = 8;

    HANDLE h_cin = ::GetStdHandle(STD_INPUT_HANDLE);

    DWORD mode_old;
    ::GetConsoleMode(h_cin, &mode_old);
    DWORD mode_new = mode_old & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    ::SetConsoleMode(h_cin, mode_new);

    bool r = true;
    std::string buf;
    buf.reserve(max_password_size);
    while (buf.size() < max_password_size)
    {
      DWORD read;
      char ch;
      r = (TRUE == ::ReadConsoleA(h_cin, &ch, 1, &read, NULL));
      r &= (1 == read);
      if (!r)
      {
        break;
      }
      else if (ch == '\n' || ch == '\r')
      {
        std::cout << std::endl;
        break;
      }
      else if (ch == BACKSPACE)
      {
        if (!buf.empty())
        {
          buf.back() = '\0';
          buf.resize(buf.size() - 1);
          std::cout << "\b \b";
        }
      }
      else
      {
        buf.push_back(ch);
        std::cout << '*';
      }
    }

    m_password.assign(buf);
    if (!buf.empty()) {
      volatile char* p = &buf[0];
      for (size_t i = 0; i < buf.size(); ++i) p[i] = 0;
    }

    ::SetConsoleMode(h_cin, mode_old);

    return r;
  }

#else

  namespace
  {
    bool is_cin_tty()
    {
      return 0 != isatty(fileno(stdin));
    }

    int getch()
    {
      struct termios tty_old;
      tcgetattr(STDIN_FILENO, &tty_old);

      struct termios tty_new;
      tty_new = tty_old;
      tty_new.c_lflag &= ~(ICANON | ECHO);
      tcsetattr(STDIN_FILENO, TCSANOW, &tty_new);

      int ch = getchar();

      tcsetattr(STDIN_FILENO, TCSANOW, &tty_old);

      return ch;
    }
  }

  bool PasswordContainer::read_from_tty()
  {
    const char BACKSPACE = 127;

    std::string buf;
    buf.reserve(max_password_size);
    while (buf.size() < max_password_size)
    {
      int ch = getch();
      if (EOF == ch)
      {
        m_password.assign(buf);
        if (!buf.empty()) {
          volatile char* p = &buf[0];
          for (size_t i = 0; i < buf.size(); ++i) p[i] = 0;
        }
        return false;
      }
      else if (ch == '\n' || ch == '\r')
      {
        std::cout << std::endl;
        break;
      }
      else if (ch == BACKSPACE)
      {
        if (!buf.empty())
        {
          buf.back() = '\0';
          buf.resize(buf.size() - 1);
          std::cout << "\b \b";
        }
      }
      else
      {
        buf.push_back(ch);
        std::cout << '*';
      }
    }

    m_password.assign(buf);
    if (!buf.empty()) {
      volatile char* p = &buf[0];
      for (size_t i = 0; i < buf.size(); ++i) p[i] = 0;
    }

    return true;
  }

#endif
}
