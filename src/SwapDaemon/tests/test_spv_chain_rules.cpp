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

// Vectors are copied from the reference implementations:
//   Bitcoin Core v28.1 src/test/pow_tests.cpp (2016-block retarget)
//   Litecoin Core v0.21.4 src/test/scrypt_tests.cpp (scrypt proof of work)
//   Bitcoin Cash Node v28.0.1 src/test/pow_tests.cpp calculate_asert_test (ASERT)

#include "SwapDaemon/Spv/SpvChainRules.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label.c_str());
  }
}

static std::vector<uint8_t> fromHex(const std::string& hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2)
    out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return out;
}

static std::string displayHex(std::vector<uint8_t> le) {
  std::reverse(le.begin(), le.end());
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (uint8_t b : le) {
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 15]);
  }
  return out;
}

static void testCompactCodec() {
  PowTarget target;
  check(PowTarget::fromCompact(0x1d00ffff, target) && target.toCompact() == 0x1d00ffff, "1d00ffff round-trips");
  check(!PowTarget::fromCompact(0x04923456, target), "negative compact refused");
  check(!PowTarget::fromCompact(0xff123456, target), "overflowing compact refused");
  check(!PowTarget::fromCompact(0x01003456, target), "zero target refused");
  check(PowTarget::fromCompactUnchecked(0x05009234).toCompact() == 0x05009234, "0x05009234 round-trips");
  check(PowTarget::fromCompactUnchecked(0x01120000).toCompact() == 0x01120000, "small compact round-trips");
  check(SpvChainRules::bitcoinMainnet().powLimit.toCompact() == 0x1d00ffff, "BTC powLimit compact");
}

static void testBitcoinRetarget() {
  const SpvChainRules btc = SpvChainRules::bitcoinMainnet();
  struct Case { int64_t firstTime; int64_t lastTime; uint32_t lastBits; uint32_t expected; const char* name; } cases[] = {
    {1261130161, 1262152739, 0x1d00ffff, 0x1d00d86a, "get_next_work"},
    {1231006505, 1233061996, 0x1d00ffff, 0x1d00ffff, "get_next_work_pow_limit"},
    {1279008237, 1279297671, 0x1c05a3f4, 0x1c0168fd, "get_next_work_lower_limit_actual"},
    {1263163443, 1269211443, 0x1c387f6f, 0x1d00e1fd, "get_next_work_upper_limit_actual"},
  };
  for (const auto& c : cases) {
    check(retargetBits(btc, c.lastBits, c.lastTime - c.firstTime) == c.expected, std::string("BTC ") + c.name);
    check(permittedTransition(btc, 2016 * 30, c.lastBits, c.expected), std::string("BTC permitted ") + c.name);
  }
  check(!permittedTransition(btc, 68544, 0x1c05a3f4, 0x1c0168fd - 1), "BTC below the hardest bound refused");
  check(!permittedTransition(btc, 46368, 0x1c387f6f, 0x1d00e1fd + 1), "BTC above the easiest bound refused");
  check(!permittedTransition(btc, 46367, 0x1c387f6f, 0x1c387f70), "BTC nBits change between retargets refused");
}

static void testLitecoinScrypt() {
  const SpvChainRules ltc = SpvChainRules::litecoinMainnet();
  const std::pair<const char*, const char*> vectors[] = {
    {"020000004c1271c211717198227392b029a64a7971931d351b387bb80db027f270411e398a07046f7d4a08dd815412a8712f874a7ebf0507e3878bd24e20a3b73fd750a667d2f451eac7471b00de6659",
     "00000000002bef4107f882f6115e0b01f348d21195dacd3582aa2dabd7985806"},
    {"0200000011503ee6a855e900c00cfdd98f5f55fffeaee9b6bf55bea9b852d9de2ce35828e204eef76acfd36949ae56d1fbe81c1ac9c0209e6331ad56414f9072506a77f8c6faf551eac7471b00389d01",
     "00000000003a0d11bdd5eb634e08b7feddcfbbf228ed35d250daf19f1c88fc94"},
    {"02000000a72c8a177f523946f42f22c3e86b8023221b4105e8007e59e81f6beb013e29aaf635295cb9ac966213fb56e046dc71df5b3f7f67ceaeab24038e743f883aff1aaafaf551eac7471b0166249b",
     "00000000000b40f895f288e13244728a6c2d9d59d8aff29c65f8dd5114a8ca81"},
    {"010000007824bc3a8a1b4628485eee3024abd8626721f7f870f8ad4d2f33a27155167f6a4009d1285049603888fe85a84b6c803a53305a8d497965a5e896e1a00568359589faf551eac7471b0065434e",
     "00000000003007005891cd4923031e99d8e8d72f6e8e7edc6a86181897e105fe"},
    {"0200000050bfd4e4a307a8cb6ef4aef69abc5c0f2d579648bd80d7733e1ccc3fbc90ed664a7f74006cb11bde87785f229ecd366c2d4e44432832580e0608c579e4cb76f383f7f551eac7471b00c36982",
     "000000000018f0b426a4afc7130ccb47fa02af730d345b4fe7c7724d3800ec8c"},
  };
  int index = 0;
  for (const auto& v : vectors) {
    const SpvHeader header = SpvHeader::parse(fromHex(v.first));
    check(displayHex(powHashOf(ltc, header)) == v.second, "LTC scrypt vector " + std::to_string(index++));
  }
  const std::vector<uint8_t> raw = fromHex(vectors[0].first);
  check(displayHex(SpvHeader::parse(raw).hash()) != vectors[0].second, "LTC identity hash is not the scrypt hash");
}

