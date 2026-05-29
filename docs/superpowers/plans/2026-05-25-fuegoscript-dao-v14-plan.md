# FuegoScript + XFG-Lock DAO — V14 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship V14 hard fork adding a predicate VM (FuegoScript), XFG-Lock voting construct, PCM bundle tag, and federated DAO governance to Fuego.

**Architecture:** FuegoScript is a 45-opcode stack-based predicate VM (no persistent state, no side effects) that executes in the parent-block bundle via a new PCM inner tag (`0x0B`). XFG-Lock (`0x13`) lets users lock XFG with a voter_pubkey for DAO voting weight. DAO proposals (`0x14`) and votes (`0x15`) are top-level tx_extra tags; a standard tally PCM script commits pass/fail at proposal close. V14 treasury is a federated M-of-N multi-sig; V15 replaces it with script-locked outputs.

**Tech Stack:** C++ (matching existing CryptoNote codebase), ed25519 signatures, cn_fast_hash (keccak-256), u256 big-integer arithmetic.

**Spec:** [`docs/superpowers/specs/2026-05-18-fuegoscript-dao-w1-design.md`](../specs/2026-05-18-fuegoscript-dao-w1-design.md) on branch `claude/nifty-wing-af39df`.

**Open questions to resolve before implementation:** See spec §11 — especially Q1a (VM parameter defaults: bytecode cap, gas cap, stack depth, etc.). All values below use the spec's proposed defaults until confirmed.

---

## Chunk 1: Fork constants + u256 library + opcode definitions

This chunk has zero consensus logic — it establishes the type system, constants, and opcode catalog that everything else builds on. It should compile and pass unit tests in isolation.

### Task 1: V14 fork constants

**Files:**
- Modify: `src/CryptoNoteConfig.h`

- [ ] **Step 1: Write the failing test**

Create `tests/UnitTests/TestForkConstantsV14.cpp`:

```cpp
#include <gtest/gtest.h>
#include "CryptoNoteConfig.h"
#include "CryptoNote.h"

TEST(ForkConstants, V14Defined) {
  EXPECT_EQ(CryptoNote::parameters::UPGRADE_HEIGHT_V14, 1444444u);
  EXPECT_EQ(CryptoNote::parameters::BLOCK_MAJOR_VERSION_14, 14);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target UnitTests && ./build/tests/UnitTests --gtest_filter="ForkConstants.*"`
Expected: compilation error — `UPGRADE_HEIGHT_V14` and `BLOCK_MAJOR_VERSION_14` not defined.

- [ ] **Step 3: Add constants to source**

In `src/CryptoNoteConfig.h`, after the existing `UPGRADE_HEIGHT_V11 = 1111111` block:

```cpp
const uint64_t UPGRADE_HEIGHT_V14                        = 1444444; // FuegoScript + XFG-Lock DAO
```

In the same file, after `BLOCK_MAJOR_VERSION_11`:

```cpp
const uint8_t  BLOCK_MAJOR_VERSION_14                    = 14; // FuegoScript + XFG-Lock DAO
```

Note: both `UPGRADE_HEIGHT_V*` and `BLOCK_MAJOR_VERSION_*` constants live in `src/CryptoNoteConfig.h` (not `include/CryptoNote.h`).

- [ ] **Step 4: Run test to verify it passes**

Run: rebuild + run test.
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteConfig.h tests/UnitTests/TestForkConstantsV14.cpp
git commit -m "feat(v14): add UPGRADE_HEIGHT_V14 and BLOCK_MAJOR_VERSION_14 constants"
```

---

### Task 2: u256 type and arithmetic

**Files:**
- Create: `src/CryptoNoteCore/FuegoScript/U256.h`
- Create: `src/CryptoNoteCore/FuegoScript/U256.cpp`
- Create: `tests/UnitTests/TestFuegoScriptU256.cpp`

The VM needs a fixed-width 256-bit unsigned integer with deterministic overflow/underflow trapping. Check whether the existing codebase has a u256 type (e.g., in `src/crypto/` or via Boost multiprecision). If yes, wrap it. If no, implement a minimal one: two `__uint128_t` limbs or four `uint64_t` limbs, big-endian serialization.

- [ ] **Step 1: Write failing tests for u256 arithmetic**

```cpp
#include <gtest/gtest.h>
#include "FuegoScript/U256.h"

using namespace FuegoScript;

TEST(U256, AddNoOverflow) {
  U256 a = U256::from_u64(100);
  U256 b = U256::from_u64(200);
  auto [result, trapped] = a.checked_add(b);
  EXPECT_FALSE(trapped);
  EXPECT_EQ(result.to_u64(), 300u);
}

TEST(U256, AddOverflowTraps) {
  U256 max = U256::max_value();
  U256 one = U256::from_u64(1);
  auto [result, trapped] = max.checked_add(one);
  EXPECT_TRUE(trapped);
}

TEST(U256, SubUnderflowTraps) {
  U256 a = U256::from_u64(5);
  U256 b = U256::from_u64(10);
  auto [result, trapped] = a.checked_sub(b);
  EXPECT_TRUE(trapped);
}

TEST(U256, MulOverflowTraps) {
  U256 big = U256::max_value();
  U256 two = U256::from_u64(2);
  auto [result, trapped] = big.checked_mul(two);
  EXPECT_TRUE(trapped);
}

TEST(U256, DivByZeroTraps) {
  U256 a = U256::from_u64(100);
  U256 zero = U256::from_u64(0);
  auto [result, trapped] = a.checked_div(zero);
  EXPECT_TRUE(trapped);
}

TEST(U256, ModByZeroTraps) {
  U256 a = U256::from_u64(100);
  U256 zero = U256::from_u64(0);
  auto [result, trapped] = a.checked_mod(zero);
  EXPECT_TRUE(trapped);
}

TEST(U256, MinMax) {
  U256 a = U256::from_u64(42);
  U256 b = U256::from_u64(99);
  EXPECT_EQ(U256::min(a, b).to_u64(), 42u);
  EXPECT_EQ(U256::max(a, b).to_u64(), 99u);
}

TEST(U256, Comparison) {
  U256 a = U256::from_u64(10);
  U256 b = U256::from_u64(20);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(a <= a);
  EXPECT_TRUE(a >= a);
  EXPECT_FALSE(a == b);
}

TEST(U256, BitwiseAndOr) {
  U256 a = U256::from_u64(0xFF00);
  U256 b = U256::from_u64(0x0FF0);
  EXPECT_EQ((a & b).to_u64(), 0x0F00u);
  EXPECT_EQ((a | b).to_u64(), 0xFFF0u);
}

TEST(U256, BigEndianRoundtrip) {
  U256 val = U256::from_u64(0xDEADBEEF);
  std::array<uint8_t, 32> bytes = val.to_big_endian();
  U256 back = U256::from_big_endian(bytes);
  EXPECT_EQ(val, back);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: compilation error — `FuegoScript/U256.h` not found.

- [ ] **Step 3: Implement U256**

Create `src/CryptoNoteCore/FuegoScript/U256.h` and `U256.cpp`. Key interface:

```cpp
namespace FuegoScript {

class U256 {
public:
  static U256 from_u64(uint64_t v);
  static U256 from_big_endian(const std::array<uint8_t, 32>& bytes);
  static U256 max_value();
  static U256 zero();
  static U256 min(const U256& a, const U256& b);
  static U256 max(const U256& a, const U256& b);

  std::pair<U256, bool> checked_add(const U256& other) const;
  std::pair<U256, bool> checked_sub(const U256& other) const;
  std::pair<U256, bool> checked_mul(const U256& other) const;
  std::pair<U256, bool> checked_div(const U256& other) const;
  std::pair<U256, bool> checked_mod(const U256& other) const;

  U256 operator&(const U256& other) const;
  U256 operator|(const U256& other) const;

  bool operator==(const U256& other) const;
  bool operator<(const U256& other) const;
  bool operator>(const U256& other) const;
  bool operator<=(const U256& other) const;
  bool operator>=(const U256& other) const;

  std::array<uint8_t, 32> to_big_endian() const;
  uint64_t to_u64() const; // truncates; use only when known to fit

private:
  uint64_t limbs[4]; // limbs[0] = most significant
};

} // namespace FuegoScript
```

Implementation: four `uint64_t` limbs, big-endian order (limbs[0] = MSB). `checked_add` carries across limbs; `checked_mul` uses schoolbook multiplication with overflow detection.

- [ ] **Step 4: Run tests to verify they pass**

Expected: all 10 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/FuegoScript/U256.h src/CryptoNoteCore/FuegoScript/U256.cpp tests/UnitTests/TestFuegoScriptU256.cpp
git commit -m "feat(v14): add U256 type with checked arithmetic for FuegoScript VM"
```

---

### Task 3: Opcode enum + gas table

**Files:**
- Create: `src/CryptoNoteCore/FuegoScript/Opcodes.h`
- Create: `tests/UnitTests/TestFuegoScriptOpcodes.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "FuegoScript/Opcodes.h"

using namespace FuegoScript;

TEST(Opcodes, AllEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_PUSH8),  0x01);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_PUSH32), 0x02);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_ADD),    0x10);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_RETURN), 0x36);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_HASH256),0x40);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_BLOCK_HEIGHT), 0x50);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_XFG_LOCKED),   0x60);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_PROPOSAL_GET), 0x70);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_VOTE_TALLY),   0x71);
  EXPECT_EQ(static_cast<uint8_t>(Opcode::OP_VOTE_COUNT),   0x72);
}

