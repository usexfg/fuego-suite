# XFG Atomic Swap Plan

## Why

TradeOgre is dead. The small privacy coin ecosystem has no good trading venue.
Atomic swaps let users trade trustlessly, no exchange needed.
XMR and XFG are both Ed25519/CryptoNote — this is an advantage we can exploit.
Adding HTLCs also enables swaps with ETH and BCH.

## Constraint Analysis

| Property           | Monero (XMR)         | Fuego (XFG)              |
|--------------------|----------------------|--------------------------|
| Curve              | Ed25519              | Ed25519 (same!)          |
| Scripting          | None                 | None                     |
| Adaptor-sig support| No                   | Yes (v10, TransactionOutputHashLock) |
| Timelocks          | `unlock_time`        | `unlock_time` + `term`   |
| Multisig           | N-of-N (Musig-style) | `MultisignatureOutput`   |
| Ring sigs          | CLSAG (16 decoys)    | Basic ring sig (mixin 8) |
| Block time         | 2 min                | 8 min                    |
| Stealth addresses  | Yes                  | Yes                      |

## Swap Pairs (Priority Order)

1. **XMR ↔ XFG** — Primary pair. Both Ed25519/CryptoNote. XMR side uses adaptor sigs.
2. **ETH ↔ XFG** — EVM has native HTLC support (Solidity contract). High liquidity.
3. **BCH ↔ XFG** — Bitcoin Cash has OP_HASH160/OP_CHECKLOCKTIMEVERIFY. Standard HTLC.

## Protocol: XMR ↔ XFG (COMIT Adaptor Signatures)

**Core problem:** Monero has no scripting. Standard HTLC atomic swaps don't work on the XMR side. We use the proven COMIT protocol — XMR side uses adaptor signatures (DLEQ proofs), XFG side uses the new HTLC output.

```
Alice: has XMR, wants XFG
Bob:   has XFG, wants XMR

          Alice (XMR holder)                    Bob (XFG holder)
          ─────────────────                    ─────────────────

Phase 1 — Negotiate
          ◄──────── amounts, exchange rate, timelocks ────────►

Phase 2 — Key Exchange
    Alice generates:                      Bob generates:
      secret s                              keypair (b, B = b·G)
      keypair (a, A = a·G)
      h = H(s)
          ────── A, h ──────►
          ◄───── B ─────────

Phase 3 — Lock XFG (Bob goes first)
    Bob creates XFG adaptor-sig output:
      amount: agreed XFG amount
      adaptor_lock: h = H(s)
      timeout: T_xfg (e.g., current_height + 180 blocks ≈ 24h)
      recipient: Alice's XFG address
           ◄── XFG adaptor-sig tx ──

    Alice verifies adaptor-sig on XFG chain ✓

Phase 4 — Lock XMR (Alice locks second)
    Alice sends XMR to a joint address:
      spend_key_pub = A + B
      (spending requires both a and b)
      with adaptor: encrypted signature using s
      timeout: T_xmr (e.g., current_height + 720 blocks ≈ 24h)
          ── XMR adaptor-sig tx ──►

    Bob verifies XMR locked ✓

Phase 5 — Claim XFG (Alice reveals s)
    Alice spends the XFG HTLC by providing preimage s
    s is now visible on the XFG blockchain
          ── XFG claim tx ──►    (reveals s on-chain)

Phase 6 — Claim XMR (Bob uses s)
    Bob sees s on XFG chain
    Bob computes full XMR spend key: a + b (using s to decrypt adaptor)
    Bob spends the joint XMR address
          ◄── XMR claim tx ──

Timeout paths:
  - If Alice disappears after Phase 3: Bob reclaims XFG after T_xfg
  - If Alice disappears after Phase 4: Bob reclaims XFG after T_xfg,
    Alice reclaims XMR after T_xmr (T_xmr > T_xfg ensures safety)
  - If Bob disappears after Phase 4: Alice reclaims XMR after T_xmr
```

## Protocol: ETH ↔ XFG (Adaptor-Sig)

Both sides have HTLC capability. Straightforward hash-locked swap.

