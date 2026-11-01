# Release Registry API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Serve a read-only content-hash registry from the Fuego daemon so a platform can fetch a merkle root and an inclusion proof without running a node and without a second service.

**Architecture:** No `xfgapi` exists in this tree or under `/Users/aejt`. Do not create one. The daemon already exposes open JSON HTTP. Copy `/get_commitment_merkle_proof` (`src/Rpc/RpcServer.cpp:233`, handler at `:2565`). The third map flag is `allowBusyCore`, not an auth bit (`RpcServer.h:69-72`). Set it `true`. Do not early-return on `m_restricted_rpc`. Do not call `queryblocks.bin` / `queryblockslite.bin` / `getblocks.bin` — those handlers hang (AGENTS.md). Do not insert song leaves into `CommitmentIndex`. That tree is HEAT commitments. `ReleaseRegistry` is a separate class with the same pairing rule: `Crypto::cn_fast_hash` of a 64-byte concat, odd node duplicated (`CommitmentIndex.cpp:100-131`). This plan ships the index, the proof check, and the HTTP read API. It does not parse `TX_EXTRA_DIGM_ALBUM` and does not accept an HTTP body as a registration. A POST that writes the index is a website. Chain feed is a follow-on plan; the interface it must call is `insert` / `eraseByHeight` below.

**Tech Stack:** C++17, CryptoNote daemon HTTP JSON (`KV_MEMBER`, `Common::podToHex`), `Crypto::cn_fast_hash`, GoogleTest, CMake `tests/CMakeLists.txt`.

---

## Scope check

Two subsystems. This plan is only the first.

| Plan | Ships | Does not ship |
| --- | --- | --- |
| This file | `ReleaseRegistry`, three open HTTP routes, core getters, developer guide | Tx-extra parse, block connect, reorg, signature check, activation height |
| Follow-on, not started | `TransactionExtraReleaseRegistration` on tag `0x0A`, variant insert next to `TransactionExtraMinerParadioVote` (`TransactionExtra.h:535`), parse arm next to `TransactionExtra.cpp:138`, `Blockchain` connect/pop calling `insert` / `eraseByHeight` | A new process named xfgapi |

`TX_EXTRA_DIGM_ALBUM` is already `#define`d at `TransactionExtra.h:52` and is not in the variant. Do not reuse that tag for a different payload. Do not add the struct in this plan. An index fed by tests is not chain truth. The HTTP status string for an empty index is `OK`, `found: false`. Do not invent a leaf to make the route look live.

**Do not push.** Local commits only if the executor is told to commit.

---

## File structure

| File | Responsibility |
| --- | --- |
| `src/CryptoNoteCore/ReleaseRegistry.h` | Leaf, insert, erase-by-height, root, proof, verify |
| `src/CryptoNoteCore/ReleaseRegistry.cpp` | Pairing and lookup. No chain I/O |
| `tests/UnitTests/ReleaseRegistryTest.cpp` | Failing tests first |
| `tests/CMakeLists.txt` | `release_registry_tests` target, link `CryptoNoteCore` the way `alias_index_tests` does (`tests/CMakeLists.txt:30-40`) |
| `src/CryptoNoteCore/Blockchain.h` | Declare the five getters. Own one `ReleaseRegistry` |
| `src/CryptoNoteCore/Blockchain.cpp` | Forward to the member. No extra parsing |
| `src/CryptoNoteCore/Core.h` | Same five getters |
| `src/CryptoNoteCore/Core.cpp` | Forward to `m_blockchain`, same shape as `getCommitmentMerkleRoot` (`Core.cpp:1571`) |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | Three `COMMAND_RPC_*` structs after `COMMAND_RPC_CHECK_COMMITMENT_EXISTS` |
| `src/Rpc/RpcServer.h` | Three handler declarations |
| `src/Rpc/RpcServer.cpp` | Map entries and handlers |
| `docs/developer/release-registry-api.md` | The guide a platform caller reads |

---

## Leaf bytes

A leaf is not the content hash alone. Otherwise two signers of the same file collapse, and a later metadata edit cannot be seen.

```text
preimage = content_hash[32] || signer[32] || height_le64[8] || metadata_len_le16[2] || metadata[0..256]
leaf     = cn_fast_hash(preimage)
```