TEST(Opcodes, GasCosts) {
  EXPECT_EQ(gas_cost(Opcode::OP_PUSH8), 1u);
  EXPECT_EQ(gas_cost(Opcode::OP_ADD),   2u);
  EXPECT_EQ(gas_cost(Opcode::OP_HASH256), 30u);
  EXPECT_EQ(gas_cost(Opcode::OP_VERIFY_SIG), 100u);
  EXPECT_EQ(gas_cost(Opcode::OP_VERIFY_BLS), 300u);
  EXPECT_EQ(gas_cost(Opcode::OP_VRF_VERIFY), 150u);
  EXPECT_EQ(gas_cost(Opcode::OP_XFG_LOCKED), 20u);
  EXPECT_EQ(gas_cost(Opcode::OP_VOTE_TALLY), 25u);
}

TEST(Opcodes, TotalCount) {
  EXPECT_EQ(OPCODE_COUNT, 45u);
}

TEST(Opcodes, InvalidOpcodeDetected) {
  EXPECT_FALSE(is_valid_opcode(0x00));
  EXPECT_FALSE(is_valid_opcode(0x08));
  EXPECT_FALSE(is_valid_opcode(0xFF));
  EXPECT_TRUE(is_valid_opcode(0x01));  // OP_PUSH8
  EXPECT_TRUE(is_valid_opcode(0x72));  // OP_VOTE_COUNT
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: compilation error.

- [ ] **Step 3: Implement Opcodes.h**

Create `src/CryptoNoteCore/FuegoScript/Opcodes.h` with the full 45-opcode enum per spec §2.4:

```cpp
#pragma once
#include <cstdint>

namespace FuegoScript {

enum class Opcode : uint8_t {
  // Stack manipulation (gas 1)
  OP_PUSH8  = 0x01, OP_PUSH32 = 0x02, OP_DUP  = 0x03, OP_DROP = 0x04,
  OP_SWAP   = 0x05, OP_OVER   = 0x06, OP_PICK = 0x07,
  // Arithmetic (gas 2)
  OP_ADD = 0x10, OP_SUB = 0x11, OP_MUL = 0x12, OP_DIV = 0x13,
  OP_MOD = 0x14, OP_MIN = 0x15, OP_MAX = 0x16,
  // Comparison & logic (gas 1)
  OP_EQ  = 0x20, OP_LT = 0x21, OP_GT  = 0x22, OP_LE = 0x23,
  OP_GE  = 0x24, OP_NOT = 0x25, OP_AND = 0x26, OP_OR = 0x27,
  // Control flow (gas 1 base)
  OP_IF = 0x30, OP_ELSE = 0x31, OP_ENDIF = 0x32,
  OP_LOOP = 0x33, OP_ENDLOOP = 0x34, OP_VERIFY = 0x35, OP_RETURN = 0x36,
  // Hash & signature (gas 30-300)
  OP_HASH256 = 0x40, OP_VERIFY_SIG = 0x41, OP_VERIFY_MULTISIG_K_OF_N = 0x42,
  OP_VERIFY_BLS = 0x43, OP_VRF_VERIFY = 0x44, OP_MERKLE_VERIFY = 0x45,
  // Chain state reads (gas 20)
  OP_BLOCK_HEIGHT = 0x50, OP_BLOCK_TIME = 0x51, OP_BLOCK_HASH = 0x52,
  OP_XFG_LOCKED = 0x60, OP_HEAT_CD_BAL = 0x61, OP_DIGM_BAL = 0x62, OP_CURA_BAL = 0x63,
  // DAO helpers (gas 25)
  OP_PROPOSAL_GET = 0x70, OP_VOTE_TALLY = 0x71, OP_VOTE_COUNT = 0x72,
};

constexpr size_t OPCODE_COUNT = 45;

uint32_t gas_cost(Opcode op);
bool is_valid_opcode(uint8_t byte);
// Returns number of immediate operand bytes for this opcode (0 for most)
size_t operand_size(Opcode op);

} // namespace FuegoScript
```

Gas costs and operand sizes implemented as lookup tables per spec §2.4 and §2.5.

- [ ] **Step 4: Run tests to verify they pass**

Expected: all 4 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/FuegoScript/Opcodes.h tests/UnitTests/TestFuegoScriptOpcodes.cpp
git commit -m "feat(v14): add FuegoScript opcode enum, gas table, and validity check"
```

---

### Task 4: Bytecode parser

**Files:**
- Create: `src/CryptoNoteCore/FuegoScript/Bytecode.h`
- Create: `src/CryptoNoteCore/FuegoScript/Bytecode.cpp`
- Create: `tests/UnitTests/TestFuegoScriptBytecode.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "FuegoScript/Bytecode.h"

using namespace FuegoScript;

TEST(Bytecode, ValidPreamble) {
  // magic "FUEG" + version 0x01 + reserved 0x00 + bytecode_len=2 + max_gas_hint=10000
  std::vector<uint8_t> raw = {
    0xFE, 0xE6, 0x01, 0x00,   // magic + version + reserved
    0x02, 0x00,                 // bytecode_len = 2 (little-endian)
    0x10, 0x27,                 // max_gas_hint = 10000 (little-endian)
    0x01, 42                    // OP_PUSH8 42
  };
  auto result = Bytecode::parse(raw);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->version, 0x01);
  EXPECT_EQ(result->max_gas_hint, 10000u);
  EXPECT_EQ(result->opcodes.size(), 2u); // OP_PUSH8 + operand byte
}

TEST(Bytecode, BadMagicRejected) {
  std::vector<uint8_t> raw = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_FALSE(Bytecode::parse(raw).has_value());
}

TEST(Bytecode, TooLongRejected) {
  // bytecode_len = 1025 (exceeds 1024 cap)
  std::vector<uint8_t> raw = {0xFE, 0xE6, 0x01, 0x00, 0x01, 0x04, 0x10, 0x27};
  // 0x0401 = 1025 little-endian
  EXPECT_FALSE(Bytecode::parse(raw).has_value());
}

TEST(Bytecode, SerializeRoundtrip) {
  std::vector<uint8_t> opcodes = {0x01, 42}; // OP_PUSH8 42
  auto bc = Bytecode::create(0x01, 10000, opcodes);
  auto serialized = bc.serialize();
  auto parsed = Bytecode::parse(serialized);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->opcodes, opcodes);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: compilation error.

- [ ] **Step 3: Implement Bytecode**

Per spec §2.2: 8-byte preamble (`0xFE 0xE6 version reserved bytecode_len_u16le max_gas_hint_u16le`) followed by opcode bytes. Parse validates magic, version == 0x01, bytecode_len ≤ 1024, and that the byte stream is exactly `8 + bytecode_len` long.

- [ ] **Step 4: Run tests to verify they pass**

Expected: all 4 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/FuegoScript/Bytecode.h src/CryptoNoteCore/FuegoScript/Bytecode.cpp tests/UnitTests/TestFuegoScriptBytecode.cpp
git commit -m "feat(v14): add FuegoScript bytecode parser with preamble validation"
```

---

## Chunk 2: VM execution engine

The VM takes parsed bytecode + a `ScriptContext` and produces a 32-byte commitment (or traps). This chunk implements the core execution loop, stack, gas accounting, and all 45 opcodes. Chain-state opcodes are stubbed via a `ChainStateView` interface.

### Task 5: ScriptContext + ChainStateView interface

**Files:**
- Create: `src/CryptoNoteCore/FuegoScript/ScriptContext.h`
- Create: `src/CryptoNoteCore/FuegoScript/ChainStateView.h`

- [ ] **Step 1: Define the interfaces**

Per spec §2.3:

```cpp
// ScriptContext.h
#pragma once
#include <cstdint>
#include <array>
#include "crypto/hash.h"
#include "FuegoScript/ChainStateView.h"

namespace FuegoScript {

struct ScriptContext {
  uint32_t block_height;
  uint64_t block_timestamp;
  Crypto::Hash block_prev_hash;
  Crypto::Hash bundle_hash;
  std::array<Crypto::Hash, 16> recent_block_hashes;
  const ChainStateView* state; // non-owning
};

} // namespace FuegoScript
```

```cpp
// ChainStateView.h
#pragma once
#include "FuegoScript/U256.h"
#include "crypto/hash.h"

namespace FuegoScript {

class ChainStateView {
public:
  virtual ~ChainStateView() = default;

  // §2.4 chain state opcodes
  virtual U256 get_xfg_locked(const std::array<uint8_t, 32>& voter_pubkey) const = 0;
  virtual U256 get_heat_cd_balance(const std::array<uint8_t, 32>& wallet_commitment) const = 0;
  virtual U256 get_digm_balance(const std::array<uint8_t, 32>& wallet_address) const = 0;
  virtual U256 get_cura_balance(const std::array<uint8_t, 32>& wallet_pubkey) const = 0;