```
Alice: has ETH, wants XFG
Bob:   has XFG, wants ETH

Phase 1 — Bob generates secret s, h = keccak(s)
Phase 2 — Bob locks XFG adaptor-sig output (adaptor_lock=h, timeout=T_xfg, recipient=Alice)
Phase 3 — Alice locks ETH adaptor-sig output (adaptor_lock=h, timeout=T_eth, recipient=Bob)
Phase 4 — Bob claims ETH by revealing s through adaptor-sig flow
Phase 5 — Alice sees s on Ethereum, claims XFG with preimage
Timeout: both sides refund if counterparty disappears
```

ETH-side contract is ~50 lines of Solidity (standard HashedTimelock pattern).
`T_eth < T_xfg` — ETH timeout must expire first (opposite direction from XMR pair since Bob initiates here).

## Protocol: BCH ↔ XFG (Adaptor-Sig)

BCH adaptor-sig uses script-like constructs but leverages adaptor signatures instead of classical HTLC scripting.

BCH adaptor-sig flow: the BCH side locks via adaptor-sig output compatible with the shared pool, mirroring the ETH adaptor-sig flow.

BCH block time = 10 min. Timelocks: T_bch = 144 blocks (~24h), T_xfg = 180 blocks (~24h).

## Timelock Requirements

```
XMR pair: T_xfg < T_xmr   (XFG timeout expires first)
ETH pair: T_eth < T_xfg   (ETH timeout expires first, Bob initiates)
BCH pair: T_bch < T_xfg   (BCH timeout expires first, Bob initiates)

Recommended:
  T_xfg = 180 XFG blocks  ≈ 24 hours  (180 × 8 min)
  T_xmr = 720 XMR blocks  ≈ 24 hours  (720 × 2 min)
  T_eth = 7200 ETH blocks  ≈ 24 hours  (7200 × 12 sec)
  T_bch = 144 BCH blocks   ≈ 24 hours  (144 × 10 min)

  SAFETY_MARGIN = 45 XFG blocks ≈ 6 hours (reorg protection)
```

---

## Privacy: Unified Ring Pool (v10 staging → v11 activation)

### Problem
Adaptor-sig outputs are transparent — anyone can see which output is being claimed/refunded. Ring sigs need 9+ outputs of the same amount as decoys (mixin=8). Few adaptor-sig outputs exist initially → no decoys → no privacy.

### Solution: Shared Commitment Pool

Adaptor-sig outputs are added to the same per-amount ring pool (`m_commitmentOutputs`) as deposit outputs. This is implemented in v10 — adaptor-sig outputs automatically seed the commitment pool.

**How it works:**
- Every `TransactionOutputHashLock` at amount X also creates a `CommitmentOutputRef` in `m_commitmentOutputs[X]`
- `recipientKey` is used as `commitKey` for ring membership
- `getRandomCommitmentOutputsForAmount()` already serves from this pool — no new RPC needed
- EFier staking seeds 40+ outputs per amount tier → immediate mixin coverage

**What this hides (when adaptor-sig outputs land in v11):**
- Which specific output is being spent (hidden in ring of deposits + adaptor-sigs)
- Link between adaptor-sig creator and claimer (ring sigs break the graph)
- Amount (once BP+ lands)

**What remains visible:**
- The preimage (inherent to cross-chain atomic swap protocol)
- That a claim/refund is happening (input type reveals this)

**Implemented in:** `Blockchain.cpp` — pushTransaction, rebuildCache, popTransaction all maintain the shared pool.

---

## Price Oracle (Phase 3)

The swap protocol enforces atomicity but not price. The swap daemon needs a price oracle to validate proposed exchange rates.

### Sources

| Source | Type | Data | Notes |
|--------|------|------|-------|
| **Exbitron** | CEX | XFG/USDT direct | Primary XFG price. Query REST API. |
| **HEAT DEX token** | DEX | HEAT/ETH on Uniswap | HEAT is minted 1:1 from burned XFG. Confirms/cross-references CEX price. |
| **Chainlink / Uniswap TWAP** | Oracle/DEX | ETH/USD, BCH/USD | For cross-pair conversion. TWAP resists flash-loan manipulation. |

### Price Validation

The swap daemon doesn't set the price — each side independently validates the proposed rate:

```
1. Counterparty proposes: "I'll give X ETH for Y XFG"
2. Daemon queries both sources:
   - exbitron_xfg_usd = Exbitron XFG/USDT price
   - heat_xfg_usd    = HEAT/ETH × ETH/USD (cross-reference)
   - Use lower of the two (conservative)
3. Compute fair rate and compare against proposal
4. Accept if within slippage tolerance, reject otherwise
```