`content_hash` is SHA-256 of the audio file, computed by the caller. This daemon does not hash audio. `signer` is the 32-byte Ed25519 public key that the follow-on plan must have checked. `metadata` is raw bytes, max 256. Empty metadata is length 0. Odd merkle nodes are paired with themselves, matching `CommitmentIndex.cpp:119-124`.

---

### Task 1: Registry types and the failing test — DONE 2026-10-30

**Files:**
- Create: `src/CryptoNoteCore/ReleaseRegistry.h`
- Test: `tests/UnitTests/ReleaseRegistryTest.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include <cstring>
#include "CryptoNoteCore/ReleaseRegistry.h"

namespace {

Crypto::Hash ones() {
  Crypto::Hash h;
  std::memset(h.data, 0x11, sizeof(h.data));
  return h;
}

Crypto::Hash twos() {
  Crypto::Hash h;
  std::memset(h.data, 0x22, sizeof(h.data));
  return h;
}

}  // namespace

TEST(ReleaseRegistry, EmptyRootIsZeroAndLookupMisses) {
  CryptoNote::ReleaseRegistry reg;
  Crypto::Hash zero{};
  EXPECT_EQ(reg.root(), zero);
  EXPECT_EQ(reg.count(), 0u);
  EXPECT_FALSE(reg.findByContentHash(ones()).has_value());
}

TEST(ReleaseRegistry, InsertBindsSignerAndHeightIntoTheLeaf) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord rec;
  rec.contentHash = ones();
  rec.signer = twos();
  rec.height = 9;
  rec.metadata = {0x01, 0x02};
  ASSERT_TRUE(reg.insert(rec));
  auto found = reg.findByContentHash(ones());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->signer, twos());
  EXPECT_EQ(found->height, 9u);
  EXPECT_EQ(found->metadata.size(), 2u);
  CryptoNote::ReleaseRecord other = rec;
  other.signer = ones();
  Crypto::Hash leafA = CryptoNote::ReleaseRegistry::leafHash(rec);
  Crypto::Hash leafB = CryptoNote::ReleaseRegistry::leafHash(other);
  EXPECT_NE(leafA, leafB);
}

TEST(ReleaseRegistry, SecondContentHashDoesNotReplaceTheFirst) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord first;
  first.contentHash = ones();
  first.signer = twos();
  first.height = 1;
  CryptoNote::ReleaseRecord again = first;
  again.height = 2;
  again.signer = ones();
  ASSERT_TRUE(reg.insert(first));
  EXPECT_FALSE(reg.insert(again));
  EXPECT_EQ(reg.findByContentHash(ones())->height, 1u);
  EXPECT_EQ(reg.count(), 1u);
}

TEST(ReleaseRegistry, ProofVerifiesAndATamperedSiblingDoesNot) {
  CryptoNote::ReleaseRegistry reg;
  for (uint8_t i = 1; i <= 3; ++i) {
    CryptoNote::ReleaseRecord rec;
    std::memset(rec.contentHash.data, i, 32);
    std::memset(rec.signer.data, 0xA0 + i, 32);
    rec.height = i;
    ASSERT_TRUE(reg.insert(rec));
  }
  Crypto::Hash want;
  std::memset(want.data, 2, 32);
  auto proof = reg.proof(want);
  ASSERT_TRUE(proof.found);
  EXPECT_TRUE(CryptoNote::ReleaseRegistry::verify(proof));
  if (!proof.path.empty()) {
    proof.path[0].data[0] ^= 0xff;
    EXPECT_FALSE(CryptoNote::ReleaseRegistry::verify(proof));
  }
}

TEST(ReleaseRegistry, EraseByHeightDropsOnlyThatHeight) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord a;
  std::memset(a.contentHash.data, 1, 32);
  std::memset(a.signer.data, 2, 32);
  a.height = 4;
  CryptoNote::ReleaseRecord b = a;
  std::memset(b.contentHash.data, 3, 32);
  b.height = 5;
  ASSERT_TRUE(reg.insert(a));
  ASSERT_TRUE(reg.insert(b));
  reg.eraseByHeight(4);
  EXPECT_FALSE(reg.findByContentHash(a.contentHash).has_value());
  EXPECT_TRUE(reg.findByContentHash(b.contentHash).has_value());
  EXPECT_EQ(reg.count(), 1u);
}

TEST(ReleaseRegistry, MetadataOver256IsRejected) {
  CryptoNote::ReleaseRegistry reg;
  CryptoNote::ReleaseRecord rec;
  std::memset(rec.contentHash.data, 1, 32);
  std::memset(rec.signer.data, 2, 32);
  rec.height = 1;
  rec.metadata.assign(257, 0x7f);
  EXPECT_FALSE(reg.insert(rec));
  EXPECT_EQ(reg.count(), 0u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build --target release_registry_tests`

