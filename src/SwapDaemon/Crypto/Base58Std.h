// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Standard base58 encode/decode using the Bitcoin/Solana alphabet
// (123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz).
// This is NOT the CryptoNote block-base58 — do not reuse Common/Base58 for
// Solana keys.

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace XfgSwap {

class Base58Std {
public:
  static std::vector<uint8_t> decode(const std::string& encoded);
  static std::string encode(const std::vector<uint8_t>& data);
};

} // namespace XfgSwap
