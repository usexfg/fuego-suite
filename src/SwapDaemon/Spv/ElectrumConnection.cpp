// Copyright (c) 2017-2026 Fuego Developers
//
// TCP / optional TLS JSON-RPC client for the Electrum protocol.

#include "SwapDaemon/Spv/ElectrumConnection.h"

#include "Common/JsonValue.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cctype>

#include <cerrno>
#include <cstring>
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#endif

#include <sstream>
#include <stdexcept>

namespace XfgSwap {

ElectrumConnection::ElectrumConnection() {}

ElectrumConnection::~ElectrumConnection() {
  disconnect();
}

namespace {

bool isHexDigest(const std::string& s) {
  return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return std::isxdigit(c) != 0;
  });
}

std::string lowercase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

bool isLoopbackHost(const std::string& host) {
  return host == "localhost" || host == "::1" || host.compare(0, 4, "127.") == 0;
}

bool parseElectrumEndpoint(const std::string& spec, ElectrumEndpoint& endpoint) {
  ElectrumEndpoint parsed;
  std::string rest = spec;
  bool explicitScheme = false;
  if (rest.compare(0, 6, "ssl://") == 0) {
    rest = rest.substr(6);
    explicitScheme = true;
    parsed.useTls = true;
  } else if (rest.compare(0, 6, "tcp://") == 0) {
    rest = rest.substr(6);
    explicitScheme = true;
    parsed.useTls = false;
  }
  const size_t hash = rest.find('#');
  if (hash != std::string::npos) {
    const std::string fragment = rest.substr(hash + 1);
    rest = rest.substr(0, hash);
    if (fragment.compare(0, 7, "sha256=") != 0 || !isHexDigest(fragment.substr(7))) return false;
    parsed.pinnedSha256Hex = lowercase(fragment.substr(7));
  }
  const size_t colon = rest.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= rest.size()) return false;
  parsed.host = rest.substr(0, colon);
  if (parsed.host.size() > 2 && parsed.host.front() == '[' && parsed.host.back() == ']')
    parsed.host = parsed.host.substr(1, parsed.host.size() - 2);
  const std::string portText = rest.substr(colon + 1);
  if (portText.size() > 5 || !std::all_of(portText.begin(), portText.end(),
                                          [](unsigned char c) { return std::isdigit(c) != 0; }))
    return false;
  const unsigned long port = std::stoul(portText);
  if (port == 0 || port > 65535) return false;
  parsed.port = static_cast<uint16_t>(port);
  if (!explicitScheme) parsed.useTls = !isLoopbackHost(parsed.host);
  if (!parsed.useTls && (!isLoopbackHost(parsed.host) || !parsed.pinnedSha256Hex.empty()))
    return false;
  endpoint = parsed;
  return true;
}

bool ElectrumConnection::peerCertificateMatchesPin(const std::string& pinnedSha256Hex) const {
  X509* cert = SSL_get_peer_certificate(static_cast<SSL*>(m_ssl));
  if (!cert) return false;
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digestLength = 0;
  const bool hashed = X509_digest(cert, EVP_sha256(), digest, &digestLength) == 1;
  X509_free(cert);
  if (!hashed || digestLength != 32) return false;
  static const char digits[] = "0123456789abcdef";
  std::string hex;
  for (unsigned int i = 0; i < digestLength; ++i) {
    hex.push_back(digits[digest[i] >> 4]);
    hex.push_back(digits[digest[i] & 0x0F]);
  }
  return hex == pinnedSha256Hex;
}

bool ElectrumConnection::sslHandshake(const ElectrumEndpoint& endpoint) {
  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx) return false;
  m_sslCtx = ctx;
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  const bool pinned = !endpoint.pinnedSha256Hex.empty();
  if (pinned) {
    // A pinned leaf replaces the CA chain: self-signed Electrum servers are
    // accepted only when their exact certificate matches.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
  } else {
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) return false;
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
  }
  SSL* ssl = SSL_new(ctx);
  if (!ssl) return false;
  m_ssl = ssl;
  if (SSL_set_tlsext_host_name(ssl, endpoint.host.c_str()) != 1) return false;
  if (!pinned && SSL_set1_host(ssl, endpoint.host.c_str()) != 1) return false;
  SSL_set_fd(ssl, m_fd);
  if (SSL_connect(ssl) != 1) return false;
  if (pinned) return peerCertificateMatchesPin(endpoint.pinnedSha256Hex);
  return SSL_get_verify_result(ssl) == X509_V_OK;
}

ssize_t ElectrumConnection::sslWrite(const void* buf, size_t len) {
  if (!m_ssl) return -1;
  return SSL_write(static_cast<SSL*>(m_ssl), buf, static_cast<int>(len));
}

ssize_t ElectrumConnection::sslRead(void* buf, size_t len) {
  if (!m_ssl) return -1;
  return SSL_read(static_cast<SSL*>(m_ssl), buf, static_cast<int>(len));
}