Expected: FAIL. `ReleaseRegistry.h` does not exist yet, so configure or compile fails with a missing header. If the target is not in CMake yet, add only the executable stub in Step 3 of Task 2 and re-run. Do not stub the class to make this pass.

---

### Task 2: Implement `ReleaseRegistry` — DONE 2026-10-30

**Files:**
- Create: `src/CryptoNoteCore/ReleaseRegistry.h`
- Create: `src/CryptoNoteCore/ReleaseRegistry.cpp`
- Modify: `tests/CMakeLists.txt` after the `alias_index_tests` block
- Modify: nothing for the source list. `src/CMakeLists.txt:15` globs `CryptoNoteCore/*`, so `ReleaseRegistry.cpp` is picked up on the next configure. Re-run `cmake -S . -B build` before building the test target.

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "crypto/hash.h"

namespace CryptoNote {

struct ReleaseRecord {
  Crypto::Hash contentHash;
  Crypto::Hash signer;
  uint64_t height;
  std::vector<uint8_t> metadata;
};

struct ReleaseProof {
  bool found;
  Crypto::Hash contentHash;
  Crypto::Hash leaf;
  Crypto::Hash root;
  uint64_t height;
  uint32_t leafIndex;
  std::vector<Crypto::Hash> path;
};

class ReleaseRegistry {
public:
  static Crypto::Hash leafHash(const ReleaseRecord& rec);
  static bool verify(const ReleaseProof& proof);

  bool insert(const ReleaseRecord& rec);
  void eraseByHeight(uint64_t height);
  std::optional<ReleaseRecord> findByContentHash(const Crypto::Hash& contentHash) const;
  ReleaseProof proof(const Crypto::Hash& contentHash) const;
  Crypto::Hash root() const;
  size_t count() const;

private:
  Crypto::Hash computeRootUnlocked() const;

