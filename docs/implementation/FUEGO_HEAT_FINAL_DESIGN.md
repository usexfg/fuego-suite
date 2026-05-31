# Fuego HEAT Flatcoin — Final Design v1.0

## Architecture

```
                           ┌─ Exbitron XFG/USDT (60s, Tier 1)
  Oracle ──────────────────┼─ SwapXFG TWAP cross-pair (Tier 2, ≥5 trades)
                           └─ Freeze: last-known-good, SWF→100% treasury absorption

                           ┌─ redemption = HEAT_PEG / EMA_oracle_price (instant)
  Peg (M4E) ───────────────┤
                           └─ Two-way arb: 40 rounds, 0.05% convergence, 8bps Hearth fee

                           ┌─ 100% atomic swap fees → CD pool → buys HEAT from Hearth
  CD Yield ────────────────┤
                           └─ SWF drip boost (dynamic, higher when atomic volume is low)

                           ┌─ 8% → YEM Reserve (scalp, before TRE/MIN/SWF split)
  Mint Split ──────────────┼─ Treasury, Mining, SWF (dynamic, based on pool deviation)
                           └─ Mint premium (PI gate, 0-5%, goes to treasury)

                           ┌─ 5% annual → 20% bonds | dynamic CD | 30% mining | restake
  SWF Drip ────────────────┼─ Pays LP fees (0.3% of volume from SWF, not from trader)
                           └─ Surplus save (60%) for lean cycles
```

## Constants

```cpp
// CryptoNoteConfig.h — Mode 4 constants
const uint8_t  HEAT_STABILITY_MODE       = 4;
const uint64_t HEAT_PEG_INDEX            = 158;    // $1.58 Dec 2008 baseline
const uint64_t HEAT_PEG_SCALE            = 100;
const uint64_t HEARTH_FEE_BPS            = 8;      // 0.08%
const uint64_t HEAT_ARB_CONVERGENCE_BPS  = 5;      // 0.05%
const uint32_t HEAT_ARB_MAX_ROUNDS       = 40;
const uint64_t YEM_BURN_SCALP_BPS        = 800;    // 8%
const uint64_t SWF_DRIP_ANNUAL_BPS       = 500;    // 5%/yr, applied per-epoch

// Dynamic allocation bands (dev = |pool_price - peg| / peg)
// Mint split: Treasury / Mining / SWF
// dev < 1%:  15 / 60 / 25
// dev 1-3%:  40 / 20 / 40
// dev > 3%:  60 / 10 / 30

// Mint premium (PI gate)
// dev < 1%:  0%
// dev 1-3%:  2%
// dev > 3%:  5%
```

## State (Blockchain.h)

```cpp
// Mode 4 fields (new)
uint64_t m_heatPegIndex       = parameters::HEAT_PEG_INDEX;    // 158
uint64_t m_heatPegScale       = parameters::HEAT_PEG_SCALE;    // 100
uint64_t m_lastOraclePrice    = 0;
uint32_t m_lastOracleHeight   = 0;
uint32_t m_oracleStaleEpochs  = 0;
uint64_t m_heatCurrentCpi     = 158;   // updated monthly via signed tx
uint64_t m_heatLaunchCpi      = 100;

// YEM v3 fields (new)
YemState    m_yemState;
YemBondIndex m_yemBonds;
uint64_t    m_rebalancerVault = 0;

// Legacy fields (kept, not updated in Mode 4)
PiControllerState m_piState;          // serialized, never updated
```

## Epoch Handler (Blockchain.cpp)

Replace the entire PI controller + basin + rebalancer block (~lines 3257-3359) with:

