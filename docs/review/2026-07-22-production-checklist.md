# Production checklist status — 2026-07-22

**Worktree:** `/Users/aejt/.grok/worktrees/aejt-xfgo/sd`  
**Build tree:** `build_wt` (fresh cmake, sources = this worktree)  
**Binaries:** `build_wt/src/xfg-swapd`, `build_wt/src/fuegod`

## Automated gates (unit)

| Gate | Evidence | Status |
|------|----------|--------|
| Fresh cmake/link `xfg-swapd` + `fuegod` from worktree | `build_wt` configure + ninja link | **GREEN** |
| Crash/restart secrets recover | `test_production_gates` (serialize→decrypt) | **GREEN** |
| Fail-closed persist without enc key | same test | **GREEN** |
| Encrypted blob load bugfix | `fromHex` not `podFromHex(vector)` | **GREEN** |
| Offer rebroadcast mutated rate rejected | `test_production_gates` | **GREEN** |
| Soft-order foreign maker gate | `test_production_gates` | **GREEN** |
| `timelockOrderingOk` inverted reject | `test_production_gates` | **GREEN** |
| Adaptor aggregate/extract roundtrip | `test_adaptor_roundtrip` 3/3 | **GREEN** |
| Hashlock H(t) not T | `test_swap_hashlock` 4/4 | **GREEN** |
| BCH claim preimage parse | `test_bch_htlc` + `test_bch_chain_client` | **GREEN** |
| SPV state machine + serial | `test_swap_state_machine_spv` 10/10 | **GREEN** |
| DCR client unit | `test_dcr_chain_client` | **GREEN** |
| XMR sweep sequence | `test_xmr_sweep_sequence` | **GREEN** |
| Full automated suite this session | **15/15 unit targets + local smoke 17/17** | **GREEN** |
| ETH contractId / hashlock / RLP offline | `test_eth_protocol` 11/11 | **GREEN** |
| Collaborative ring verifies `check_ring_signature` | `test_ring_collab` size-1 + size-9 | **GREEN** |
| BCH secret-reveal offline (SHA256 H(t) + parse claim) | `test_bch_secret_reveal` 9/9 | **GREEN** |
| XFG spend state machine SECRET→XFG_SPENT | `test_xfg_spend_states` 18/18 | **GREEN** |
| Local `fuegod --testnet` RPC + `xfg-swapd list` | `scripts/smoke-swap-local.sh` | **GREEN** |
| Dual-party XFG protocol (keys→adaptor→ring spend) | `test_xfg_dual_protocol` 4/4 | **GREEN** |
| On-chain fundEscrow local testnet | blocked | **BLOCKED** (miner `mined block failed verification`; MixIn/decoys sparse) |

## Protocol (code)

| Item | Status |
|------|--------|
| Alice-locks protocol in daemon | **GREEN** (Bob claims with `t`, Alice extracts) |
| SOL `tryExtractClaimedSecret` | **GREEN** (getHtlcState) |
| ETH `tryExtractClaimedSecret` | **GREEN** (getClaimedPreimage) |
| BCH `tryExtractClaimedSecret` | **GREEN** (SPV findSpend + parse) |
| DCR `tryExtractClaimedSecret` | **GREEN** (SPV path) |
| P2P SECRET_REVEAL backup | present (optional) |
| `--swap-control-token` | **GREEN** |
| Docker loopback + nginx wallet off | **GREEN** |

## Live chain matrix (on-chain e2e)

| Pair | lock→claim→extract | XFG spend e2e | Notes |
|------|--------------------|---------------|-------|
| SOL | **GREEN (localnet)** | PENDING | 2026-07-22: `solana-test-validator` + deploy + `test_sol_e2e` lock/claim/extract preimage match |
| ETH/ARB/BASE/BNB | **GREEN (anvil)** | PENDING | 2026-07-22: Foundry anvil deploy `HashedTimelock` → lock → claim → getContract preimage; units green |
| BCH | **GREEN (offline path)** | PENDING | full H(t)→claim→extract offline; live BCH node still needed for on-chain |
| XMR | PARTIAL units | PENDING | watch-only verify unit; no live e2e this session |
| DCR | PARTIAL units | PENDING | client unit + tryExtract SPV path; Neutrino TCP incomplete |
| BTC/LTC/KMD | **NO** | **NO** | stubs |
| TON/Zano | **NO** | **NO** | deferred |

## Remaining before mainnet value

1. ~~Live SOL localnet lock/claim/extract~~ **DONE**  
2. ~~Live EVM (anvil) HashedTimelock lock/claim/extract~~ **DONE**  
3. Live BCH chain node claim + extract (offline path already green)  
4. ~~Dual-party XFG crypto spend path~~ **DONE** (`test_xfg_dual_protocol`)  
4b. On-chain fundEscrow: fix testnet miner verification + decoy supply, then two swapd  
5. External review of adaptor + collaborative ring  
6. Always run with `--swap-control-token` + loopback RPC  

**Local smoke:** `./scripts/smoke-swap-local.sh`  
**XFG protocol:** `./scripts/e2e-xfg-local.sh`  

## Critical fix this session

**Crash-restart secret recovery was broken:** `deserialize` used `podFromHex` on `std::vector`, which only wrote `sizeof(vector)` bytes. Fixed to `Common::fromHex`. Production impact: mid-swap restart could not recover `adaptorSecret` / `ourSwapSecKey` even with the correct encryption key.

## Score

| Layer | Score |
|-------|------:|
| Automated fund-safety / protocol unit | **9.5/10** |
| Live multi-chain e2e matrix | **7.5/10** (SOL + ETH live secret-reveal; BCH offline; local fuegod) |
| **Composite mainnet readiness** | **~7.5/10** lab/testnet — not mainnet |

Offline gates green. SOL + ETH **on-chain** secret-reveal proven. BCH secret-reveal offline proven. Collab ring + XFG spend SM green. Local `fuegod`/`xfg-swapd` smoke green. Remaining: funded dual-swapd XFG spend, live BCH node, external review.
