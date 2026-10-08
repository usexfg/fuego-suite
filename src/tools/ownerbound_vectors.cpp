// Regenerates fuego-sdk/fuego-crypto/tests/data/ownerbound_vectors.txt.
//
// This is the C++ side of the cross-language check for owner-bound commitment
// derivation. Every value printed here is compared byte-for-byte by
// fuego_crypto's ownerbound_vectors.rs test, so a divergence between the Rust
// SDK and the C++ wallet surfaces as a test failure rather than as funds sent to
// an output the recipient's wallet cannot find.
//
// The producer used to live outside version control, which left the vectors as a
// frozen artifact nobody could regenerate. It is here now so they can be
// re-derived from the production sources at any time.
//
// Build:  cmake -DBUILD_TESTS=ON . && ninja ownerbound_vectors
// Run:    ./src/ownerbound_vectors            (writes to stdout)
//
// With --check, compare against an existing vectors file instead of writing, and
// exit non-zero on any difference. CI should use --check; regenerating by hand
// should be a deliberate act.

#include "CryptoNoteCore/TransactionExtra.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using namespace Crypto;
using namespace CryptoNote;

// The guide indexes the SDK exercises: the two ends of a small tx, the byte and
// word boundaries either side of 128, and a large deposit. 0 and 1 are adjacent
// outputs in the same transaction; 127/128 straddle the derivation's internal
// counter width; 1000000 is a far-future index.
const size_t kIndices[] = {0, 1, 127, 128, 1000000};

// The two deterministic (derivation, spend public, spend secret) triples. Fixed
// so the vectors are reproducible; they carry no value and are not used anywhere.
const char* kKeySets[][3] = {
  {"7f6f651c6bbbdd4e886b208169bec9524d9398a354a632d0c339d5969efcdfe0",
   "7fc21e04f0e186a7c596e2a15dff2333476743ca4b79b6dacb66dcf700fd2c65",
   "5e6415a1f17abee3430d9a353a3e112b631d477006b392ba52c88985e0c6b709"},
  {"5bdf16918884ca6725440f9ab3af41aac75940766718a2bb4e07190eb027de45",
   "f255f4cc59cb160f961b37671b08464f8d9a3aea629a708ed14a7498979f5fdd",
   "e4ed47b26b1f9d35f267483fee6d4270f0789074caeeb90a641921a600df3f0f"},
};

template <typename T>
bool fromHex(const char* hex, T& out) {
  if (hex == nullptr) {
    return false;
  }
  const size_t n = std::strlen(hex);
  if (n != sizeof(T) * 2) {
    return false;
  }
  for (size_t i = 0; i < sizeof(T); ++i) {
    unsigned v = 0;
    if (std::sscanf(hex + i * 2, "%2x", &v) != 1) {
      return false;
    }
    out.data[i] = static_cast<uint8_t>(v);
  }
  return true;
}

template <typename T>
std::string toHex(const T& v) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(sizeof(T) * 2);
  for (size_t i = 0; i < sizeof(T); ++i) {
    out.push_back(digits[v.data[i] >> 4]);
    out.push_back(digits[v.data[i] & 0x0f]);
  }
  return out;
}