```cpp
// Mode 4: Fixed peg with dynamic SWF allocation
if (block.bl.majorVersion >= BLOCK_MAJOR_VERSION_12) {

    // 1. Oracle selection (Tier 1→2→3)
    uint64_t oraclePrice = getOraclePrice(block.height);
    
    // 2. Fixed redemption
    FixedPoint64 peg = FixedPoint64::fromRatio(m_heatPegIndex, m_heatPegScale);
    FixedPoint64 price = FixedPoint64::fromRatio(oraclePrice, VALUE_SCALE);
    m_piState.redemptionPrice = peg.div(price);
    
    // 3. Arbitrage (40 rounds, 0.05% convergence)
    if (!m_ammPool.isEmpty() && oraclePrice > 0) {
        uint32_t rounds = 0;
        while (rounds < HEAT_ARB_MAX_ROUNDS) {
            // ... compute gap, trade direction, execute swap
            if (gap < HEAT_ARB_CONVERGENCE_BPS / 10000.0) break;
            rounds++;
        }
    }
    
    // 4. Dynamic mint split + YEM scalp
    // 5. CD buyback (100% atomic swap fees)
    // 6. SWF drip (5% annual, paid per-epoch)
    // 7. LP fees from SWF
}

// Legacy path (pre-activation height)
else {
    // Existing PI controller logic
}
```

## What Gets Removed (Mode 4 Activation)

- `computeTargetRatio()` call
- `computeNewRedemptionPrice()` call
- `computeRebalanceAmount()` call
- Protocol rebalancer block (lines 3313-3359)
- CPI multiplier block on target ratio (lines 3297-3301)
- PI integral, basin phase updates
- `m_heatLaunchTwap` tracking (stops being set)
- Hill damping
- `CD_YIELD_TREASURY_ROUTE_PCT` routing block

## What Gets Added

- Oracle selection function (`getOraclePrice`)
- EMA smoothing on oracle (α=0.30)
- 40-round arbitrage in epoch handler
- Dynamic mint split based on pool deviation
- Dynamic SWF drip split
- Mint premium PI gate
- LP fee payment from SWF at epoch boundary
- 8% YEM scalp before mint split
- Signed CPI update RPC (`/heat_set_peg_index`)

## What Stays Unchanged

- Hearth AMM (constant product, ammGetOutputAmount)
- CD buyback (uses `m_piState.redemptionPrice` — now set by fixed formula)
- Treasury accumulation
- BankingIndex (addForeverDeposit, ethereal_xfg)
- Emission/block reward formula
- HeatMintEngine (uses redemption price as input)
- EpochStateSnapshot (extended with Mode 4 fields)
- popBlock reversal (restores from snapshot)

## Oracle Chain

```
Exbitron (Tier 1):  poll exbitron.com/api/v2/peatio/public/markets/xfgusdt/tickers (60s)
                    stale if > 2 epochs
SwapXFG (Tier 2):   cross-pair triangulation (SOL/ETH/XMR/BCH rates)
                    stale if < 5 trades or > 7 days
Freeze (Tier 3):    last-known-good oracle, SWF routes 100% to treasury absorption
                    peg stops updating after 10 epochs without fresh data
```

## CPI Update

Monthly BLS CPI-U data → operator signs a `set_peg_index` tx → updates `m_heatPegIndex`.
CLI: `cpi-update --value 159 --signer-key <key>`. Validation: signature check + ±1% change-rate cap.

## ERC20/SLP Bridge

Bridge contract acts as CD holder:
- Lock HEAT on Fuego → bridge wallet deposits into CD
- CD accrues yield → bridge mints equivalent ERC20/SLP HEAT as yield
- Yield distributed to bridged HEAT holders proportional to balance
- No oracle, no peg logic, no SWF on external chains
- Cross-chain arb tightens the peg: external pools follow Fuego via profit motive

## Activation

```cpp
const uint32_t UPGRADE_HEIGHT_V12 = 1500000;  // {Resolution} Mode 4 + YEM v3
```

Pre-activation: existing Mode 2 (PI + CPI multiplier). Post-activation: Mode 4.
30-day notice for node operators. Hardfork height gating.