  // §2.4 DAO helper opcodes
  struct ProposalInfo {
    uint8_t status;           // 0=open, 1=passed, 2=failed, 3=expired
    uint32_t deadline_height;
    std::array<uint8_t, 20> target_addr_truncated;
    uint64_t amount;
  };
  virtual ProposalInfo get_proposal(const std::array<uint8_t, 32>& proposal_id) const = 0;
  virtual U256 get_vote_tally(const std::array<uint8_t, 32>& proposal_id, uint8_t vote_kind) const = 0;
  virtual U256 get_vote_count(const std::array<uint8_t, 32>& proposal_id) const = 0;
};

} // namespace FuegoScript
```

- [ ] **Step 2: Compile to verify headers are valid**

Run: `cmake --build build --target CryptoNoteCore` (with new files added to CMakeLists).
Expected: compiles with no errors.

- [ ] **Step 3: Commit**

```bash
git add src/CryptoNoteCore/FuegoScript/ScriptContext.h src/CryptoNoteCore/FuegoScript/ChainStateView.h
git commit -m "feat(v14): add ScriptContext and ChainStateView interfaces"
```

---

### Task 6: VM core — stack + gas counter + execution loop

**Files:**
- Create: `src/CryptoNoteCore/FuegoScript/VM.h`
- Create: `src/CryptoNoteCore/FuegoScript/VM.cpp`
- Create: `tests/UnitTests/TestFuegoScriptVM.cpp`

- [ ] **Step 1: Write failing tests for basic execution**

```cpp
#include <gtest/gtest.h>
#include "FuegoScript/VM.h"
#include "FuegoScript/Bytecode.h"

using namespace FuegoScript;

// Minimal stub ChainStateView for testing
class StubChainState : public ChainStateView {
public:
  U256 get_xfg_locked(const std::array<uint8_t, 32>&) const override { return U256::zero(); }
  U256 get_heat_cd_balance(const std::array<uint8_t, 32>&) const override { return U256::zero(); }
  U256 get_digm_balance(const std::array<uint8_t, 32>&) const override { return U256::zero(); }
  U256 get_cura_balance(const std::array<uint8_t, 32>&) const override { return U256::zero(); }
  ProposalInfo get_proposal(const std::array<uint8_t, 32>&) const override { return {}; }
  U256 get_vote_tally(const std::array<uint8_t, 32>&, uint8_t) const override { return U256::zero(); }
  U256 get_vote_count(const std::array<uint8_t, 32>&) const override { return U256::zero(); }
};

TEST(VM, Push8ReturnsValue) {
  // Script: OP_PUSH8 42
  auto bc = Bytecode::create(0x01, 10000, {0x01, 42});
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->to_u64(), 42u);
}

TEST(VM, AddTwoNumbers) {
  // OP_PUSH8 10, OP_PUSH8 20, OP_ADD
  auto bc = Bytecode::create(0x01, 10000, {0x01, 10, 0x01, 20, 0x10});
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->to_u64(), 30u);
}

TEST(VM, GasExhaustionTraps) {
  // Script that exceeds gas: OP_PUSH8 1 repeated enough times
  // gas_hint = 3 means only 3 gas available; OP_PUSH8 costs 1 each
  auto bc = Bytecode::create(0x01, 3, {0x01, 1, 0x01, 2, 0x01, 3, 0x01, 4});
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  EXPECT_FALSE(result.has_value()); // trapped on gas
}

TEST(VM, StackUnderflowTraps) {
  // OP_ADD with empty stack
  auto bc = Bytecode::create(0x01, 10000, {0x10});
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  EXPECT_FALSE(result.has_value());
}

TEST(VM, InvalidOpcodeTraps) {
  auto bc = Bytecode::create(0x01, 10000, {0x01, 1, 0x00}); // 0x00 is invalid
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  EXPECT_FALSE(result.has_value());
}

TEST(VM, MustLeaveExactlyOneValue) {
  // Two values left on stack (no reduce)
  auto bc = Bytecode::create(0x01, 10000, {0x01, 1, 0x01, 2});
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  EXPECT_FALSE(result.has_value()); // stack has 2 items, not 1
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: compilation error — `VM.h` not found.

- [ ] **Step 3: Implement VM core**

VM class with:
- Fixed-size stack (max 256 entries per spec §2.1)
- Gas counter with MAX_GAS_GLOBAL = 10000
- Main dispatch loop over parsed opcodes
- Start with stack/arithmetic/comparison/control-flow opcodes only (Tasks 7-8 add crypto + chain-state)

```cpp
// VM.h
#pragma once
#include <optional>
#include "FuegoScript/Bytecode.h"
#include "FuegoScript/ScriptContext.h"
#include "FuegoScript/U256.h"

namespace FuegoScript {

class VM {
public:
  static constexpr uint32_t MAX_GAS_GLOBAL = 10000;
  static constexpr size_t MAX_STACK_DEPTH = 256;

  // Returns commitment (top-of-stack u256) on success, nullopt on trap
  static std::optional<U256> execute(const Bytecode& bc, const ScriptContext& ctx);
};

} // namespace FuegoScript
```

Implement in `VM.cpp`:
- Loop over bytecode bytes, dispatch per opcode
- Track gas counter; trap if exceeds `min(bc.max_gas_hint, MAX_GAS_GLOBAL)`
- Stack push/pop with bounds checks
- All 7 stack opcodes, 7 arithmetic opcodes, 8 comparison/logic opcodes
- Control flow: `OP_IF/ELSE/ENDIF`, `OP_LOOP/ENDLOOP` (with depth-4 nesting limit), `OP_VERIFY`, `OP_RETURN`
- Gas for `OP_LOOP`: pre-charge `1 + n × body_cost` per spec §2.5

- [ ] **Step 4: Run tests to verify they pass**

Expected: all 6 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/FuegoScript/VM.h src/CryptoNoteCore/FuegoScript/VM.cpp tests/UnitTests/TestFuegoScriptVM.cpp
git commit -m "feat(v14): add FuegoScript VM execution engine with stack, gas, and core opcodes"
```

---

### Task 7: Hash & signature opcodes

**Files:**
- Modify: `src/CryptoNoteCore/FuegoScript/VM.cpp`
- Create: `tests/UnitTests/TestFuegoScriptVMCrypto.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(VMCrypto, Hash256ProducesKnownHash) {
  // OP_PUSH32 <32 bytes>, OP_HASH256
  // The script pushes 32 bytes, hashes them with cn_fast_hash, leaves hash on stack
  std::vector<uint8_t> ops;
  ops.push_back(0x02); // OP_PUSH32
  std::array<uint8_t, 32> input{};
  input[0] = 0xAB;
  ops.insert(ops.end(), input.begin(), input.end());
  ops.push_back(0x40); // OP_HASH256
  auto bc = Bytecode::create(0x01, 10000, ops);
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  ASSERT_TRUE(result.has_value());
  // Verify against known cn_fast_hash output
  Crypto::Hash expected;
  Crypto::cn_fast_hash(input.data(), input.size(), expected);
  std::array<uint8_t, 32> expected_arr;
  std::memcpy(expected_arr.data(), &expected, 32);
  EXPECT_EQ(result->to_big_endian(), expected_arr);
}

TEST(VMCrypto, VerifySigPassesOnValid) {
  // Use a hardcoded ed25519 keypair (deterministic from seed)
  Crypto::SecretKey sk;
  Crypto::PublicKey pk;
  Crypto::generate_keys(pk, sk);

  // Message hash to sign
  std::array<uint8_t, 32> msg_hash{};
  msg_hash[0] = 0x42;
  Crypto::Signature sig;
  Crypto::generate_signature(
    *reinterpret_cast<Crypto::Hash*>(msg_hash.data()), pk, sk, sig);

  // Script: OP_PUSH32 msg_hash, OP_PUSH32 sig[0..31], OP_PUSH32 sig[32..63],
  //         OP_PUSH32 pubkey, OP_VERIFY_SIG, OP_PUSH8 1
  // OP_VERIFY_SIG pops (msg_hash, signature, pubkey) and traps on failure;
  // if it succeeds, script continues to push 1 as the commitment
  std::vector<uint8_t> ops;
  ops.push_back(0x02); // OP_PUSH32 msg_hash
  ops.insert(ops.end(), msg_hash.begin(), msg_hash.end());
  ops.push_back(0x02); // OP_PUSH32 sig bytes (64 B = two PUSH32)
  ops.insert(ops.end(), reinterpret_cast<uint8_t*>(&sig),
             reinterpret_cast<uint8_t*>(&sig) + 32);
  ops.push_back(0x02);
  ops.insert(ops.end(), reinterpret_cast<uint8_t*>(&sig) + 32,
             reinterpret_cast<uint8_t*>(&sig) + 64);
  ops.push_back(0x02); // OP_PUSH32 pubkey
  ops.insert(ops.end(), reinterpret_cast<uint8_t*>(&pk),
             reinterpret_cast<uint8_t*>(&pk) + 32);
  ops.push_back(0x41); // OP_VERIFY_SIG
  ops.push_back(0x01); ops.push_back(1); // OP_PUSH8 1 (final commitment)

  auto bc = Bytecode::create(0x01, 10000, ops);
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->to_u64(), 1u);
}