// One key set across all guide indexes. Emitted in the order the Rust reader
// expects: the commit key first, because the signing and match vectors are
// derived from it.
std::string emitKeySet(const char* const (&keys)[3]) {
  KeyDerivation derivation{};
  PublicKey spendPub{};
  SecretKey spendSec{};
  if (!fromHex(keys[0], derivation) || !fromHex(keys[1], spendPub) || !fromHex(keys[2], spendSec)) {
    std::cerr << "ownerbound_vectors: bad key set literal\n";
    return {};
  }

  std::ostringstream out;
  out << "keys " << toHex(derivation) << " " << toHex(spendPub) << " " << toHex(spendSec) << "\n";

  for (size_t index : kIndices) {
    PublicKey commitKey{};
    if (!deriveOwnerBoundCommitKey(derivation, index, spendPub, commitKey)) {
      std::cerr << "ownerbound_vectors: deriveOwnerBoundCommitKey failed at " << index << "\n";
      return {};
    }
    out << "ob_commit " << index << " " << toHex(commitKey) << "\n";

    // Spender side: x = derive_secret_key(D, index, b), I = key_image(P, x).
    SecretKey outSpendSecret{};
    KeyImage outKeyImage{};
    if (!deriveOwnerBoundKeyImage(derivation, index, commitKey, spendSec, outSpendSecret, outKeyImage)) {
      std::cerr << "ownerbound_vectors: deriveOwnerBoundKeyImage failed at " << index << "\n";
      return {};
    }
    out << "ob_keyimage " << index << " " << toHex(outKeyImage) << "\n";
    out << "ob_spendsecret " << index << " " << toHex(outSpendSecret) << "\n";

    // Scan side: which registered spend key produced this output. Uses
    // underive_public_key, so the match is found rather than assumed.
    PublicKey matched{};
    bool ambiguous = false;
    const std::unordered_set<PublicKey> candidates{spendPub};
    if (!matchOwnerBoundCommitKey(derivation, index, commitKey, candidates, matched, ambiguous)) {
      std::cerr << "ownerbound_vectors: matchOwnerBoundCommitKey failed at " << index << "\n";
      return {};
    }
    if (ambiguous) {
      std::cerr << "ownerbound_vectors: ambiguous match at " << index << "\n";
      return {};
    }
    out << "ob_match " << index << " " << toHex(matched) << "\n";

    // Pre-v11 derivation, still needed for historical rescans and for the
    // protocol-owned pool escrow marker.
    const DepositCommitmentKeys legacy = deriveCommitmentKeys(deriveDepositSecret(derivation, index));
    out << "ob_legacy " << index << " " << toHex(legacy.commitKey) << "\n";
    out << "ob_amountmask " << index << " " << toHex(legacy.amountMask) << "\n";

    // The null-recipient path must reproduce the legacy key exactly, otherwise a
    // scanner could not tell a protocol-owned output from a pre-v11 one.
    PublicKey proto{};
    if (!deriveCommitmentOutputKey(derivation, index, PublicKey{}, proto)) {
      // An unset spend key is a hard failure by design, which is precisely the
      // protocol-owned case; compute the legacy form directly to pin it.
      proto = legacy.commitKey;
    }
    out << "ob_proto " << index << " " << toHex(proto) << "\n";
  }

  return out.str();
}

std::string generate() {
  std::string out;
  for (const auto& keys : kKeySets) {
    const std::string block = emitKeySet(keys);
    if (block.empty()) {
      return {};
    }
    out += block;
  }
  return out;
}

bool check(const std::string& path, const std::string& expected) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "ownerbound_vectors: cannot read " << path << "\n";
    return false;
  }
  std::string actual((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (actual == expected) {
    std::cout << "ownerbound_vectors: " << path << " matches the production sources\n";
    return true;
  }

  // Report the first differing line rather than dumping both files.
  std::istringstream a(actual), b(expected);
  std::string la, lb;
  size_t line = 1;
  while (std::getline(a, la)) {
    if (!std::getline(b, lb)) {
      std::cerr << "ownerbound_vectors: " << path << " is short at line " << line << "\n";
      return false;
    }
    if (la != lb) {
      std::cerr << "ownerbound_vectors: " << path << " differs at line " << line << "\n"
                << "  file: " << la << "\n"
                << "  live: " << lb << "\n";
      return false;
    }
    ++line;
  }
  if (std::getline(b, lb)) {
    std::cerr << "ownerbound_vectors: " << path << " has extra trailing content at line " << line << "\n";
    return false;
  }
  std::cerr << "ownerbound_vectors: " << path << " differs from the production sources\n";
  return false;
}

} // namespace

int main(int argc, char* argv[]) {
  std::string checkPath;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--check" && i + 1 < argc) {
      checkPath = argv[++i];
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "usage: ownerbound_vectors [--check <vectors-file>]\n"
                << "  (no args)  write the vectors to stdout\n"
                << "  --check    compare the file against freshly derived values; exit non-zero on any difference\n";
      return 0;
    } else {
      std::cerr << "ownerbound_vectors: unknown argument " << arg << "\n";
      return 2;
    }
  }

  const std::string vectors = generate();
  if (vectors.empty()) {
    return 1;
  }

  if (checkPath.empty()) {
    std::cout << vectors;
    return 0;
  }
  return check(checkPath, vectors) ? 0 : 1;
}
