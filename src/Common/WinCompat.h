// Copyright (c) 2017-2026 Fuego Developers
//
// Minimal cross-platform socket compatibility.
// On POSIX this header is a no-op; on Windows it pulls in winsock2 and
// provides the handful of POSIX→Win32 shims the SwapDaemon needs.

#pragma once

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")

  typedef SSIZE_T ssize_t;

  // POSIX close() → closesocket()
  #define close(x) closesocket(x)

  // POSIX shutdown() constants
  #define SHUT_RDWR SD_BOTH

  // MSG_NOSIGNAL is not available on Windows (send never generates SIGPIPE)
  #ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
  #endif

  // struct dirent / opendir / readdir / closedir — replaced with
  // <filesystem> helpers; see WinCompat::listJsonFiles() below.
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
#endif