TEST(VMCrypto, VerifySigTrapsOnInvalid) {
  Crypto::SecretKey sk;
  Crypto::PublicKey pk;
  Crypto::generate_keys(pk, sk);

  std::array<uint8_t, 32> msg_hash{};
  msg_hash[0] = 0x42;
  Crypto::Signature sig;
  Crypto::generate_signature(
    *reinterpret_cast<Crypto::Hash*>(msg_hash.data()), pk, sk, sig);

  // Corrupt first byte of signature
  uint8_t* sig_bytes = reinterpret_cast<uint8_t*>(&sig);
  sig_bytes[0] ^= 0xFF;

  std::vector<uint8_t> ops;
  ops.push_back(0x02);
  ops.insert(ops.end(), msg_hash.begin(), msg_hash.end());
  ops.push_back(0x02);
  ops.insert(ops.end(), sig_bytes, sig_bytes + 32);
  ops.push_back(0x02);
  ops.insert(ops.end(), sig_bytes + 32, sig_bytes + 64);
  ops.push_back(0x02);
  ops.insert(ops.end(), reinterpret_cast<uint8_t*>(&pk),
             reinterpret_cast<uint8_t*>(&pk) + 32);
  ops.push_back(0x41); // OP_VERIFY_SIG
  ops.push_back(0x01); ops.push_back(1);

  auto bc = Bytecode::create(0x01, 10000, ops);
  StubChainState state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  EXPECT_FALSE(result.has_value()); // trapped: invalid signature
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: test references unimplemented opcodes in VM dispatch.

- [ ] **Step 3: Implement crypto opcodes in VM.cpp**

Add dispatch cases for:
- `OP_HASH256` (gas 30): pop 32 bytes, push cn_fast_hash result
- `OP_VERIFY_SIG` (gas 100): pop msg_hash + sig + pubkey, trap if ed25519 verify fails
- `OP_VERIFY_MULTISIG_K_OF_N` (gas 100×N): pop msg_hash + K sigs + N pubkeys, verify threshold
- `OP_VERIFY_BLS` (gas 300): pop msg_hash + BLS_agg_sig + pubkey_bitmap + committee_id, verify
- `OP_VRF_VERIFY` (gas 150): pop alpha + beta + proof + pubkey, verify VRF
- `OP_MERKLE_VERIFY` (gas 30): pop leaf + path + root, verify inclusion

Wire to existing `src/crypto/` ed25519 and cn_fast_hash implementations. BLS may require a stub or new dependency — document what's needed.

- [ ] **Step 4: Run tests to verify they pass**

Expected: all crypto tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/FuegoScript/VM.cpp tests/UnitTests/TestFuegoScriptVMCrypto.cpp
git commit -m "feat(v14): implement FuegoScript hash and signature opcodes"
```

---

### Task 8: Chain-state + DAO-helper opcodes

**Files:**
- Modify: `src/CryptoNoteCore/FuegoScript/VM.cpp`
- Create: `tests/UnitTests/TestFuegoScriptVMChainState.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(VMChainState, BlockHeightPushed) {
  auto bc = Bytecode::create(0x01, 10000, {0x50}); // OP_BLOCK_HEIGHT
  StubChainState state;
  ScriptContext ctx{};
  ctx.block_height = 12345;
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->to_u64(), 12345u);
}

TEST(VMChainState, BlockTimePushed) {
  auto bc = Bytecode::create(0x01, 10000, {0x51}); // OP_BLOCK_TIME
  StubChainState state;
  ScriptContext ctx{};
  ctx.block_timestamp = 1716000000;
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->to_u64(), 1716000000u);
}

TEST(VMChainState, XfgLockedFromStub) {
  // Custom stub that returns 5000 for a known voter pubkey
  class XfgStub : public StubChainState {
  public:
    U256 get_xfg_locked(const std::array<uint8_t, 32>& voter) const override {
      if (voter[0] == 0xAA) return U256::from_u64(5000);
      return U256::zero();
    }
  };
  // Script: OP_PUSH32 <voter_pubkey with [0]=0xAA>, OP_XFG_LOCKED
  std::vector<uint8_t> ops;
  ops.push_back(0x02); // OP_PUSH32
  std::array<uint8_t, 32> voter{};
  voter[0] = 0xAA;
  ops.insert(ops.end(), voter.begin(), voter.end());
  ops.push_back(0x60); // OP_XFG_LOCKED
  auto bc = Bytecode::create(0x01, 10000, ops);
  XfgStub state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->to_u64(), 5000u);
}

TEST(VMChainState, XfgLockedExpiryBoundary) {
  // Per spec §3.7: OP_XFG_LOCKED must NOT count outputs whose
  // lock_until_height <= current_height (lock expired)
  // This tests the boundary through the VM opcode path
  class ExpiryStub : public StubChainState {
  public:
    uint32_t current_height = 0;
    U256 get_xfg_locked(const std::array<uint8_t, 32>& voter) const override {
      // Simulate: lock of 1000 XFG expires at height 500
      if (current_height < 500) return U256::from_u64(1000);
      return U256::zero();
    }
  };
  std::vector<uint8_t> ops;
  ops.push_back(0x02);
  std::array<uint8_t, 32> voter{};
  ops.insert(ops.end(), voter.begin(), voter.end());
  ops.push_back(0x60); // OP_XFG_LOCKED
  auto bc = Bytecode::create(0x01, 10000, ops);

  ExpiryStub state;
  ScriptContext ctx{};
  ctx.state = &state;

  // Before expiry
  state.current_height = 499;
  ctx.block_height = 499;
  auto result1 = VM::execute(bc, ctx);
  ASSERT_TRUE(result1.has_value());
  EXPECT_EQ(result1->to_u64(), 1000u);

  // At expiry
  state.current_height = 500;
  ctx.block_height = 500;
  auto result2 = VM::execute(bc, ctx);
  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(result2->to_u64(), 0u);
}