static void testAsert() {
  const SpvChainRules bch = SpvChainRules::bitcoinCashMainnet();
  const PowTarget powLimit = bch.powLimit;
  const int64_t halfLife = bch.asertHalfLife;
  const int64_t parentTimeDiff = 600;
  const PowTarget initial = powLimit >> 4;

  PowTarget next = calculateAsert(initial, 600, parentTimeDiff + 600, 1, powLimit, halfLife);
  check(next == initial, "ASERT steady");
  next = calculateAsert(initial, 600, parentTimeDiff + 600 + 300, 2, powLimit, halfLife);
  check(next < initial, "ASERT fast block hardens");
  next = calculateAsert(initial, 600, parentTimeDiff + 600 + 300 + 900, 3, powLimit, halfLife);
  check(next == initial, "ASERT catch-up restores");
  PowTarget doubled = initial;
  doubled *= 2;
  check(calculateAsert(initial, 600, parentTimeDiff + 288 * 1200, 288, powLimit, halfLife) == doubled,
        "ASERT two days behind doubles the target");
  check(calculateAsert(doubled, 600, parentTimeDiff + 0, 288, powLimit, halfLife) == initial,
        "ASERT two days ahead halves the target");

  const PowTarget single300 = PowTarget::fromBigEndianHex("00000000ffb1ffffffffffffffffffffffffffffffffffffffffffffffffffff");
  const PowTarget funnyRef = PowTarget::fromBigEndianHex("000000008000000000000000000fffffffffffffffffffffffffffffffffffff");
  const uint32_t powLimitBits = powLimit.toCompact();
  struct Case { PowTarget ref; int64_t timeDiff; int64_t heightDiff; PowTarget expected; uint32_t bits; };
  const Case table[] = {
    {powLimit, 0, 2 * 144, powLimit >> 1, 0x1c7fffff},
    {powLimit, 0, 4 * 144, powLimit >> 2, 0x1c3fffff},
    {powLimit >> 1, 0, 2 * 144, powLimit >> 2, 0x1c3fffff},
    {powLimit >> 2, 0, 2 * 144, powLimit >> 3, 0x1c1fffff},
    {powLimit >> 3, 0, 2 * 144, powLimit >> 4, 0x1c0fffff},
    {powLimit, 0, 2 * (256 - 34) * 144, PowTarget(3), 0x01030000},
    {powLimit, 0, 2 * (256 - 34) * 144 + 119, PowTarget(3), 0x01030000},
    {powLimit, 0, 2 * (256 - 34) * 144 + 120, PowTarget(2), 0x01020000},
    {powLimit, 0, 2 * (256 - 33) * 144 - 1, PowTarget(2), 0x01020000},
    {powLimit, 0, 2 * (256 - 33) * 144, PowTarget(1), 0x01010000},
    {powLimit, 0, 2 * (256 - 32) * 144, PowTarget(1), 0x01010000},
    {PowTarget(1), 0, 2 * (256 - 32) * 144, PowTarget(1), 0x01010000},
    {powLimit, 2 * (512 - 32) * 144, 0, powLimit, powLimitBits},
    {PowTarget(1), (512 - 64) * 144 * 600, 0, powLimit, powLimitBits},
    {powLimit, 300, 1, single300, 0x1d00ffb1},
    {funnyRef, 600 * 2 * 33 * 144, 0, powLimit, powLimitBits},
    {PowTarget(1), 600 * 2 * 256 * 144, 0, powLimit, powLimitBits},
    {PowTarget(1), 600 * 2 * 224 * 144 - 1, 0, PowTarget(0xffff8) << 204, powLimitBits},
  };
  int index = 0;
  for (const auto& c : table) {
    const PowTarget got = calculateAsert(c.ref, 600, parentTimeDiff + c.timeDiff, c.heightDiff, powLimit, halfLife);
    check(got == c.expected && got.toCompact() == c.bits, "ASERT table row " + std::to_string(index++));
  }
}