bool ElectrumConnection::connect(const ElectrumEndpoint& endpoint) {
  disconnect();
  m_useTls = endpoint.useTls;
  const std::string& host = endpoint.host;
  const uint16_t port = endpoint.port;

  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  std::string portStr = std::to_string(port);
  struct addrinfo* res = nullptr;
  int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
  if (rc != 0 || !res) {
    return false;
  }

  m_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (m_fd < 0) {
    freeaddrinfo(res);
    return false;
  }

#ifdef _WIN32
  u_long nonBlocking = 1;
  ioctlsocket(m_fd, FIONBIO, &nonBlocking);
  rc = ::connect(m_fd, res->ai_addr, static_cast<int>(res->ai_addrlen));
  freeaddrinfo(res);
  if (rc < 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
    ::close(m_fd);
    m_fd = -1;
    return false;
  }
  if (rc < 0) {
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(m_fd, &writeSet);
    struct timeval tv {};
    tv.tv_sec = m_connectTimeout;
    tv.tv_usec = 0;
    int selectRc = select(0, nullptr, &writeSet, nullptr, &tv);
    if (selectRc <= 0) {
      ::close(m_fd);
      m_fd = -1;
      return false;
    }
    int sockErr = 0;
    int errLen = sizeof(sockErr);
    getsockopt(m_fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sockErr), &errLen);
    if (sockErr != 0) {
      ::close(m_fd);
      m_fd = -1;
      return false;
    }
  }
  nonBlocking = 0;
  ioctlsocket(m_fd, FIONBIO, &nonBlocking);
#else
  int flags = fcntl(m_fd, F_GETFL, 0);
  fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

  rc = ::connect(m_fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);

  if (rc < 0 && errno != EINPROGRESS) {
    ::close(m_fd);
    m_fd = -1;
    return false;
  }

  if (rc < 0) {
    struct pollfd pfd {};
    pfd.fd = m_fd;
    pfd.events = POLLOUT;
    int pollRc = poll(&pfd, 1, static_cast<int>(m_connectTimeout * 1000));
    if (pollRc <= 0) {
      ::close(m_fd);
      m_fd = -1;
      return false;
    }
    int sockErr = 0;
    socklen_t errLen = sizeof(sockErr);
    getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &sockErr, &errLen);
    if (sockErr != 0) {
      ::close(m_fd);
      m_fd = -1;
      return false;
    }
  }

  fcntl(m_fd, F_SETFL, flags);
#endif

  struct timeval tv {};
  tv.tv_sec = m_readTimeout;
  setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
  setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

  if (m_useTls && !sslHandshake(endpoint)) {
    disconnect();
    return false;
  }

  return true;
}

void ElectrumConnection::disconnect() {
  if (m_ssl) {
    SSL_shutdown(static_cast<SSL*>(m_ssl));
    SSL_free(static_cast<SSL*>(m_ssl));
    m_ssl = nullptr;
  }
  if (m_sslCtx) {
    SSL_CTX_free(static_cast<SSL_CTX*>(m_sslCtx));
    m_sslCtx = nullptr;
  }
  if (m_fd >= 0) {
    ::shutdown(m_fd, SHUT_RDWR);
    ::close(m_fd);
    m_fd = -1;
  }
  m_useTls = false;
}

bool ElectrumConnection::isConnected() const {
  return m_fd >= 0;
}

std::string ElectrumConnection::call(const std::string& method, const std::string& paramsJson) {
  if (m_fd < 0) {
    return "";
  }

  ++m_callId;

  std::ostringstream req;
  req << "{\"jsonrpc\":\"2.0\",\"id\":" << m_callId
      << ",\"method\":\"" << method << "\""
      << ",\"params\":" << paramsJson << "}\n";

  std::string request = req.str();

  size_t sent = 0;
  while (sent < request.size()) {
    ssize_t n = m_useTls
        ? sslWrite(request.data() + sent, request.size() - sent)
        : ::send(m_fd, request.data() + sent, static_cast<int>(request.size() - sent), MSG_NOSIGNAL);
    if (n < 0) {
      return "";
    }
    sent += static_cast<size_t>(n);
  }

  static constexpr size_t MAX_RESPONSE_SIZE = 1024 * 1024;
  std::string response;
  while (true) {
    char buf[4096];
    ssize_t n = m_useTls
        ? sslRead(buf, sizeof(buf) - 1)
        : ::recv(m_fd, buf, static_cast<int>(sizeof(buf) - 1), 0);
    if (n < 0) {
      return "";
    }
    if (n == 0) {
      return "";
    }
    buf[n] = '\0';
    response.append(buf, static_cast<size_t>(n));
    if (response.size() > MAX_RESPONSE_SIZE) {
      return "";
    }

    auto nl = response.find('\n');
    if (nl != std::string::npos) {
      response.resize(nl);
      break;
    }
  }

  try {
    Common::JsonValue json = Common::JsonValue::fromString(response);

    if (!json.contains("result")) {
      return "";
    }

    const auto& result = json("result");
    if (result.isString()) {
      return result.getString();
    }

    return result.toString();
  } catch (const std::exception&) {
    return "";
  }
}

void ElectrumConnection::setConnectTimeout(uint32_t seconds) {
  m_connectTimeout = seconds;
}

void ElectrumConnection::setReadTimeout(uint32_t seconds) {
  m_readTimeout = seconds;
}

} // namespace XfgSwap