  mutable std::mutex m_mutex;
  std::vector<Crypto::Hash> m_leaves;
  std::unordered_map<Crypto::Hash, size_t> m_index;
  std::vector<ReleaseRecord> m_records;
  std::unordered_map<uint64_t, std::vector<Crypto::Hash>> m_byHeight;
};

}  // namespace CryptoNote
```

`Crypto::Hash` is hashable and comparable: `src/crypto/hash.h:91` expands `CRYPTO_MAKE_HASHABLE(Hash)`, which defines `operator==`, `operator!=`, and `std::hash<Crypto::Hash>` (`src/crypto/generic-ops.h:24-45`). No functor is needed, and none may be added. `CommitmentIndex` keys on hex strings by its own choice (`CommitmentIndex.h:161`), not because `Crypto::Hash` requires it.

- [ ] **Step 2: Leaf hash and insert rules**

`leafHash` writes 32 + 32 + 8 + 2 + metadata into a `std::vector<uint8_t>` and calls `Crypto::cn_fast_hash`. Height and metadata length are little-endian. `insert` returns false if `metadata.size() > 256` or `m_index` already holds `contentHash`. It appends the leaf, stores the record, and records the content hash under `height`. It does not replace.

- [ ] **Step 3: Root and proof**

Copy the loop in `CommitmentIndex.cpp:100-184`. Leaves are `leafHash(record)`, not `contentHash`. `verify` recomputes the root from `leaf`, `leafIndex`, and `path`. At level `i`, sibling is `path[i]`. If `leafIndex` at that level is even, hash `current || sibling`. If odd, hash `sibling || current`. Then `leafIndex /= 2`. A duplicated odd node is the same hash on both sides, so a one-leaf tree has an empty path and `verify` checks `leaf == root`.

- [ ] **Step 4: CMake target**

```cmake
add_executable(release_registry_tests
    UnitTests/ReleaseRegistryTest.cpp
)
target_include_directories(release_registry_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(release_registry_tests PRIVATE
    CryptoNoteCore
    gtest
    gtest_main
)
add_test(NAME release_registry_tests COMMAND release_registry_tests)
```

- [ ] **Step 5: Run the test**

Run: `cmake --build build --target release_registry_tests && ./build/tests/release_registry_tests`

Expected: PASS, seven tests. If the binary path differs, use the path CMake printed. Do not claim pass without reading that output.

- [ ] **Step 6: Commit**

```bash
git add src/CryptoNoteCore/ReleaseRegistry.h src/CryptoNoteCore/ReleaseRegistry.cpp tests/UnitTests/ReleaseRegistryTest.cpp tests/CMakeLists.txt
git commit -m "feat: add release registry merkle index"
```

Include the core `CMakeLists.txt` line that compiles `ReleaseRegistry.cpp` in that commit. Do not push.

---

### Task 3: Daemon read routes — DONE 2026-10-31

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.h` near line 349
- Modify: `src/CryptoNoteCore/Blockchain.cpp` next to `getCommitmentMerkleProof` (`:4739`)
- Modify: `src/CryptoNoteCore/Core.h` near line 211
- Modify: `src/CryptoNoteCore/Core.cpp` near line 1571
- Modify: `src/Rpc/CoreRpcServerCommandsDefinitions.h` after line 1602
- Modify: `src/Rpc/RpcServer.h` near line 136
- Modify: `src/Rpc/RpcServer.cpp` after line 234 and after line 2565

- [ ] **Step 1: Core getters**

`Blockchain` holds `ReleaseRegistry m_releaseRegistry`. Getters lock inside the registry. Signatures:

```cpp
Crypto::Hash getReleaseRegistryRoot() const;
size_t getReleaseRegistryCount() const;
std::optional<ReleaseRecord> findRelease(const Crypto::Hash& contentHash) const;
ReleaseProof getReleaseProof(const Crypto::Hash& contentHash) const;
```

`Core` forwards each call to `m_blockchain`, as `getCommitmentMerkleRoot` does. No parsing.

- [ ] **Step 2: Command structs**

```cpp
struct COMMAND_RPC_GET_RELEASE_ROOT {
  typedef EMPTY_STRUCT request;
  struct response {
    std::string merkle_root;
    uint64_t count;
    std::string status;
    void serialize(ISerializer& s) {
      KV_MEMBER(merkle_root)
      KV_MEMBER(count)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_RELEASE {
  struct request {
    std::string content_hash;
    void serialize(ISerializer& s) { KV_MEMBER(content_hash) }
  };
  struct response {
    bool found;
    std::string content_hash;
    std::string signer;
    uint64_t height;
    std::string metadata_hex;
    std::string status;
    void serialize(ISerializer& s) {
      KV_MEMBER(found)
      KV_MEMBER(content_hash)
      KV_MEMBER(signer)
      KV_MEMBER(height)
      KV_MEMBER(metadata_hex)
      KV_MEMBER(status)
    }
  };
};

struct COMMAND_RPC_GET_RELEASE_PROOF {
  struct request {
    std::string content_hash;
    void serialize(ISerializer& s) { KV_MEMBER(content_hash) }
  };
  struct response {
    bool found;
    std::string content_hash;
    std::string leaf_hash;
    std::string merkle_root;
    uint64_t height;
    uint32_t leaf_index;
    std::vector<std::string> proof_path;
    std::string status;
    void serialize(ISerializer& s) {
      KV_MEMBER(found)
      KV_MEMBER(content_hash)
      KV_MEMBER(leaf_hash)
      KV_MEMBER(merkle_root)
      KV_MEMBER(height)
      KV_MEMBER(leaf_index)
      KV_MEMBER(proof_path)
      KV_MEMBER(status)
    }
  };
};
```

- [ ] **Step 3: Map entries**

```cpp
{ "/get_release_root",  { jsonMethod<COMMAND_RPC_GET_RELEASE_ROOT>(&RpcServer::on_get_release_root), true } },
{ "/get_release",       { jsonMethod<COMMAND_RPC_GET_RELEASE>(&RpcServer::on_get_release), true } },
{ "/get_release_proof", { jsonMethod<COMMAND_RPC_GET_RELEASE_PROOF>(&RpcServer::on_get_release_proof), true } },
```

`true` is `allowBusyCore`. Match `/get_commitment_merkle_proof`. Do not wrap these in `jsonMethodSwapAuth`.

- [ ] **Step 4: Handlers**

Reject a `content_hash` whose hex length is not 64 or that fails `Common::podFromHex`. Set `found = false`, `status = CORE_RPC_STATUS_OK`, return true. Do not answer 500 for a miss. On a hit, hex-encode `signer`, `leaf`, `root`, and `metadata` with `Common::podToHex` / a byte-hex helper already used by the commitment handler. Copy the try/catch shape at `RpcServer.cpp:2550-2562`. A thrown exception sets `status` to the exception text and returns true. Do not call `sendrawtransaction`.

- [ ] **Step 5: Build**

Run: `cmake --build build --target Daemon -j$(sysctl -n hw.ncpu)`

Expected: the daemon links. The CMake target is `Daemon`, not `fuegod` (`src/CMakeLists.txt:688`). `fuegod` is only the output binary name (`src/CMakeLists.txt:1059`), so `--target fuegod` fails with "unknown target". Read the linker output. A missing declaration is a fail.

- [ ] **Step 6: Commit**

```bash
git add src/CryptoNoteCore/Blockchain.h src/CryptoNoteCore/Blockchain.cpp src/CryptoNoteCore/Core.h src/CryptoNoteCore/Core.cpp src/Rpc/CoreRpcServerCommandsDefinitions.h src/Rpc/RpcServer.h src/Rpc/RpcServer.cpp
git commit -m "feat: add release registry read routes"
```

Do not push.

---

### Task 4: Developer guide — DONE 2026-10-31

**Files:**
- Create: `docs/developer/release-registry-api.md`

- [ ] **Step 1: Write the guide with these sections and no others**

1. What a caller sends. `content_hash` is hex SHA-256 of the audio file. The daemon does not fetch the file.
2. Routes: `POST /get_release_root`, `POST /get_release`, `POST /get_release_proof`. Body is JSON matching the `KV_MEMBER` names. These handlers use the same JSON method wrapper as `/heat_metrics`, so the HTTP method is whatever that wrapper already accepts. Read `jsonMethod` before writing "GET" into the guide. If it is POST-only, the guide says POST.
3. Empty index: `count: 0`, zero root, `found: false`. That is not a registration.
4. Proof check the caller can do without a node: fold `proof_path` with `cn_fast_hash` of 64-byte pairs, even index means leaf-then-sibling. Odd node duplicates. State that this hash is `cn_fast_hash`, not SHA-256 and not Keccak-in-Solidity.
5. What is absent: no register route, no `0x0A` parser, no reorg undo, no signature check. Tag `0x0A` is reserved and unused.
6. Why there is no xfgapi. One sentence. Point at `s_handlers` in `RpcServer.cpp`.
7. Do not document PARA, hosting, or Farcaster playback on this page.

- [ ] **Step 2: Commit the guide**

```bash
git add docs/developer/release-registry-api.md
git commit -m "docs: describe release registry read API"
```

Do not push.

---

## Self-review

- Spec coverage: read API, merkle proof, no second service, no block-RPC hang path, earlier registration wins (`insert` rejects the second content hash). Chain feed is named and excluded.
- Placeholder scan: no TBD. Follow-on work is a named exclusion, not a step that says "implement later" inside a task.
- Type names used above are the names tasks must keep: `ReleaseRecord`, `ReleaseProof`, `ReleaseRegistry`, `get_release_root`, `get_release`, `get_release_proof`.

## Execution handoff

Plan saved to `docs/superpowers/plans/2026-10-28-release-registry-api.md`.

1. Subagent-driven. Fresh subagent per task, review between tasks.
2. Inline. Execute in this session with checkpoints.

Chain feed is not either option until this plan's tests pass.