static SpvHeader headerWith(uint32_t time, uint32_t bits) {
  SpvHeader h;
  h.version = 4;
  h.prevHash.assign(32, 0);
  h.merkleRoot.assign(32, 0);
  h.time = time;
  h.bits = bits;
  return h;
}

static void testHeaderRules() {
  const SpvChainRules btc = SpvChainRules::bitcoinMainnet();
  std::map<uint64_t, SpvHeader> chain;
  for (uint64_t h = 100; h < 120; ++h) chain[h] = headerWith(static_cast<uint32_t>(1000000 + h * 600), 0x1d00ffff);
  const SpvAncestorLookup lookup = [&](uint64_t height, SpvHeader& out) {
    auto it = chain.find(height);
    if (it == chain.end()) return false;
    out = it->second;
    return true;
  };
  const int64_t now = 2000000000;
  SpvHeader easy = headerWith(static_cast<uint32_t>(1000000 + 120 * 600), 0x1d00ffff);
  // A header whose hash meets powLimit is found by trying nonces; it exists
  // within a few billion attempts, too slow here, so check rejection paths.
  check(headerRuleViolation(btc, 120, easy, chain[119], lookup, now) == "insufficient proof of work",
        "random header fails proof of work");
  SpvHeader negative = easy;
  negative.bits = 0x04923456;
  check(!headerRuleViolation(btc, 120, negative, chain[119], lookup, now).empty(), "negative nBits refused");
  SpvHeader aboveLimit = easy;
  aboveLimit.bits = 0x1e00ffff;
  check(headerRuleViolation(btc, 120, aboveLimit, chain[119], lookup, now) == "target above powLimit",
        "target above powLimit refused");
  const SpvChainRules bch = SpvChainRules::bitcoinCashMainnet();
  check(headerRuleViolation(bch, 100, headerWith(1, 0x207fffff), chain[119], lookup, now) == "target above powLimit",
        "BCH regtest difficulty refused on mainnet rules");

  // Bitcoin's genesis header, with a parent of equal nBits at a non-retarget height.
  SpvHeader genesis = headerWith(1231006505, 0x1d00ffff);
  genesis.version = 1;
  genesis.merkleRoot = fromHex("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a");
  genesis.nonce = 2083236893;
  check(displayHex(genesis.hash()) == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f",
        "genesis header hashes to the genesis block");
  const SpvAncestorLookup none = [](uint64_t, SpvHeader&) { return false; };
  check(headerRuleViolation(btc, 1, genesis, headerWith(1231006000, 0x1d00ffff), none, now).empty(),
        "genesis header passes BTC rules");
  SpvHeader tampered = genesis;
  tampered.nonce ^= 1;
  check(headerRuleViolation(btc, 1, tampered, headerWith(1231006000, 0x1d00ffff), none, now) ==
            "insufficient proof of work",
        "one nonce bit flipped fails proof of work");
  check(headerRuleViolation(btc, 1, genesis, headerWith(1231006000, 0x1d00fffe), none, now) ==
            "nBits changed outside a retarget",
        "nBits differing from the parent between retargets refused");
  check(headerRuleViolation(btc, 1, genesis, headerWith(1231006000, 0x1d00ffff), none, 1231006505 - 7201) ==
            "header time too far in the future",
        "header more than two hours ahead refused");

  // A real Litecoin mainnet header (scrypt vector 0) passes Litecoin rules.
  const SpvChainRules ltc = SpvChainRules::litecoinMainnet();
  const SpvHeader ltcHeader = SpvHeader::parse(fromHex(
      "020000004c1271c211717198227392b029a64a7971931d351b387bb80db027f270411e398a07046f7d4a08dd815412a8712f874a7ebf0507e3878bd24e20a3b73fd750a667d2f451eac7471b00de6659"));
  check(headerRuleViolation(ltc, 1000001, ltcHeader, headerWith(ltcHeader.time - 150, ltcHeader.bits), none, now).empty(),
        "real Litecoin header passes Litecoin rules");
  check(headerRuleViolation(btc, 1000001, ltcHeader, headerWith(ltcHeader.time - 150, ltcHeader.bits), none, now) ==
            "insufficient proof of work",
        "the same header fails Bitcoin's double-SHA256 proof of work");
}

int main() {
  testCompactCodec();
  testBitcoinRetarget();
  testLitecoinScrypt();
  testAsert();
  testHeaderRules();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
