# Fuego Dual-Coin Ecosystem — Implementation Guide

## The Two Assets

| Asset | Role | Supply Model |
|-------|------|-------------|
| **XFG** | Hard-capped reserve & privacy coin (~8M) | Existing. Burned XFG recycles into block rewards via EternalFlame. |
| **HEAT** | Algorithmic colored stablecoin | Floating PI-controlled redemption price (initial 0.2 XFG/HEAT). No hard cap. Minted by burning XFG (one-way, permanent). |

XFG is the collateral. HEAT is the money.

### Key Design Decisions

| Decision | Resolution |
|---|---|
| Coin count | Two (XFG + HEAT). No PARA/volatility sink. |
| HEAT initial redemption price | 0.2 XFG/HEAT (1/5) |
| HEAT mint source | Burn XFG (one-way), tracks into EternalFlame → recycles into block rewards |
| HEAT exit | Sell on Hearth AMM. No reverse-mint. |
| Hearth AMM fee | 1% (100 bps), accumulates for CD yield pool |
| CD yield currency | HEAT CDs pay HEAT (bought with XFG swap fees from Hearth) |
| CD yield split | 80% CD pool / 20% treasury |
| PI controller tuning | Hard-coded at launch |
| PI targeting | Two modes: XFG-only (oracle=0) or USD ($1.00 target via OracleEngine) |
| Oracle feed | Swap TWAPs → XFG/USD median aggregation |
| CD yield reserve | Yes, capped at 2× base pool |

---

## Architecture Overview

```
                    ┌──────────────────────────────────────────┐
                    │           Fuego L1 Consensus             │
                    │                                          │
 XFG               │  ┌─────────────┐    ┌──────────────────┐ │
 (send/receive)    │  │  XFG burns  │    │  HEAT mint tx    │ │
                    │  │  →ethernal  │───→│  (new tx type)   │─┼─→ HEAT outputs
                    │  │  _xfg       │    │  redemptionPrice │ │   (assetId=HEAT)
                    │  └─────────────┘    └──────────────────┘ │
                    │                                          │
                    │  ┌──────────────────────────────────┐    │
                    │  │       Hearth AMM (consensus)      │    │
                    │  │   XFG/HEAT constant-product pool  │    │
                    │  │   swap/LP math + fee accumulation │    │
                    │  └──────────────┬───────────────────┘    │
                    │                 │                         │
                    │                 ▼                         │
                    │  ┌──────────────────────────────────┐    │
                    │  │    PI Controller (epoch boundary) │    │
                    │  │    reads TWAP → adjusts           │    │
                    │  │    redemptionPrice + CD spendRate │    │
                    │  └──────────────┬───────────────────┘    │
                    │                 │                         │
                    │      ┌──────────┴──────────┐             │
                    │      ▼                     ▼             │
                    │  Treasury (20%)    HEAT CD Pool (80%)    │
                    │                           │              │
                    │                    buy HEAT from          │
                    │                    Hearth → distribute    │
                    │                    to HEAT CD holders     │
                    └──────────────────────────────────────────┘
```

---

## File Inventory

### New Files (13)

| File | Purpose |
|------|---------|
| `src/Common/FixedPoint.h` | Q64.64 deterministic math type |
| `src/Common/FixedPoint.cpp` | exp/ln/mul/div/add/sub implementations |
| `src/CryptoNoteCore/AssetId.h` | XFG=0x00, HEAT=0x01 enum |
| `src/CryptoNoteCore/HeatMintEngine.h` | HEAT mint validation (burn→mint ratio) |
| `src/CryptoNoteCore/HeatMintEngine.cpp` | Implementation |
| `src/CryptoNoteCore/AmmPool.h` | Hearth AMM state + math functions |
| `src/CryptoNoteCore/AmmPool.cpp` | Constant-product swap/LP implementation |
| `src/CryptoNoteCore/PiController.h` | PI controller interface with USD-targeting |
| `src/CryptoNoteCore/PiController.cpp` | PI algorithm (deviation, integral, rate, price update) |
| `src/CryptoNoteCore/OracleEngine.h` | XFG/USD price feed from swap TWAPs |
| `src/CryptoNoteCore/OracleEngine.cpp` | Median aggregation of swap pair prices |
| `tests/CoreTests/FixedPoint.cpp` | 24 tests (add/sub/mul/div/exp/ln/determinism) |
| `tests/CoreTests/HeatAmm.cpp` | 18 tests (10 AMM + 7 PI + 1 integration) |