TEST(VMChainState, VoteTallyFromStub) {
  // Custom stub that returns known tally for a proposal
  class TallyStub : public StubChainState {
  public:
    U256 get_vote_tally(const std::array<uint8_t, 32>& pid, uint8_t kind) const override {
      if (pid[0] == 0xBB && kind == 1) return U256::from_u64(7500); // yes votes
      if (pid[0] == 0xBB && kind == 0) return U256::from_u64(2500); // no votes
      return U256::zero();
    }
  };
  // Script: OP_PUSH32 <proposal_id>, OP_PUSH8 1 (yes), OP_VOTE_TALLY
  std::vector<uint8_t> ops;
  ops.push_back(0x02); // OP_PUSH32
  std::array<uint8_t, 32> pid{};
  pid[0] = 0xBB;
  ops.insert(ops.end(), pid.begin(), pid.end());
  ops.push_back(0x01); ops.push_back(1); // OP_PUSH8 1 (vote_kind = yes)
  ops.push_back(0x71); // OP_VOTE_TALLY
  auto bc = Bytecode::create(0x01, 10000, ops);
  TallyStub state;
  ScriptContext ctx{};
  ctx.state = &state;
  auto result = VM::execute(bc, ctx);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->to_u64(), 7500u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: unimplemented opcodes in dispatch.

- [ ] **Step 3: Implement chain-state opcodes**

Add dispatch cases for all 10 chain-state + DAO opcodes:
- `OP_BLOCK_HEIGHT`: push `ctx.block_height`
- `OP_BLOCK_TIME`: push `ctx.block_timestamp`
- `OP_BLOCK_HASH n`: push `ctx.recent_block_hashes[n-1]`, trap if n ∉ [1,16]
- `OP_XFG_LOCKED`: pop voter_pubkey, push `ctx.state->get_xfg_locked(pubkey)`
- `OP_HEAT_CD_BAL`, `OP_DIGM_BAL`, `OP_CURA_BAL`: similar pattern
- `OP_PROPOSAL_GET`: pop proposal_id, push packed ProposalInfo as U256
- `OP_VOTE_TALLY`: pop (proposal_id, vote_kind), push tally
- `OP_VOTE_COUNT`: pop proposal_id, push count

- [ ] **Step 4: Run tests to verify they pass**

Expected: all chain-state tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/FuegoScript/VM.cpp tests/UnitTests/TestFuegoScriptVMChainState.cpp
git commit -m "feat(v14): implement FuegoScript chain-state and DAO-helper opcodes"
```

---

### Task 9: Per-opcode unit test coverage

**Files:**
- Create: `tests/UnitTests/TestFuegoScriptVMOpcodes.cpp`

- [ ] **Step 1: Write exhaustive per-opcode tests**

One test per opcode covering:
- Normal operation
- Edge cases (zero, max value, boundary)
- Trap conditions (overflow, underflow, div-by-zero, stack underflow)

This is spec item #28. Cover all 45 opcodes × at least 2 tests each = ~90+ test cases.

Group by opcode category:
- Stack: PUSH8, PUSH32, DUP, DROP, SWAP, OVER, PICK
- Arithmetic: ADD, SUB, MUL, DIV, MOD, MIN, MAX (overflow/underflow/div-by-zero traps)
- Comparison: EQ, LT, GT, LE, GE, NOT, AND, OR
- Control: IF/ELSE/ENDIF nesting, LOOP bounds + ENDLOOP, VERIFY, RETURN
- Crypto: HASH256, VERIFY_SIG, MULTISIG, BLS, VRF, MERKLE
- State + DAO: all 10

- [ ] **Step 2: Run all tests**

Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/UnitTests/TestFuegoScriptVMOpcodes.cpp
git commit -m "test(v14): exhaustive per-opcode unit tests for FuegoScript VM"
```

---

### Task 10: VM determinism property tests

**Files:**
- Create: `tests/UnitTests/TestFuegoScriptVMDeterminism.cpp`

Per spec §2.6 and checklist item #7/#29.

- [ ] **Step 1: Write determinism tests**

```cpp
TEST(VMDeterminism, SameInputSameOutput) {
  // Run the same script 1000 times with the same context
  // Verify byte-identical output every time
  // ... test body ...
}

TEST(VMDeterminism, NoFloatingPoint) {
  // Verify no FP instructions in any opcode path
  // (compile-time static assert or runtime verification)
}

TEST(VMDeterminism, U256ArithmeticConsistency) {
  // Property: for random a, b: a + b - b == a (when no overflow)
  // Run with many random u256 pairs
}
```

Note: spec says "run on x86 + ARM, byte-equivalent outputs" — this is a CI concern. The test itself verifies determinism on the build platform; cross-platform verification happens in CI matrix.

- [ ] **Step 2: Run tests**

Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/UnitTests/TestFuegoScriptVMDeterminism.cpp
git commit -m "test(v14): VM determinism property tests for cross-platform consistency"
```

---

## Chunk 3: XFG-Lock construct

### Task 11: XFG-Lock tx_extra tag + struct

**Files:**
- Modify: `src/CryptoNoteCore/TransactionExtra.h`
- Modify: `src/CryptoNoteCore/TransactionExtra.cpp`
- Create: `tests/UnitTests/TestXfgLock.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "CryptoNoteCore/TransactionExtra.h"

TEST(XfgLock, TagDefined) {
  EXPECT_EQ(TX_EXTRA_XFG_LOCK, 0x13);
}

TEST(XfgLock, SerializeRoundtrip) {
  CryptoNote::TransactionExtraXfgLock lock;
  lock.amount_locked = 100000000; // 1 XFG
  lock.lock_until_height = 1500000;
  lock.voter_pubkey = {}; // fill with test key
  lock.lock_purpose = 0;

  std::vector<uint8_t> blob;
  ASSERT_TRUE(CryptoNote::serialize(lock, blob));

  CryptoNote::TransactionExtraXfgLock parsed;
  ASSERT_TRUE(CryptoNote::deserialize(parsed, blob));
  EXPECT_EQ(parsed.amount_locked, 100000000u);
  EXPECT_EQ(parsed.lock_until_height, 1500000u);
  EXPECT_EQ(parsed.lock_purpose, 0u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: `TX_EXTRA_XFG_LOCK` not defined.

- [ ] **Step 3: Add tag + struct**

In `TransactionExtra.h`:

```cpp
#define TX_EXTRA_XFG_LOCK             0x13

struct TransactionExtraXfgLock {
  uint64_t amount_locked;       // XFG amount (must match an output)
  uint32_t lock_until_height;   // earliest spendable height
  Crypto::PublicKey voter_pubkey; // ed25519 key for vote weight
  uint8_t lock_purpose;         // 0 = general DAO voting
};
```

Add serializer in `TransactionExtra.cpp` following existing patterns (look at how `TransactionExtraMergeMiningTag` is serialized).

- [ ] **Step 4: Run tests to verify they pass**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/TransactionExtra.h src/CryptoNoteCore/TransactionExtra.cpp tests/UnitTests/TestXfgLock.cpp
git commit -m "feat(v14): add TX_EXTRA_XFG_LOCK tag and TransactionExtraXfgLock struct"
```

---

### Task 12: Locked-key-images index

**Files:**
- Create: `src/CryptoNoteCore/XfgLockIndex.h`
- Create: `src/CryptoNoteCore/XfgLockIndex.cpp`
- Create: `tests/UnitTests/TestXfgLockIndex.cpp`

Per spec §3.3 and §3.5: maintain a per-voter_pubkey index of locked outputs and their weights.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(XfgLockIndex, AddAndQueryLock) {
  XfgLockIndex index;
  Crypto::KeyImage ki = /* test key image */;
  Crypto::PublicKey voter = /* test voter pubkey */;
  index.add_lock(ki, voter, 100000000, 1500000); // 1 XFG locked until height 1500000
  EXPECT_EQ(index.get_locked_weight(voter, 1400000), 100000000u); // before unlock
  EXPECT_EQ(index.get_locked_weight(voter, 1500000), 0u);         // at unlock height
}

TEST(XfgLockIndex, SpendBlockedBeforeUnlock) {
  XfgLockIndex index;
  Crypto::KeyImage ki = /* test */;
  index.add_lock(ki, /* voter */, 100000000, 1500000);
  EXPECT_TRUE(index.is_spend_blocked(ki, 1400000));   // blocked
  EXPECT_FALSE(index.is_spend_blocked(ki, 1500000));  // allowed
}

TEST(XfgLockIndex, RollbackRemovesLock) {
  XfgLockIndex index;
  Crypto::KeyImage ki = /* test */;
  index.add_lock(ki, /* voter */, 100000000, 1500000);
  index.remove_lock(ki); // rollback
  EXPECT_FALSE(index.is_spend_blocked(ki, 1400000));
}

TEST(XfgLockIndex, MultipleLocksPerVoter) {
  XfgLockIndex index;
  Crypto::PublicKey voter = /* test */;
  index.add_lock(/* ki1 */, voter, 50000000, 1500000);
  index.add_lock(/* ki2 */, voter, 30000000, 1600000);
  EXPECT_EQ(index.get_locked_weight(voter, 1400000), 80000000u);
  EXPECT_EQ(index.get_locked_weight(voter, 1500000), 30000000u); // first expired
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: `XfgLockIndex` not found.

- [ ] **Step 3: Implement XfgLockIndex**

```cpp
class XfgLockIndex {
public:
  void add_lock(const Crypto::KeyImage& ki, const Crypto::PublicKey& voter,
                uint64_t amount, uint32_t lock_until_height);
  void remove_lock(const Crypto::KeyImage& ki); // for reorg rollback
  bool is_spend_blocked(const Crypto::KeyImage& ki, uint32_t current_height) const;
  uint64_t get_locked_weight(const Crypto::PublicKey& voter, uint32_t current_height) const;

private:
  struct LockEntry {
    Crypto::PublicKey voter_pubkey;
    uint64_t amount;
    uint32_t lock_until_height;
  };
  std::unordered_map<Crypto::KeyImage, LockEntry> locks_by_ki;
  std::unordered_map<Crypto::PublicKey, std::vector<Crypto::KeyImage>> ki_by_voter;
};
```

- [ ] **Step 4: Run tests to verify they pass**

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/XfgLockIndex.h src/CryptoNoteCore/XfgLockIndex.cpp tests/UnitTests/TestXfgLockIndex.cpp
git commit -m "feat(v14): add XfgLockIndex for locked-key-images and per-voter weight tracking"
```

---

### Task 13: Wire XFG-Lock into Blockchain validation

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp`
- Modify: `src/CryptoNoteCore/Blockchain.h`
- Create: `tests/UnitTests/TestXfgLockValidation.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(XfgLockValidation, RejectSpendOfLockedOutput) {
  // Construct a tx that tries to spend a locked output before lock_until_height
  // Feed through Blockchain validation
  // Expect rejection
}

TEST(XfgLockValidation, AllowSpendAfterUnlock) {
  // Same setup but current_height >= lock_until_height
  // Expect acceptance
}

TEST(XfgLockValidation, RejectLockWithMismatchedAmount) {
  // Lock tag amount doesn't match any output in the tx
  // Expect rejection
}

TEST(XfgLockValidation, RejectLockInPast) {
  // lock_until_height <= current_height
  // Expect rejection
}
```

- [ ] **Step 2: Run test to verify it fails**

- [ ] **Step 3: Wire into Blockchain**

In `Blockchain.cpp`, find the transaction validation path (likely `Blockchain::checkTransactionInputs` or similar). Add:
1. On tx acceptance: if tx has `TX_EXTRA_XFG_LOCK`, validate fields per spec §3.3 and add to `XfgLockIndex`
2. On spend check: reject if key image is in locked index and `current_height < lock_until_height`
3. On block rollback: remove locks for rolled-back txs

Gate all checks on `block.majorVersion >= BLOCK_MAJOR_VERSION_14`.

- [ ] **Step 4: Run tests to verify they pass**

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/Blockchain.cpp src/CryptoNoteCore/Blockchain.h tests/UnitTests/TestXfgLockValidation.cpp
git commit -m "feat(v14): wire XFG-Lock validation into Blockchain tx checks"
```

---

### Task 14: Sybil and reorg tests for XFG-Lock

**Files:**
- Create: `tests/UnitTests/TestXfgLockEdgeCases.cpp`

Per spec checklist items #31-32.

- [ ] **Step 1: Write tests**

```cpp
TEST(XfgLockSybil, SplitDoesNotMultiplyWeight) {
  // Lock 100 XFG under pubkey A → weight 100
  // Lock 50 XFG under pubkey B + 50 under pubkey C → weight 50 + 50 = 100 total
  // Verify no inflation
}

TEST(XfgLockReorg, LockRolledBackRemovesWeight) {
  // Add lock, verify weight, rollback block, verify weight gone
}

TEST(XfgLockReorg, VoteWeightRecomputedAfterReorg) {
  // Vote cast at height H with weight W
  // Reorg removes the lock tx
  // At proposal close, voter weight = 0
}
```

- [ ] **Step 2: Run tests**

Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/UnitTests/TestXfgLockEdgeCases.cpp
git commit -m "test(v14): sybil and reorg edge-case tests for XFG-Lock"
```

---

## Chunk 4: DAO governance tags + PCM bundle tag

### Task 15: DAO proposal + vote tx_extra tags

**Files:**
- Modify: `src/CryptoNoteCore/TransactionExtra.h`
- Modify: `src/CryptoNoteCore/TransactionExtra.cpp`
- Create: `tests/UnitTests/TestDaoTags.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(DaoTags, TagsDefined) {
  EXPECT_EQ(TX_EXTRA_DAO_PROPOSAL, 0x14);
  EXPECT_EQ(TX_EXTRA_DAO_VOTE, 0x15);
}

TEST(DaoTags, ProposalSerializeRoundtrip) {
  CryptoNote::TransactionExtraDaoProposal prop;
  prop.proposal_id = /* cn_fast_hash(...) */;
  prop.creator_nonce = /* random 32B */;
  prop.voting_open_height = 1400000;
  prop.voting_close_height = 1408192;
  prop.proposal_type = 0;
  prop.quorum_pct = 30;
  prop.threshold_pct = 51;
  prop.action_hash = /* hash */;
  prop.creator_pubkey = /* pubkey */;
  // serialize + deserialize, verify all fields match
}

TEST(DaoTags, VoteSerializeRoundtrip) {
  CryptoNote::TransactionExtraDaoVote vote;
  vote.proposal_id = /* hash */;
  vote.voter_pubkey = /* pubkey */;
  vote.vote = 1; // yes
  vote.signature = /* ed25519 sig */;
  // serialize + deserialize, verify
}
```

- [ ] **Step 2: Run test to verify it fails**

- [ ] **Step 3: Add tag constants + structs + serializers**

Per spec §5.1-5.3. Proposal = 139 bytes, Vote = 129 bytes.

- [ ] **Step 4: Run tests to verify they pass**

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/TransactionExtra.h src/CryptoNoteCore/TransactionExtra.cpp tests/UnitTests/TestDaoTags.cpp
git commit -m "feat(v14): add TX_EXTRA_DAO_PROPOSAL and TX_EXTRA_DAO_VOTE tags"
```

---

### Task 16: DAO proposal + vote validation rules

**Files:**
- Create: `src/CryptoNoteCore/DaoValidation.h`
- Create: `src/CryptoNoteCore/DaoValidation.cpp`
- Modify: `src/CryptoNoteCore/Blockchain.cpp`
- Create: `tests/UnitTests/TestDaoValidation.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(DaoValidation, RejectProposalWithoutLock) {
  // Creator has no XFG-Lock → reject
}

TEST(DaoValidation, RejectProposalWithShortLock) {
  // Creator lock expires before voting_close_height → reject
}

TEST(DaoValidation, RejectProposalVotingWindowTooLong) {
  // voting_close - voting_open > MAX_VOTING_WINDOW → reject
}

TEST(DaoValidation, RejectDuplicateActiveProposal) {
  // Same creator_pubkey already has an active proposal → reject
}

TEST(DaoValidation, RejectProposalIdMismatch) {
  // proposal_id != cn_fast_hash(creator_pubkey || nonce || height) → reject
}

TEST(DaoValidation, AcceptValidProposal) {
  // All checks pass → accept
}

TEST(DaoValidation, RejectDuplicateVote) {
  // Same (proposal_id, voter_pubkey) already voted → reject
}

TEST(DaoValidation, RejectVoteInvalidSignature) {
  // Signature doesn't verify against domain-prefixed message → reject
}

TEST(DaoValidation, RejectVoteNoLock) {
  // Voter has no XFG-Lock weight → reject
}

TEST(DaoValidation, AcceptValidVote) {
  // All checks pass → accept
}
```

Constants:
- `MAX_VOTING_WINDOW = 8192`
- `DAO_PROPOSAL_MIN_LOCK = 100000000` (1 XFG in atomic units — confirm atomic unit scale from codebase)

Vote signature domain: `"fuego-dao-vote-v1" || u32_le(network_id) || proposal_id || u8(vote)`

- [ ] **Step 2: Run test to verify it fails**

- [ ] **Step 3: Implement DaoValidation**

- [ ] **Step 4: Wire into Blockchain.cpp** (gated on `majorVersion >= 14`)

- [ ] **Step 5: Run tests to verify they pass**

- [ ] **Step 6: Commit**

```bash
git add src/CryptoNoteCore/DaoValidation.h src/CryptoNoteCore/DaoValidation.cpp src/CryptoNoteCore/Blockchain.cpp tests/UnitTests/TestDaoValidation.cpp
git commit -m "feat(v14): add DAO proposal and vote validation with domain-prefixed signatures"
```

---

### Task 17: DAO vote + proposal index

**Files:**
- Create: `src/CryptoNoteCore/DaoIndex.h`
- Create: `src/CryptoNoteCore/DaoIndex.cpp`
- Create: `tests/UnitTests/TestDaoIndex.cpp`

Provides the backing store for `OP_VOTE_TALLY`, `OP_VOTE_COUNT`, and `OP_PROPOSAL_GET`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(DaoIndex, TrackProposal) {
  DaoIndex idx;
  idx.add_proposal(/* proposal struct */);
  auto info = idx.get_proposal(/* proposal_id */);
  EXPECT_EQ(info.status, 0); // open
}

TEST(DaoIndex, TallyVotes) {
  DaoIndex idx;
  idx.add_proposal(/* ... */);
  idx.add_vote(/* proposal_id, voter1, yes, weight=500 */);
  idx.add_vote(/* proposal_id, voter2, no, weight=300 */);
  EXPECT_EQ(idx.get_vote_tally(/* id */, 1 /* yes */), 500u);
  EXPECT_EQ(idx.get_vote_tally(/* id */, 0 /* no */), 300u);
  EXPECT_EQ(idx.get_vote_count(/* id */), 2u);
}

TEST(DaoIndex, RejectDuplicateVoter) {
  DaoIndex idx;
  idx.add_proposal(/* ... */);
  idx.add_vote(/* proposal_id, voter1, yes */);
  EXPECT_FALSE(idx.add_vote(/* proposal_id, voter1, no */)); // duplicate
}

TEST(DaoIndex, RollbackRemovesVote) {
  DaoIndex idx;
  idx.add_proposal(/* ... */);
  idx.add_vote(/* proposal_id, voter1, yes */);
  idx.remove_vote(/* proposal_id, voter1 */);
  EXPECT_EQ(idx.get_vote_count(/* id */), 0u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: compilation error — `DaoIndex` not found.

- [ ] **Step 3: Implement DaoIndex**

- [ ] **Step 4: Run tests to verify they pass**

Expected: all 4 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/CryptoNoteCore/DaoIndex.h src/CryptoNoteCore/DaoIndex.cpp tests/UnitTests/TestDaoIndex.cpp
git commit -m "feat(v14): add DaoIndex for proposal tracking and vote tally"
```

---

### Task 18: Wire DaoIndex + XfgLockIndex into ChainStateView

**Files:**
- Create: `src/CryptoNoteCore/FuegoScript/BlockchainChainStateView.h`
- Create: `src/CryptoNoteCore/FuegoScript/BlockchainChainStateView.cpp`
- Create: `tests/UnitTests/TestBlockchainChainStateView.cpp`

This is the concrete implementation of the `ChainStateView` interface (Task 5) that reads from the real `XfgLockIndex` and `DaoIndex`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(BlockchainChainStateView, XfgLockedDelegatesToIndex) {
  // Create XfgLockIndex with known data, create BlockchainChainStateView wrapping it
  // Call get_xfg_locked, verify it returns the index value
}

TEST(BlockchainChainStateView, VoteTallyDelegatesToDaoIndex) {
  // Similar
}
```

- [ ] **Step 2: Implement BlockchainChainStateView**

Adaptor that holds references to `XfgLockIndex`, `DaoIndex`, and current chain state, implementing all 7 `ChainStateView` virtual methods.

- [ ] **Step 3: Run tests, commit**

```bash
git add src/CryptoNoteCore/FuegoScript/BlockchainChainStateView.h src/CryptoNoteCore/FuegoScript/BlockchainChainStateView.cpp tests/UnitTests/TestBlockchainChainStateView.cpp
git commit -m "feat(v14): wire BlockchainChainStateView adaptor to real indices"
```

---

### Task 19: PCM bundle inner tag (0x0B)

**Files:**
- Modify: parent bundle parser (find via `grep -r "TX_EXTRA_PARENT_BUNDLE\|inner.tag\|bundle.*parse" src/`)
- Create: `src/CryptoNoteCore/FuegoScript/PcmTag.h`
- Create: `src/CryptoNoteCore/FuegoScript/PcmTag.cpp`
- Create: `tests/UnitTests/TestPcmTag.cpp`

Per spec §4:

- [ ] **Step 1: Write failing tests**

```cpp
TEST(PcmTag, SerializeRoundtrip) {
  PcmPayload payload;
  payload.script_version = 0x01;
  payload.bytecode = /* valid FuegoScript bytecode */;
  payload.context_hash = /* 32 bytes */;
  payload.commitment = /* 32 bytes */;
  auto blob = payload.serialize();
  auto parsed = PcmPayload::parse(blob);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->script_version, 0x01);
  EXPECT_EQ(parsed->commitment, payload.commitment);
}

TEST(PcmTag, RejectUnknownVersion) {
  // script_version = 0x02 (Cairo, not yet supported) → reject in V14
  PcmPayload payload;
  payload.script_version = 0x02;
  auto blob = payload.serialize();
  auto parsed = PcmPayload::parse(blob);
  EXPECT_FALSE(parsed.has_value());
}

TEST(PcmTag, InnerTagId) {
  EXPECT_EQ(PCM_INNER_TAG, 0x0B);
}
```

- [ ] **Step 2: Implement PcmTag**

Per spec §4.1: `0x0B` inner tag, varint length, then `script_version(1) + bytecode_len(varint) + bytecode + context_hash(32) + commitment(32)`.

- [ ] **Step 3: Wire 0x0B into parent bundle parser**

Find the existing bundle parser (which handles inner tags 0x01-0x07 from V11). Add `0x0B` case.

- [ ] **Step 4: Run tests, commit**

```bash
git add src/CryptoNoteCore/FuegoScript/PcmTag.h src/CryptoNoteCore/FuegoScript/PcmTag.cpp tests/UnitTests/TestPcmTag.cpp
git commit -m "feat(v14): add PCM bundle inner tag 0x0B with payload parser"
```

---

### Task 20: PCM validation in block check

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp`
- Create: `tests/UnitTests/TestPcmValidation.cpp`

Per spec §4.2: for each PCM tag, re-serialize context → hash → compare to `context_hash`, execute bytecode → compare result to `commitment`. Block rejected on mismatch.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(PcmValidation, ValidScriptAccepted) {
  // Build a block with valid PCM tag (correct context_hash and commitment)
  // Run block validation → accepted
}

TEST(PcmValidation, ContextHashMismatchRejected) {
  // Tamper with context_hash → rejected
}

TEST(PcmValidation, CommitmentMismatchRejected) {
  // Tamper with commitment → rejected
}

TEST(PcmValidation, ScriptTrapRejectsBlock) {
  // Bytecode that always traps → block rejected
}

TEST(PcmValidation, PreV14BlocksIgnorePcmTag) {
  // Block with majorVersion < 14 and PCM tag → ignored (not validated)
}
```

- [ ] **Step 2: Wire into Blockchain::checkBlock**

In `Blockchain::checkBlock` (or equivalent), after existing checks, add:
```
if (block.majorVersion >= BLOCK_MAJOR_VERSION_14) {
  // Parse parent bundle for inner tag 0x0B
  // If present: validate context_hash, execute script, verify commitment
}
```

- [ ] **Step 3: Run tests, commit**

```bash
git add src/CryptoNoteCore/Blockchain.cpp tests/UnitTests/TestPcmValidation.cpp
git commit -m "feat(v14): wire PCM script validation into Blockchain::checkBlock"
```

---

## Chunk 5: Integration, wallet, treasury, and fork activation

### Task 21: Full proposal lifecycle integration test

**Files:**
- Create: `tests/UnitTests/TestDaoLifecycle.cpp`

Per spec checklist item #30: end-to-end test covering create → vote → tally → committee spend.

- [ ] **Step 1: Write integration test**

```cpp
TEST(DaoLifecycle, FullCycle) {
  // 1. Create XFG-Lock txs for 3 voters
  // 2. Submit a DAO proposal (treasury spend type)
  // 3. Cast 3 votes (2 yes, 1 no)
  // 4. Advance to voting_close_height
  // 5. Block at close height includes PCM tally script
  // 6. Verify tally script commitment = "passed"
  // 7. Committee multi-sig constructs treasury spend tx
  // 8. Verify treasury tx accepted
}
```

- [ ] **Step 2: Run test**

Expected: PASS after all previous tasks are complete.

- [ ] **Step 3: Commit**

```bash
git add tests/UnitTests/TestDaoLifecycle.cpp
git commit -m "test(v14): full DAO proposal lifecycle integration test"
```

---

### Task 22: Negative / adversarial tests

**Files:**
- Create: `tests/UnitTests/TestNegativeV14.cpp`

Per spec checklist item #31.

- [ ] **Step 1: Write negative tests**

```cpp
TEST(Negative, InvalidBytecodeRejected) { /* bytecode with bad preamble */ }
TEST(Negative, GasExhaustionRejectsBlock) { /* script that uses > MAX_GAS_GLOBAL */ }
TEST(Negative, DoubleVoteRejected) { /* same (proposal_id, voter) twice */ }
TEST(Negative, ExpiredLockVoteRejected) { /* voter lock expires before close */ }
TEST(Negative, MalformedProposalRejected) { /* truncated or oversized fields */ }
TEST(Negative, LockedOutputSpendRejected) { /* spend before lock_until_height */ }
TEST(Negative, ReorgOfLockTxHandled) { /* lock tx reorged out, weight drops */ }
TEST(Negative, DuplicatePcmTagInBundleRejected) {
  // Per spec §4.2: one PCM tag per bundle maximum in V14
  // Block with two 0x0B inner tags in parent bundle → rejected
}
TEST(Negative, LoopGasPreChargedCorrectly) {
  // Per spec §2.5: OP_LOOP n charges 1 + n × body_cost up front
  // Script: OP_LOOP 100 { OP_PUSH8 1, OP_DROP } → body_cost=2, charge=201
  // With gas_hint=200 → should trap (201 > 200)
  // With gas_hint=201 → should succeed
}
```

- [ ] **Step 2: Run tests**

Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/UnitTests/TestNegativeV14.cpp
git commit -m "test(v14): negative and adversarial tests for XFG-Lock, DAO, and PCM"
```

---

### Task 23: Multi-sig treasury constants

**Files:**
- Modify: `src/CryptoNoteConfig.h`

Per spec §6.1 and checklist item #21.

- [ ] **Step 1: Add placeholder committee keys**

```cpp
// V14 DAO treasury committee — placeholder keys, replace before mainnet activation
const std::vector<Crypto::PublicKey> V14_TREASURY_COMMITTEE = {
  // 7 placeholder pubkeys — real keys set after committee selection (spec §11 Q1)
};
const uint8_t V14_TREASURY_THRESHOLD = 5; // 5-of-7
```

- [ ] **Step 2: Commit**

```bash
git add src/CryptoNoteConfig.h
git commit -m "feat(v14): add placeholder V14 treasury committee constants"
```

---

### Task 24: Wallet — refuse to display locked outputs as spendable

**Files:**
- Modify: wallet daemon source (find via `grep -r "spendable\|available.*balance\|getBalance" src/Wallet*/`)
- Create: `tests/UnitTests/TestWalletLockDisplay.cpp`

Per spec checklist item #24.

- [ ] **Step 1: Write failing test**

```cpp
TEST(WalletLock, LockedOutputNotSpendable) {
  // Create wallet with one locked output (lock_until_height in future)
  // Query spendable balance → should exclude locked output
}

TEST(WalletLock, UnlockedOutputSpendable) {
  // Same output but current_height >= lock_until_height
  // Query spendable balance → should include it
}
```

- [ ] **Step 2: Implement balance filtering**

Find the wallet balance computation and add a check: if output has an associated `TX_EXTRA_XFG_LOCK` tag and `lock_until_height > current_height`, exclude from spendable balance (show as "locked" separately).

- [ ] **Step 3: Run tests, commit**

```bash
git commit -m "feat(v14): exclude locked XFG outputs from wallet spendable balance"
```

---

### Task 25: Wallet — XFG-Lock UI + RPC endpoints

**Files:**
- Modify: wallet RPC source
- Create: `tests/UnitTests/TestWalletRpc.cpp`

Per spec checklist items #25-27.

- [ ] **Step 1: Write failing tests for RPC endpoints**

```cpp
TEST(WalletRpc, GetXfgLocks) {
  // Call get_xfg_locks(voter_pubkey) → returns list of active locks
}

TEST(WalletRpc, GetOpenProposals) {
  // Call get_open_proposals() → returns list of active proposals
}

TEST(WalletRpc, CastVote) {
  // Call cast_vote(proposal_id, yes) → creates and submits vote tx
}
```

- [ ] **Step 2: Implement RPC endpoints**

Add three new wallet RPC methods:
- `get_xfg_locks(voter_pubkey)` — queries XfgLockIndex for locks matching pubkey
- `get_open_proposals()` — queries DaoIndex for proposals with `voting_close_height > current_height`
- `cast_vote(proposal_id, vote_kind)` — constructs `TX_EXTRA_DAO_VOTE` tx, signs with voter key, submits

- [ ] **Step 3: Run tests, commit**

```bash
git commit -m "feat(v14): add wallet RPC endpoints for XFG-Lock and DAO operations"
```

---

### Task 26: Example DAO bootstrap scripts

**Files:**
- Create: `docs/dao/treasury-bounty.fuegoscript` (or `.fscript`)
- Create: `docs/dao/dev-fund.fuegoscript`
- Create: `docs/dao/fee-pool-split.fuegoscript`
- Create: `docs/dao/paradio-vote-hybrid.fuegoscript`

Per spec §8 and checklist item #23. These are reference FuegoScript bytecode examples committed alongside V14.

- [ ] **Step 1: Write Treasury Bounty DAO script**

Per spec §8.1 / §9: a tally script that checks quorum + threshold for a treasury spend proposal.

- [ ] **Step 2: Write Dev Fund DAO script**

Per spec §8.2.

- [ ] **Step 3: Write Fee-pool Split script (XFG-Lock-only V14)**

Per spec §8.3.

- [ ] **Step 4: Write Paradio Vote Hybrid script**

Per spec §8.4: combined PVT hashrate weight + XFG-Lock weight.

- [ ] **Step 5: Commit**

```bash
git add docs/dao/
git commit -m "docs(v14): add example DAO bootstrap FuegoScript scripts"
```

---

### Task 27: Fork activation gating

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp`
- Modify: `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp`

Per spec checklist items #33-35. Final sweep to ensure ALL V14 validation paths are gated.

- [ ] **Step 1: Audit all V14 code paths**

Search for all V14-specific code added in Tasks 1-26:
```bash
grep -rn "BLOCK_MAJOR_VERSION_14\|UPGRADE_HEIGHT_V14\|TX_EXTRA_XFG_LOCK\|TX_EXTRA_DAO_PROPOSAL\|TX_EXTRA_DAO_VOTE\|PCM_INNER_TAG\|0x0B" src/
```

Verify every path is gated on `block.majorVersion >= BLOCK_MAJOR_VERSION_14`.

- [ ] **Step 2: Add any missing gates**

- [ ] **Step 3: Write a test that pre-V14 blocks ignore all V14 tags**

```cpp
TEST(ForkActivation, PreV14IgnoresAllV14Tags) {
  // Block with majorVersion=13 containing XFG-Lock, DAO, PCM tags
  // Validation passes (tags ignored, not rejected)
}
```

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(v14): audit and gate all V14 validation paths on majorVersion >= 14"
```

---

### Task 28: Committee operations doc

**Files:**
- Create: `docs/dao/COMMITTEE_OPERATIONS.md`

Per spec checklist item #22.

- [ ] **Step 1: Write committee guide**

Document:
- How signers observe passing proposals (RPC or chain scan)
- How to construct a multi-sig treasury spend tx
- Key management practices
- Rotation procedure via parameter-change proposals

- [ ] **Step 2: Commit**

```bash
git add docs/dao/COMMITTEE_OPERATIONS.md
git commit -m "docs(v14): add DAO committee operations guide"
```

---

## Chunk 6: Final review + merge preparation

### Task 29: Full test suite run

- [ ] **Step 1: Run all FuegoScript tests**

```bash
cmake --build build --target UnitTests && ./build/tests/UnitTests --gtest_filter="*FuegoScript*:*XfgLock*:*Dao*:*Pcm*:*VM*:*U256*:*Opcode*:*Bytecode*:*ForkConstants*"
```

Expected: all tests PASS.

- [ ] **Step 2: Run full test suite**

```bash
./build/tests/UnitTests
```

Expected: no regressions in existing tests.

- [ ] **Step 3: Build on both debug and release**

```bash
cmake --build build-debug && cmake --build build-release
```

Expected: clean compilation, no warnings.

---

### Task 30: Code review

- [ ] **Step 1: Use `superpowers:requesting-code-review` to get a review of all V14 changes**

- [ ] **Step 2: Address review findings**

- [ ] **Step 3: Final commit**

---

## Dependency graph

```
Task 1 (fork constants) — independent, do first

Task 2 (U256) ──→ Task 3 (opcodes) ──→ Task 4 (bytecode)
     │                                      ↓
     └────→ Task 5 (context/interface) ──→ Task 6 (VM core)
                                             ↓         ↓
                                       Task 7 (crypto) Task 8 (chain-state)
                                             ↓         ↓
                                       Task 9 (per-opcode tests) ←── Tasks 4, 6, 7, 8
                                             ↓
                                       Task 10 (determinism tests)

Task 11 (XFG-Lock tag) ──→ Task 12 (lock index) ──→ Task 13 (blockchain wire)
                                                         ↓
                                                   Task 14 (sybil/reorg tests)

Task 15 (DAO tags) ──→ Task 16 (DAO validation) ──→ Task 17 (DAO index)
                                                         ↓
Task 18 (ChainStateView adaptor) ←── Task 12 + Task 17
     ↓
Task 19 (PCM tag) ──→ Task 20 (PCM validation) ←── Task 6 + Task 18

Task 21 (lifecycle test) ←── all of Tasks 1-20
Task 22 (negative tests) ←── all of Tasks 1-20
Task 23 (treasury constants) — independent
Task 24-25 (wallet) ←── Tasks 11-12, 15-17
Task 26 (example scripts) ←── Task 6
Task 27 (fork gating audit) ←── all code tasks
Task 28 (committee doc) — independent
Task 29-30 (final review) — last
```

---

## File inventory

### New files (create)

| Path | Responsibility |
|---|---|
| `src/CryptoNoteCore/FuegoScript/U256.h` | 256-bit unsigned integer type |
| `src/CryptoNoteCore/FuegoScript/U256.cpp` | U256 implementation |
| `src/CryptoNoteCore/FuegoScript/Opcodes.h` | Opcode enum, gas table, validity check |
| `src/CryptoNoteCore/FuegoScript/Bytecode.h` | Bytecode parse/serialize |
| `src/CryptoNoteCore/FuegoScript/Bytecode.cpp` | Bytecode implementation |
| `src/CryptoNoteCore/FuegoScript/ScriptContext.h` | ScriptContext struct |
| `src/CryptoNoteCore/FuegoScript/ChainStateView.h` | Abstract chain-state interface |
| `src/CryptoNoteCore/FuegoScript/VM.h` | VM execution engine |
| `src/CryptoNoteCore/FuegoScript/VM.cpp` | VM implementation (45 opcodes) |
| `src/CryptoNoteCore/FuegoScript/PcmTag.h` | PCM bundle inner tag (0x0B) |
| `src/CryptoNoteCore/FuegoScript/PcmTag.cpp` | PCM tag implementation |
| `src/CryptoNoteCore/FuegoScript/BlockchainChainStateView.h` | Concrete ChainStateView adaptor |
| `src/CryptoNoteCore/FuegoScript/BlockchainChainStateView.cpp` | Adaptor implementation |
| `src/CryptoNoteCore/XfgLockIndex.h` | Locked-key-images index |
| `src/CryptoNoteCore/XfgLockIndex.cpp` | Lock index implementation |
| `src/CryptoNoteCore/DaoValidation.h` | DAO proposal + vote validation |
| `src/CryptoNoteCore/DaoValidation.cpp` | Validation implementation |
| `src/CryptoNoteCore/DaoIndex.h` | Proposal + vote tally index |
| `src/CryptoNoteCore/DaoIndex.cpp` | DAO index implementation |
| `docs/dao/treasury-bounty.fuegoscript` | Example: Treasury Bounty DAO script |
| `docs/dao/dev-fund.fuegoscript` | Example: Dev Fund DAO script |
| `docs/dao/fee-pool-split.fuegoscript` | Example: Fee-pool Split script |
| `docs/dao/paradio-vote-hybrid.fuegoscript` | Example: Paradio Vote Hybrid script |
| `docs/dao/COMMITTEE_OPERATIONS.md` | Committee operations guide |

### Modified files

| Path | Changes |
|---|---|
| `src/CryptoNoteConfig.h` | Add `UPGRADE_HEIGHT_V14`, `BLOCK_MAJOR_VERSION_14`, treasury committee keys |
| `src/CryptoNoteCore/TransactionExtra.h` | Add `TX_EXTRA_XFG_LOCK`, `TX_EXTRA_DAO_PROPOSAL`, `TX_EXTRA_DAO_VOTE` + structs |
| `src/CryptoNoteCore/TransactionExtra.cpp` | Add serializers for new structs |
| `src/CryptoNoteCore/Blockchain.h` | Add `XfgLockIndex` + `DaoIndex` members |
| `src/CryptoNoteCore/Blockchain.cpp` | Wire lock validation, DAO validation, PCM validation (gated on V14) |
| Wallet daemon source (exact files TBD) | Lock-aware balance, new RPC endpoints |
| Parent bundle parser (exact file TBD) | Add `0x0B` inner tag case |
| `CMakeLists.txt` (in relevant dirs) | Add new source files to build |

### Test files (all new)

| Path | Coverage |
|---|---|
| `tests/UnitTests/TestForkConstantsV14.cpp` | Fork constants |
| `tests/UnitTests/TestFuegoScriptU256.cpp` | U256 arithmetic |
| `tests/UnitTests/TestFuegoScriptOpcodes.cpp` | Opcode enum + gas |
| `tests/UnitTests/TestFuegoScriptBytecode.cpp` | Bytecode parser |
| `tests/UnitTests/TestFuegoScriptVM.cpp` | VM core execution |
| `tests/UnitTests/TestFuegoScriptVMCrypto.cpp` | Hash + signature opcodes |
| `tests/UnitTests/TestFuegoScriptVMChainState.cpp` | Chain-state + DAO opcodes |
| `tests/UnitTests/TestFuegoScriptVMOpcodes.cpp` | Exhaustive per-opcode tests |
| `tests/UnitTests/TestFuegoScriptVMDeterminism.cpp` | Determinism property tests |
| `tests/UnitTests/TestXfgLock.cpp` | XFG-Lock tag serialize |
| `tests/UnitTests/TestXfgLockIndex.cpp` | Lock index operations |
| `tests/UnitTests/TestXfgLockValidation.cpp` | Lock validation in Blockchain |
| `tests/UnitTests/TestXfgLockEdgeCases.cpp` | Sybil + reorg tests |
| `tests/UnitTests/TestDaoTags.cpp` | DAO tag serialize |
| `tests/UnitTests/TestDaoValidation.cpp` | DAO validation rules |
| `tests/UnitTests/TestDaoIndex.cpp` | DAO index operations |
| `tests/UnitTests/TestBlockchainChainStateView.cpp` | ChainStateView adaptor |
| `tests/UnitTests/TestPcmTag.cpp` | PCM tag serialize |
| `tests/UnitTests/TestPcmValidation.cpp` | PCM block validation |
| `tests/UnitTests/TestDaoLifecycle.cpp` | Full lifecycle integration |
| `tests/UnitTests/TestNegativeV14.cpp` | Negative / adversarial |
| `tests/UnitTests/TestWalletLockDisplay.cpp` | Wallet lock display |
| `tests/UnitTests/TestWalletRpc.cpp` | Wallet RPC endpoints |
