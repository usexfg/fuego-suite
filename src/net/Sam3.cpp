// Copyright (c) 2017-2026 Fuego Developers
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

#include "Sam3.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <System/TcpConnector.h>
#include <System/Ipv4Address.h>

#include "Common/StringTools.h"

namespace CryptoNote {
namespace net {

namespace {

static const uint32_t SHA256_K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
  0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
  0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
  0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  uint32_t h[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  };

  size_t padded_len = ((len + 9 + 63) / 64) * 64;
  std::vector<uint8_t> padded(padded_len, 0);
  std::memcpy(padded.data(), data, len);
  padded[len] = 0x80;
  uint64_t bit_len = static_cast<uint64_t>(len) * 8;
  for (int i = 0; i < 8; ++i)
    padded[padded_len - 1 - i] = static_cast<uint8_t>(bit_len >> (i * 8));

  for (size_t offset = 0; offset < padded_len; offset += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t(padded[offset + i*4]) << 24) |
             (uint32_t(padded[offset + i*4+1]) << 16) |
             (uint32_t(padded[offset + i*4+2]) << 8) |
              uint32_t(padded[offset + i*4+3]);
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
      uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = hh + S1 + ch + SHA256_K[i] + w[i];
      uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + maj;
      hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
  }

  for (int i = 0; i < 8; ++i) {
    out[i*4]   = (h[i] >> 24) & 0xff;
    out[i*4+1] = (h[i] >> 16) & 0xff;
    out[i*4+2] = (h[i] >> 8) & 0xff;
    out[i*4+3] = h[i] & 0xff;
  }
}

// I2P uses '-' for '+' and '~' for '/' in its base64 encoding
std::string i2p_b64_to_std(const std::string& i2p) {
  std::string out = i2p;
  for (char& c : out) {
    if (c == '-') c = '+';
    else if (c == '~') c = '/';
  }
  return out;
}

} // anonymous namespace

static const char BASE32_ALPHABET[] = "abcdefghijklmnopqrstuvwxyz234567";

static std::string base32_encode(const uint8_t* data, size_t length) {
  std::string result;
  result.reserve((length * 8 + 4) / 5);

  int buffer = 0;
  int bits_left = 0;

  for (size_t i = 0; i < length; ++i) {
    buffer = (buffer << 8) | data[i];
    bits_left += 8;
    while (bits_left >= 5) {
      result.push_back(BASE32_ALPHABET[(buffer >> (bits_left - 5)) & 0x1F]);
      bits_left -= 5;
    }
  }

  if (bits_left > 0) {
    result.push_back(BASE32_ALPHABET[(buffer << (5 - bits_left)) & 0x1F]);
  }

  return result;
}

Sam3Session::Sam3Session(System::Dispatcher& dispatcher,
                         const std::string& sam_host,
                         uint16_t sam_port)
  : m_dispatcher(dispatcher)
  , m_sam_host(sam_host)
  , m_sam_port(sam_port)
{
}

Sam3Session::~Sam3Session() {
  shutdown();
}

std::string Sam3Session::read_line(System::TcpConnection& conn) {
  std::string line;
  char c;
  while (true) {
    size_t n = conn.read(reinterpret_cast<uint8_t*>(&c), 1);
    if (n == 0) throw std::runtime_error("SAM: connection closed");
    if (c == '\n') break;
    line.push_back(c);
  }
  return line;
}

void Sam3Session::write_line(System::TcpConnection& conn, const std::string& line) {
  std::string msg = line + "\n";
  const uint8_t* data = reinterpret_cast<const uint8_t*>(msg.data());
  size_t offset = 0;
  while (offset < msg.size()) {
    offset += conn.write(data + offset, msg.size() - offset);
  }
}

bool Sam3Session::do_hello(System::TcpConnection& conn) {
  write_line(conn, "HELLO VERSION MIN=3.0 MAX=3.3");
  std::string reply = read_line(conn);
  return reply.find("RESULT=OK") != std::string::npos;
}

std::string Sam3Session::destination_to_b32(const std::string& dest_b64) {
  std::string std_b64 = i2p_b64_to_std(dest_b64);
  std::string raw = Common::base64Decode(std_b64);
  if (raw.empty()) {
    return "";
  }

  uint8_t hash[32];
  sha256(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), hash);
  return base32_encode(hash, 32) + ".b32.i2p";
}

bool Sam3Session::create_session(const std::string& session_id) {
  m_session_id = session_id;

  try {
    System::TcpConnector connector(m_dispatcher);
    m_control_conn = connector.connect(System::Ipv4Address(m_sam_host), m_sam_port);

    if (!do_hello(m_control_conn)) {
      return false;
    }

    // Create a STREAM session with a transient destination
    write_line(m_control_conn, "SESSION CREATE STYLE=STREAM ID=" + m_session_id + " DESTINATION=TRANSIENT");
    std::string reply = read_line(m_control_conn);

    if (reply.find("RESULT=OK") == std::string::npos) {
      return false;
    }

    // Extract DESTINATION from reply
    size_t dest_pos = reply.find("DESTINATION=");
    if (dest_pos != std::string::npos) {
      m_destination = reply.substr(dest_pos + 12);
      // Trim whitespace
      while (!m_destination.empty() && (m_destination.back() == '\r' || m_destination.back() == ' ')) {
        m_destination.pop_back();
      }
    }

    // Look up our own .b32.i2p address
    System::TcpConnector connector2(m_dispatcher);
    auto naming_conn = connector2.connect(System::Ipv4Address(m_sam_host), m_sam_port);
    if (do_hello(naming_conn)) {
      write_line(naming_conn, "NAMING LOOKUP NAME=ME");
      std::string naming_reply = read_line(naming_conn);
      // Reply: NAMING REPLY RESULT=OK NAME=ME VALUE=<base64_destination>
      // We need to compute the b32 from the destination
      size_t val_pos = naming_reply.find("VALUE=");
      if (val_pos != std::string::npos) {
        std::string full_dest = naming_reply.substr(val_pos + 6);
        while (!full_dest.empty() && (full_dest.back() == '\r' || full_dest.back() == ' ')) {
          full_dest.pop_back();
        }

        // Compute b32 address: SHA-256 of the decoded destination, then base32-encode
        // For now, use a placeholder approach — the SAM bridge's DEST provides this
        // In practice, i2pd provides the b32 address in session status
        if (m_destination.empty()) {
          m_destination = full_dest;
        }
      }
    }

    if (!m_destination.empty() && m_b32_address.empty()) {
      m_b32_address = destination_to_b32(m_destination);
    }

    m_active = true;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

System::TcpConnection Sam3Session::accept_stream() {
  // Open a new connection to SAM for stream accept
  System::TcpConnector connector(m_dispatcher);
  auto conn = connector.connect(System::Ipv4Address(m_sam_host), m_sam_port);

  if (!do_hello(conn)) {
    throw std::runtime_error("SAM HELLO failed on accept connection");
  }

  write_line(conn, "STREAM ACCEPT ID=" + m_session_id + " SILENT=false");
  std::string reply = read_line(conn);

  if (reply.find("RESULT=OK") == std::string::npos) {
    throw std::runtime_error("SAM STREAM ACCEPT failed: " + reply);
  }

  // The connection is now an incoming I2P stream.
  // The remote destination info may be in the reply if SILENT=false.
  return std::move(conn);
}

void Sam3Session::shutdown() {
  m_active = false;
  // The control connection will be closed when it goes out of scope
}

} // namespace net
} // namespace CryptoNote