### Modified Files (28)

| File | Changes |
|------|---------|
| `src/CryptoNoteConfig.h` | V10 fork, HEAT/CD tiers, PI gains, Oracle config, TWAP, Epoch/Fee constants |
| `include/CryptoNote.h` | `assetId` on TransactionOutput + KeyInput (v10+) |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | assetId serialization, V10 block header guard |
| `src/CryptoNoteCore/CryptoNoteSerialization.h` | Removed dead commitment type declarations |
| `src/CryptoNoteCore/TransactionExtra.h` | AMM tags (0xF0/F1/F2), structs, builder helpers |
| `src/CryptoNoteCore/TransactionExtra.cpp` | AMM parsing, serialization, visitor handlers, builder implementations |
| `src/CryptoNoteCore/Blockchain.h` | HEAT supply, AMM pool, PI state, TWAP, CD yield state, OracleEngine, V10 detector, public getters |
| `src/CryptoNoteCore/Blockchain.cpp` | encodeAssetAmount, pushTransaction (HEAT+AMM), popTransaction reversal, epoch boundary (PI+CD yield), rebuildCache state, V10 block version, getBlockMajorVersionForHeight V10, upgrade detector V10 pop, OSPEAD dead code removed |
| `src/CryptoNoteCore/Currency.h` | V10 upgrade height member + builder, calculateBankingFee restored |
| `src/CryptoNoteCore/Currency.cpp` | V10 testnet height, upgradeHeight V10 case |
| `src/CryptoNoteCore/Transaction.cpp` | addOutput accepts assetId, getOutputAssetId implementation |
| `src/CryptoNoteCore/CryptoNoteFormatUtils.h` | assetId on TransactionSourceEntry + TransactionDestinationEntry |
| `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp` | assetId set from source/dest entries in constructTransaction |
| `src/CryptoNoteCore/Core.h` | HeatMetrics/AmmQuote/AmmPoolInfo structs, getter methods |
| `src/CryptoNoteCore/Core.cpp` | getHeatMetrics, getAmmQuote, getAmmPoolInfo implementations |
| `src/CryptoNoteCore/TransactionPrefixImpl.cpp` | getOutputAssetId implementation |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | HEAT/AMM RPC structs, getRandomOuts assetId, commitment RPC restored |
| `src/Rpc/RpcServer.h` | HEAT/AMM handler declarations |
| `src/Rpc/RpcServer.cpp` | HEAT/AMM handler implementations + registration |
| `include/ITransaction.h` | Full API restored + addOutput assetId + getOutputAssetId |
| `include/ITransfersContainer.h` | TransactionOutputInformation assetId field |
| `src/WalletLegacy/WalletTransactionSender.cpp` | prepareKeyInputs sets assetId from transfer |
| `src/Wallet/WalletIndices.h` | heatBalance field on WalletRecord |
| `src/Transfers/TransfersConsumer.cpp` | Extract assetId from tx during output discovery |
| `src/SimpleWallet/SimpleWallet.h` | AMM/HEAT command declarations |
| `src/SimpleWallet/SimpleWallet.cpp` | mint_heat, swap, add_liq, remove_liq, heat_info, pool_info, show_balance (per-asset), AssetId include |
| `tui/tview_main.go` | HEAT mint screen, Hearth swap form, pool info, removed old burn/deposit screens |
| `include/IWallet.h` | WalletOrder assetId field |

### Config Constants Added

```
HEAT_INITIAL_REDEMPTION_PRICE_NUM = 1, HEAT_INITIAL_REDEMPTION_PRICE_DENOM = 5
HEAT_TIER_0..3 = 8M/80M/800M/8B (matches XFG AMOUNT_TIER)
HEAT_CD_TIER_0..3 = same values
DEPOSIT_TERM_LP = 0xFFFFFFFD

ORACLE_XFG_PER_USD = 0 (disabled, PI runs in XFG-only mode)

PI_KP_NUM/DENOM = 2/100, PI_KI_NUM/DENOM = 2/10000
PI_MAX_RATE = 50/100, PI_INTEGRAL_CLAMP = 100
BLOCKS_PER_YEAR = 65700

EPOCH_DURATION_BLOCKS = 900, TESTNET_EPOCH = 10
SWAP_FEE_RATE_BPS = 100, SWAP_FEE_RATE_DIVISOR = 10000
SWAP_FEE_CD_SHARE_PCT = 80, SWAP_FEE_TREASURY_SHARE_PCT = 20
```