### Configuration

```
[price]
sources = ["exbitron", "heat_dex"]
slippage_tolerance_pct = 5
staleness_max_seconds = 300
eth_usd_source = "uniswap_twap"   # or "chainlink"
```

If either source hasn't updated in >staleness_max_seconds, widen slippage tolerance or refuse to swap.

---

## Implementation Plan

### Phase 1: Adaptor-Sig Output Type (consensus) — DONE ✓

New structures added to XFG protocol in v10:
- `TransactionOutputHashLock` (recipientKey, refundKey, hashLock, timeoutHeight)
- `TransactionInputHashLockClaim` (amount, outputIndex, preimage)
- `TransactionInputHashLockRefund` (amount, outputIndex)
- Validation: preimage check, timeout enforcement, Ed25519 signatures
- Unified ring pool: adaptor-sig outputs share `m_commitmentOutputs` with deposits

### Phase 2: Wallet adaptor-sig Commands — DONE ✓

SimpleWallet commands implemented:
- `create_adaptor_swap <amount> <recipient_pubkey> <preimage_hex> <timeout_blocks>`
- `claim_adaptor_swap <swap_index> <amount> <preimage_hex>`
- `refund_adaptor_swap <swap_index> <amount>`

- RPC endpoints:
- `/getadaptorout` — query adaptor-sig output by index
- `/getadaptoroutcount` — total adaptor-sig output count
- `/getrandom_commitment_outs.bin` — serves decoys from unified pool (deposits + adaptor-sigs)

### Phase 3: Swap Daemon (`xfg-swap`)

C++ daemon that orchestrates the swap protocol:

```
xfg-swap/
├── src/
│   ├── main.cpp              -- CLI entry point
│   ├── protocol/
│   │   ├── state_machine.cpp -- swap state transitions
│   │   ├── messages.cpp      -- p2p protocol messages
│   │   └── timeouts.cpp      -- timeout monitoring
│   ├── monero/
│   │   ├── rpc_client.cpp    -- talks to monerod RPC
│   │   ├── wallet_rpc.cpp    -- talks to monero-wallet-rpc
│   │   └── adaptor_sig.cpp   -- DLEQ proofs for XMR side
│   ├── ethereum/
│   │   ├── rpc_client.cpp    -- talks to ETH node (JSON-RPC)
│   │   ├── htlc_contract.cpp -- deploy/interact with Solidity HTLC
│   │   └── erc20.cpp         -- optional ERC-20 token swaps
│   ├── bitcoincash/
│   │   ├── rpc_client.cpp    -- talks to BCH node
│   │   └── htlc_script.cpp   -- P2SH HTLC creation/spending
│   ├── fuego/
│   │   ├── rpc_client.cpp    -- talks to fuegod RPC
│   │   ├── htlc.cpp          -- create/claim/refund HTLCs
│   │   └── wallet_rpc.cpp    -- talks to fire_wallet RPC
│   ├── price/
│   │   ├── oracle.cpp        -- weighted price from Exbitron + HEAT DEX
│   │   ├── exbitron.cpp      -- Exbitron REST API client
│   │   └── dex.cpp           -- Uniswap/HEAT DEX price fetcher
│   ├── network/
│   │   ├── discovery.cpp     -- find swap counterparties
│   │   ├── tor.cpp           -- optional Tor transport
│   │   └── p2p.cpp           -- libp2p or ZMQ messaging
│   └── db/
│       └── swap_state.cpp    -- persist swap state (SQLite)
├── contracts/
│   └── HashedTimelock.sol    -- ETH-side HTLC contract
├── proto/
│   └── swap.proto            -- protobuf message definitions
└── CMakeLists.txt
```

**State machine:**
```
              INITIATED
                  │
            ┌─────▼─────┐
            │ XFG_LOCKED │ ◄── Bob creates HTLC
            └─────┬─────┘
                  │ Alice verifies
            ┌─────▼─────┐
            │ CTR_LOCKED │ ◄── Alice locks counterparty chain (XMR/ETH/BCH)
            └─────┬─────┘
                  │ Bob verifies
            ┌─────▼──────┐
            │ XFG_CLAIMED │ ◄── Alice claims XFG (reveals s)
            └─────┬──────┘
                  │ Bob extracts s
            ┌─────▼──────┐
            │ CTR_CLAIMED │ ◄── Bob claims counterparty chain
            └────────────┘

Abort paths:
  XFG_LOCKED ──timeout──► XFG_REFUNDED (Bob reclaims)
  CTR_LOCKED ──timeout──► CTR_REFUNDED (Alice reclaims)
```

