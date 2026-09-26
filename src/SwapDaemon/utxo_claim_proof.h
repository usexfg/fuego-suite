#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {

// Check the transaction input outpoint before accepting an HTLC preimage from
// an RPC-supplied raw transaction. The preimage/script parser separately
// checks the redeem path; a matching script alone does not identify this lock.
inline bool spends_utxo_lock_output(const std::vector<uint8_t>& raw_tx,
                                 const std::string& lock_tx_id,
                                 uint32_t lock_vout = 0) {
  if (lock_tx_id.size() != 64 || raw_tx.size() < 5 || raw_tx.size() > 1000000)
    return false;
  auto hex_nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  uint8_t expected_le[32];
  for (size_t i = 0; i < 32; ++i) {
    const int hi = hex_nibble(lock_tx_id[2 * i]);
    const int lo = hex_nibble(lock_tx_id[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    expected_le[31 - i] = static_cast<uint8_t>((hi << 4) | lo);
  }

  size_t pos = 4; // version
  if (pos + 2 <= raw_tx.size() && raw_tx[pos] == 0 && raw_tx[pos + 1] == 1)
    pos += 2; // SegWit marker and flag
  auto read_var_int = [&](uint64_t& value) -> bool {
    if (pos >= raw_tx.size()) return false;
    const uint8_t marker = raw_tx[pos++];
    if (marker < 0xfd) { value = marker; return true; }
    const size_t bytes = marker == 0xfd ? 2 : marker == 0xfe ? 4 : 8;
    if (bytes > raw_tx.size() - pos) return false;
    value = 0;
    for (size_t i = 0; i < bytes; ++i)
      value |= static_cast<uint64_t>(raw_tx[pos++]) << (8 * i);
    return true;
  };

  uint64_t inputs = 0;
  if (!read_var_int(inputs) || inputs == 0 || inputs > 128) return false;
  bool found = false;
  for (uint64_t i = 0; i < inputs; ++i) {
    if (raw_tx.size() - pos < 36) return false;
    bool txid_matches = true;
    for (size_t j = 0; j < 32; ++j)
      if (raw_tx[pos + j] != expected_le[j]) txid_matches = false;
    pos += 32;
    const uint32_t vout = static_cast<uint32_t>(raw_tx[pos]) |
        (static_cast<uint32_t>(raw_tx[pos + 1]) << 8) |
        (static_cast<uint32_t>(raw_tx[pos + 2]) << 16) |
        (static_cast<uint32_t>(raw_tx[pos + 3]) << 24);
    pos += 4;
    if (txid_matches && vout == lock_vout) found = true;
    uint64_t script_length = 0;
    if (!read_var_int(script_length) || script_length > 10000 ||
        script_length > raw_tx.size() - pos ||
        raw_tx.size() - pos - script_length < 4) return false;
    pos += static_cast<size_t>(script_length) + 4; // scriptSig and sequence
  }
  return found;
}

} // namespace XfgSwap