### Plan Documents

| File | Purpose |
|------|---------|
| `fuego-two-coin-plan.md` | Full phased development plan |
| `fuego-cd-yield-plan.md` | CD yield distribution design |
| `heath-bridge-analysis.md` | Cross-chain HEAT bridging paths |
| `docs/heath-amm-commitment-plan.md` | Privacy commitment output model plan |
| `docs/two-coin-session-context.md` | Compressed session decisions + architecture |

---

## Key Implementation Details

### HEAT Minting

```
Transaction has XFG inputs + HEAT outputs (assetId=1)
Validator: XFG_consumed / redemptionPrice == HEAT_minted
XFG burned → BankingIndex::addForeverDeposit() → ethernal_xfg
HEAT supply tracked in m_heatSupply
```

- Engine: `HeatMintEngine::validateMint()` — enforces `HEAT = XFG / redemptionPrice`
- Accepts FixedPoint64 redemption price (PI-adjusted, not static)
- Fail-safe: HEAT validation failure reverts tx only, never halts chain

### Hearth AMM

```
Constant product: reserveXfg × reserveHeat = K
Swap: output = reserveOut × input × (10000−fee) / (reserveIn×10000 + input×(10000−fee))
Spot price: reserveXfg / reserveHeat × 1e18
LP mint: sqrt(amountA × amountB), proportional to existing shares
```

- Math in `AmmPool.cpp` — all `__int128` intermediates, deterministic
- Pool state in `Blockchain::m_ammPool` — serialized, reorg-safe
- AMM transactions detected via tx_extra tags 0xF0/F1/F2
- Pool bootstrapped at v10 fork: 100 XFG + 500 HEAT

### PI Controller

```
deviation = (marketPrice − redemptionPrice) / redemptionPrice
redemptionRate = Kp × deviation + Ki × integralDeviation
newRedemptionPrice = redemptionPrice × (1 + rate × blocksElapsed / blocksPerYear)
```

- Market price: TWAP from Hearth over epoch (resists single-block manipulation)
- USD targeting: `heatUsdPrice = marketPrice / xfgPerUsd`, targets $1.00
- All math in FixedPoint64 — zero floating-point
- Clamps: integral at ±100%, rate at ±50% annual

### CD Yield Pipeline (Epoch Boundary)

```
80% CD pool:
  spendRate = 1.0 + clamp(redemptionRate, −0.5, +2.0)
  spend = min(pool, pool × spendRate + drawFromReserve)
  Buy HEAT from Hearth: heatReceived = ammGetOutputAmount(spend, reserves)
  heatReceived → HEAT CD fee pool
  Reserve: holds XFG from spendRate < 1.0 periods, capped at 2× pool
20% → Treasury
```

### Per-Asset Decoy Pool

```
encodeAssetAmount(amount, assetId) = (amount << 8) | assetId
```

Used in output index key. HEAT and XFG decoys are separate pools. Privacy-critical — cross-contamination would reveal the real output's asset type in a ring signature.

### TWAP

Accumulator of spot prices over epoch blocks. Divides by block count at epoch boundary. Resists single-block manipulation of the PI controller.

---

## Build & Test

```
22/22 targets: fuegod, fuego-wallet-cli, walletd, optimizer
42/42 tests: 24 FixedPoint + 10 AMM + 7 PI + 1 integration
C++17 standard, boost coroutine + context
Zero floating-point in consensus code
```

## Post-Session: Privacy Commitment Model

`TransactionOutputCommitment` types exist in codebase (DepositCommitment.h, CommitmentIndex.h). HEAT mint + AMM operations should produce commitment outputs for operational privacy — all deposit-type operations share the same on-chain footprint. Plan: `docs/heath-amm-commitment-plan.md`.