**Counterparty discovery:**
1. **Manual** — paste peer address (v1, fine for launch)
2. **Rendezvous server** — simple HTTP server listing swap offers (like UnstoppableSwap)
3. **DHT** — decentralized, libp2p-based (future)

### Phase 4: XMR-side Adaptor Signatures

Cryptographic core for the XMR pair (not needed for ETH/BCH which have native HTLCs):

```cpp
// DLEQ proof: prove that log_G(A) == log_G(A') without revealing the scalar
struct DleqProof {
    EllipticCurveScalar s;
    EllipticCurveScalar e;
};

// Adaptor signature: signature encrypted under a public key
struct AdaptorSignature {
    EllipticCurvePoint  R_hat;   // nonce point (shifted)
    EllipticCurveScalar s_hat;   // partial signature
    DleqProof           proof;   // proves adaptor is well-formed
};
```

**Reference implementation:** [COMIT xmr-btc-swap](https://github.com/comit-network/xmr-btc-swap) `monero-adaptor` crate.

### Phase 5: ETH ↔ XFG Swap Support

- Deploy `HashedTimelock.sol` on Ethereum mainnet
- Add ETH JSON-RPC client to swap daemon
- Price oracle: Exbitron XFG/USDT + HEAT/ETH DEX + Chainlink ETH/USD
- Test on Sepolia testnet + XFG testnet

### Phase 6: BCH ↔ XFG Swap Support

- Add BCH RPC client to swap daemon
- P2SH HTLC script generation and spending
- Test on BCH testnet + XFG testnet

### Phase 7: Testnet Deployment & Hardening

1. Deploy on XFG testnet (p2p:20808 / rpc:28280)
2. Use Monero stagenet, Sepolia, BCH testnet
3. Test all paths per pair:
   - Happy path (both parties cooperate)
   - Alice timeout (disappears after XFG lock)
   - Bob timeout (disappears after counterparty lock)
   - Reorg safety (chain reorgs during swap)
4. Fuzz the HTLC validation
5. Audit the adaptor signature implementation

### Phase 8: Mainnet & Discovery

1. Launch with XMR pair first (highest demand)
2. Add ETH pair second (highest liquidity)
3. Add BCH pair third
4. Launch rendezvous server for swap offers
5. CLI: `xfg-swap buy 1.0 XFG --for 0.001 XMR --pair xmr`
6. Optional: Tor hidden service for privacy

---

## Risk Analysis

| Risk | Severity | Mitigation |
|------|----------|------------|
| HTLC validation bug allows theft | Critical | Extensive testing, formal verification of claim/refund paths |
| Adaptor signature implementation flaw | Critical | Use COMIT's audited Rust implementation as reference |
| ETH contract vulnerability | Critical | Use audited OpenZeppelin HashedTimelock pattern |
| Timelock race (reorg during swap) | High | Conservative safety margins (6h), confirmation requirements |
| Price oracle manipulation | High | Use multiple sources, TWAP, reject stale data |
| Counterparty griefing (lock funds, walk away) | Medium | Minimum timeout ensures funds always recoverable |
| Low liquidity / no counterparties | Medium | Seed initial liquidity, run a market-making bot |
| XMR protocol changes break adaptor sigs | Low | Pin to specific XMR version, monitor upstream |

## Resolved Design Decisions

1. **HTLC privacy:** Unified ring pool — HTLC outputs share `m_commitmentOutputs` with deposits. Ring sig HTLCs activate with v11 (BP+/CLSAG).
2. **Language choice:** C++ (consistent with codebase). Reference COMIT's Rust for adaptor sig correctness.
3. **Fork timing:** HTLC added in v10 (separate from BP+/CLSAG in v11). Ring sig HTLCs activate with v11.
4. **Amount visibility:** Fine for v10 (amounts already visible). Hidden with BP+ in v11.
5. **Swap pairs:** XMR first, then ETH, then BCH. All use XFG-side HTLC.
6. **Price oracle:** Weighted average of Exbitron (CEX) + HEAT DEX token price. Each node validates independently.
