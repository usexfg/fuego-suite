# Fuego HEAT Flatcoin — Final Design v2.0

## What this is

HEAT is Fuego's algorithmic floating-supply colored coin pegged to **constant USD purchasing power as of Jan 2009** (the Bitcoin genesis era). The starting peg of **$1.58** represents the cumulative loss of USD purchasing power from Jan 2009 to the v12 activation epoch. Going forward, the peg adjusts monthly with US CPI so that 1 HEAT always preserves Jan-2009 purchasing power — making HEAT a true flatcoin, not a USD stablecoin.

```
1 HEAT (Jan 2009)  →  $1.00 nominal
1 HEAT (v12 activation)  →  $1.58 nominal  (USD lost 58% PP since Jan 2009)
1 HEAT (10 years post-activation, +30% CPI)  →  $2.05 nominal  (preserves Jan 2009 PP)
```

XFG is the overcollateral asset and oracle source. The XFG/USD price discovered via Fuego's own atomic swap pairs (multi-pair median) drives the redemption math.

## Architecture

```
Oracle ───── Tier 1: Multi-pair SwapXFG TWAP (60s)  ──→ Tier 2: Exbitron CEX (sanity)  ──→ Tier 3: Freeze
               │ ≥4 fresh pairs, vol gates             │ divergence detector only
               │                                       │
Peg ──────── peg_usd = 1.58 × CPI_now / CPI_launch     (monthly BLS update)
             redemption = peg_usd / EMA_oracle_price
               │
Arb ──────── PER-BLOCK: 1-2 rounds, 0.05% convergence, 8bps Hearth fee
               │   Runs at pushBlock(), not epoch boundary
               │   HEAT > peg: mint + sell to pool
               │   HEAT < peg: buy from pool
               │
               │   CRITICAL: per-block (480s) not per-epoch (5 days).
               │   Per-block arb caps drift at 1 block of random swaps.
               │
Hearth ────── 8bps fee → LPs          (traders pay, no SWF subsidy)
               │
Atomic ────── 100% swap fees → CD pool → buys HEAT from pool
               │
OvercollGate ─ Mint paused if XFG / outstanding_HEAT × xfg_oracle_price < OVERCOLL_MIN_RATIO
               │   Default: 1.5x collateralization required to mint
               │
Mint ─────── 8% YEM scalp ──→ remainder split:
               │   15/60/25 TRE/MIN/SWF (tight peg, dev < 1%)
               │   40/20/40 TRE/MIN/SWF (moderate, dev 1-3%)
               │   60/10/30 TRE/MIN/SWF (emergency, dev > 3%)
               │
               │   TWAP asymmetry:
               │   - Mint uses 15-day TWAP (slow, attack-resistant)
               │   - Burn uses 3-day TWAP + 1% extraction fee
               │
MintPrem ──── PI gate: 0% (tight) / 2% (moderate) / 5% (wide) → Treasury
               │
SWF ──────── 5% annual drip → 15% bonds | dyn CD | 30% mining | 30% restake | 10% sovereign reserve
               │   Sovereign reserve: BTC/gold/XMR exposure (long-term backing)
               │   CD boost: higher when atomic volume is low
               │
Treasury ──── Absorption: buys HEAT from pool when pool is cheap
               │   Funded by mint premium + SWF drip
               │
CD ────────── Yield: atomic_fees + SWF_drip_boost
               │   Locked XFG grows/decays based on APY
               │   5% annual floor (SWF drip backup)
               │
Supply ────── 8M8 absolute cap
               │   Mint: XFG → flame → emission recycling
               │   Burn: HEAT → treasury XFG (no inflation)
               │   Net deflationary
               │
CPI ────────── BLS CPI-U fetched monthly by CpiOracleService
               │   Single signer attestation tx (TX_EXTRA_CPI_ATTESTATION)
               │   Sanity bounds: ±10%/year max change, ±0.5% per-month gate
               │   Stale > 90 days: peg freezes at last known value
               │
BRIDGE ────── Protocol-controlled native CD account (no external signer)
               │   Mint wrapped HEAT against zk-proof of native HEAT burn
               │   Bridge wallet earns CD yield → passes to wrapped holders
               │   No oracle/peg/SWF needed on external chain
               │   Cross-chain arb tightens peg globally
```

## Core Constants

```cpp
// Mode + peg baseline
const uint8_t  HEAT_STABILITY_MODE          = 4;
const uint64_t HEAT_PEG_INDEX_LAUNCH        = 158;     // $1.58 starting peg
const uint64_t HEAT_PEG_SCALE               = 100;     // index ÷ scale = USD

// CPI tracking (monthly BLS)
const uint64_t HEAT_CPI_LAUNCH_INDEX        = 100;     // baseline at v12 activation
const uint64_t HEAT_CPI_UPDATE_INTERVAL_DAYS = 30;     // monthly cadence
const uint64_t HEAT_CPI_STALE_DAYS          = 90;      // freeze peg after 90 days no update
const uint64_t HEAT_CPI_MAX_MONTHLY_BPS     = 100;     // ±1% max month-over-month change
const uint64_t HEAT_CPI_MAX_ANNUAL_BPS      = 1000;    // ±10% max year-over-year change

// Multi-pair TWAP oracle (Tier 1)
const uint32_t HEAT_ORACLE_MIN_PAIRS        = 4;       // min fresh pairs for Tier 1
const uint64_t HEAT_ORACLE_PAIR_MIN_VOL     = 100000;  // min atomic units volume per epoch per pair
const uint64_t HEAT_ORACLE_PAIR_MAX_MOVE_BPS = 500;    // reject pairs that moved >5% this epoch
const uint32_t HEAT_ORACLE_PAIR_CURE_DAYS   = 30;      // new pairs ineligible for 30 days

// Arb mechanism
const uint64_t HEARTH_FEE_BPS               = 8;       // 0.08%
const uint64_t HEAT_ARB_CONVERGENCE_BPS     = 5;       // 0.05% target
const uint32_t HEAT_ARB_MAX_ROUNDS          = 40;

// Overcollateral gate (NEW)
const uint64_t HEAT_OVERCOLL_MIN_RATIO_BPS  = 15000;   // 1.5x — mint paused if backing < this
const uint64_t HEAT_OVERCOLL_PAUSE_GRACE    = 86400;   // 1 day grace before pausing mint

// TWAP asymmetry (NEW)
const uint32_t HEAT_MINT_TWAP_DAYS          = 15;      // slow side for mint
const uint32_t HEAT_BURN_TWAP_DAYS          = 3;       // fast side for burn
const uint64_t HEAT_BURN_EXTRACTION_FEE_BPS = 100;     // 1% fee on burn-side redemption

// Mint splits + scalp
const uint64_t YEM_BURN_SCALP_BPS           = 800;     // 8% to YEM Reserve

// SWF allocation (NEW — adds sovereign reserve)
const uint64_t SWF_DRIP_ANNUAL_BPS          = 500;     // 5%/yr
const uint64_t SWF_ALLOC_BONDS_PCT          = 15;
const uint64_t SWF_ALLOC_CD_BOOST_PCT       = 15;
const uint64_t SWF_ALLOC_MINING_PCT         = 30;
const uint64_t SWF_ALLOC_RESTAKE_PCT        = 30;
const uint64_t SWF_ALLOC_SOVEREIGN_PCT      = 10;      // BTC/gold/XMR reserve
```

## How $1.58 is justified

USD purchasing power from Jan 2009 to mid-2026 (estimated CPI-U cumulative):
- Jan 2009 CPI-U ≈ 211.143
- v12 activation CPI-U (projected mid-2026) ≈ 333.6
- Ratio: 333.6 / 211.143 ≈ **1.58**

So **$1 in Jan 2009 spending power = $1.58 today**. Pegging HEAT at $1.58 at v12 activation means **1 HEAT = 1 Jan-2009 dollar of purchasing power**. The Jan 2009 anchor matters because it's the Bitcoin genesis epoch — Fuego positions HEAT as the inflation-resistant unit of account that has existed *implicitly* since Bitcoin's launch.

Going forward, the peg adjusts monthly:
```
peg_usd(t) = HEAT_PEG_INDEX_LAUNCH / HEAT_PEG_SCALE × CPI_now(t) / CPI_launch
```

If CPI continues rising 3%/year, in 10 years:
- `peg_usd(t+10y) = 1.58 × 1.34 ≈ $2.12`
- HEAT still equals 1 Jan-2009 dollar of purchasing power
- Holders preserve real value across decades

## Oracle Tier Architecture

```
Tier 1: Multi-pair SwapXFG TWAP — authoritative
   ├─ Each atomic swap pair contributes XFG/USD via CTR_USD triangulation
   ├─ Require ≥ HEAT_ORACLE_MIN_PAIRS (default 4) fresh pairs
   ├─ Per-pair volume gate (≥ HEAT_ORACLE_PAIR_MIN_VOL atomic units / epoch)
   ├─ Per-pair movement gate (reject if TWAP moved >5% this epoch)
   ├─ Pair cure period (30 days minimum age) — anti-pair-listing-attack
   └─ Return median across qualifying pairs
   
Tier 2: Exbitron CEX (now demoted to divergence detector)
   ├─ Continuously polled, compared to Tier 1
   ├─ If |Exbitron - Tier1| / Tier1 > 5% → log warning, no action
   ├─ Used as advisory signal only — never enters consensus
   └─ Survives even if Exbitron geoblocks Fuego nodes
   
Tier 3: Freeze
   ├─ If Tier 1 has < HEAT_ORACLE_MIN_PAIRS fresh pairs after 2 epochs
   ├─ Use last known Tier 1 value
   ├─ Peg redemption uses frozen oracle price + escalating extraction fee
   └─ Resumes Tier 1 when minimum pairs return
```

**This eliminates Exbitron as a single point of failure.** A coordinated attack would need to manipulate ≥3 independent atomic swap markets simultaneously — significantly more expensive than spoofing a single CEX.

## XFG Overcollateralization

XFG is reframed from "burn collateral" to **structural overcollateral**:

```
collateral_ratio = (total_XFG_locked_in_CD × xfg_oracle_price_usd) 
                   / (outstanding_HEAT × peg_usd)

if collateral_ratio < HEAT_OVERCOLL_MIN_RATIO_BPS / 10000:
    pause_mint = true   // can still burn, can still arb
    log_warning("Overcollateral below 1.5x — mint paused")
```

This protects against XFG death spirals:
- If XFG crashes 50%, collateral ratio drops, mint pauses
- Burn still works (releases more XFG per HEAT — protects holders)
- When XFG recovers OR holders burn HEAT to restore ratio, mint resumes
- No new HEAT is issued against insufficient backing

## Redemption Asymmetry (Anti-Manipulation)

```
mint_redemption = peg_usd / EMA_oracle_price[15-day TWAP]
burn_redemption = peg_usd / EMA_oracle_price[3-day TWAP] × (1 - BURN_EXTRACTION_FEE)
```

Attack costs:
- **Mint cheap by pushing XFG up**: requires sustained 15-day pump → enormous capital cost
- **Drain XFG by pushing XFG down**: 3-day pump *and* eat 1% extraction fee → net unprofitable below ~5% sustained spread

Asymmetry makes both directions unprofitable for short-duration attacks while not significantly impacting honest users (who typically don't time their mint/burn to the second).

## CPI Update Mechanism

**Why monthly?** BLS publishes US CPI-U around the 15th of each month. That's the authoritative source. Higher-frequency proxies (Truflation, Pyth commodity baskets) add complexity without commensurate accuracy gain for a *flatcoin* (which doesn't need intra-month precision).

**Architecture:**
```
Off-chain (CpiOracleService — already started in P1.1):
   ├─ Polls api.bls.gov daily checking for new monthly publication
   ├─ Detects monthly release, computes index relative to LAUNCH_CPI
   ├─ Exposes /cpi RPC endpoint (advisory monitoring)
   └─ Triggers signing tool when fresh value detected

On-chain attestation (Phase 2):
   ├─ Single designated CPI signer key (hardcoded pubkey in CryptoNoteConfig.h)
   ├─ Signs (cpiIndex, epochHeight, observationTime) once per month
   ├─ Submits as TX_EXTRA_CPI_ATTESTATION tx
   ├─ Validators verify signature + sanity bounds:
   │    ▸ HEAT_CPI_MAX_MONTHLY_BPS = ±1% MoM change
   │    ▸ HEAT_CPI_MAX_ANNUAL_BPS = ±10% YoY change (rolling)
   │    ▸ One attestation per HEAT_CPI_UPDATE_INTERVAL_DAYS = 30 days
   └─ If accepted: state.cpiCurrentValue updates, peg recomputes

Staleness handling:
   ├─ If no fresh attestation for 90 days: log warning, freeze peg at last value
   ├─ Burn extraction fee escalates 10bps/month past 90 days (incentivize attestor)
   └─ No catastrophic failure — system continues at stale peg
```

**Sovereignty note**: The CPI signer is a centralization point. Mitigations:
1. Signer key holder publishes commitments publicly (monthly attestation is observable)
2. Bounded blast radius — even malicious signer is rate-limited to ±1%/month
3. Community can fork-replace the pubkey if signer misbehaves
4. Long-term: migrate to threshold signing across N geographically distributed parties (Phase 4)

## Bridge Mechanism (Sovereign)

Replaces the previous "Bridge wallet = CD holder" pattern with a protocol-controlled native account:

```
Native Fuego side:
   ├─ Special native account (no human signer, key derived from chain state)
   ├─ Receives HEAT to be bridged via TX_EXTRA_BRIDGE_LOCK
   ├─ Locked HEAT auto-enters CD pool → accrues yield
   ├─ Yield routes to bridge_payout_account
   └─ Provides zk-proof of lock for external chain

External chain side (Base/Solana/Tron):
   ├─ Wrapped HEAT contract accepts zk-proof of native burn
   ├─ Mints wrapped HEAT proportional to native lock
   ├─ Yield distribution: bridge_payout_account broadcasts merkle root daily
   ├─ Wrapped holders claim yield against merkle proofs
   └─ Burn wrapped HEAT → emits event → unlocks native HEAT (via reverse proof)
```

This eliminates the multisig/Elderfier custody risk. The bridge is a protocol-defined rule, not a wallet under human control.

**Trade-off**: Implementing zk-bridge requires SP1 zkVM circuits (already used elsewhere in Fuego per the vision doc). This adds engineering complexity vs. simple multisig but is the only path consistent with sovereignty.

## Epoch Handler (replaces lines 3257-3359 of Blockchain.cpp)

```
At each block (per-block arb):
─────────────────────────────────────────────────

1. ORACLE — Tier 1 multi-pair median XFG/USD
   ├─ Iterate registered swap pairs
   ├─ Filter by volume + movement + age gates
   ├─ Compute median; fall to Tier 2 then Tier 3 if pairs insufficient
   └─ Set m_oraclePrice

2. PEG — peg_usd = HEAT_PEG_INDEX_LAUNCH × cpiCurrent / cpiLaunch / HEAT_PEG_SCALE

3. ARBITRAGE (40 rounds, 0.05% target)
   gap = |pool_heat_price - peg_usd| / peg_usd
   while gap > 0.0005 and rounds < 40:
       if pool_heat_price > peg_usd:  
           if overcollateral_ratio >= 1.5x:
               mint HEAT + sell to pool
           else:
               skip (mint paused)
       else:
           buy HEAT from pool

At epoch boundary (height % epochDuration == 0):
─────────────────────────────────────────────────

4. OVERCOLL CHECK
   collateral_ratio = (total_XFG_locked × oraclePrice) / (outstanding_HEAT × peg_usd)
   if collateral_ratio < HEAT_OVERCOLL_MIN_RATIO_BPS / 10000:
       pause_mint = true (with HEAT_OVERCOLL_PAUSE_GRACE delay)

5. CD YIELD
   100% atomic swap fees → CD pool → buy HEAT from pool
   SWF drip boost (dynamic, higher when atomic volume low)

6. MINT SPLIT + SCALP
   8% YEM Reserve first
   Remainder → TRE/MIN/SWF (dynamic bands)

7. MINT PREMIUM
   PI gate: 0/2/5% → Treasury

8. TREASURY ABSORPTION
   When pool cheap: buy HEAT using treasury XFG

9. SWF DRIP
   5% annual → 15% bonds | 15% dyn CD | 30% mining | 30% restake | 10% sovereign reserve
```

## Attack Resistance

| Attack | Defense |
|--------|---------|
| CEX oracle manipulation | Tier 1 multi-pair median; Exbitron demoted to advisory |
| Single-pair manipulation | Volume + movement + age gates per pair; rejected from median |
| New-pair listing attack | 30-day cure period before pair eligible for oracle |
| Mint manipulation (push XFG up) | 15-day TWAP — requires 2-week sustained pump |
| XFG-drain manipulation (push XFG down) | 3-day TWAP + 1% extraction fee makes unprofitable below 5% spread |
| XFG death spiral | Overcollateral gate pauses mint when ratio < 1.5x |
| Pool drain | Arb caps at 3% per round, 40-round max |
| Wash trading | 8bps fee per side, 16bps round trip |
| SWF drain | 5% annual drip, dynamic CD boost floor, 10% sovereign reserve survives crypto-only crashes |
| Mint premium evasion | PI gate, treasury claims surplus |
| Death spiral | Two-way arb works at any XFG price, overcollateral pause prevents toxic minting |
| Thin pool | Arb scales with pool depth (3% per round) |
| Bridge custody theft | Native protocol account — no human signer to compromise |
| CPI signer compromise | ±1% MoM rate cap + community fork-replace option |
| BLS data unavailable | 90-day stale gate freezes peg, escalating fee incentivizes new attestation |

## Files Changed

**Removed (Mode 4 only — legacy path kept for hardfork compat):**
- computeTargetRatio() → no longer called for Mode 4
- computeNewRedemptionPrice() → no longer called for Mode 4
- computeRebalanceAmount() → replaced by per-block arb
- Protocol rebalancer block → arb handles
- PI controller state updates → frozen
- Basin discovery → no longer updated
- Single-pair TWAP fallback as Tier 1
- Elderfier-based bridge custody references

**Added:**
- `getOraclePrice()` — Tier 1 multi-pair median selection
- Multi-pair oracle gating (volume + movement + age)
- Exbitron divergence detector (Tier 2)
- Per-block arb loop (40 rounds)
- CpiOracleService — BLS-monthly fetcher + median
- TX_EXTRA_CPI_ATTESTATION transaction extra type
- CPI signer attestation validation
- 90-day stale-peg freeze with escalating fee
- XFG overcollateralization check on mint
- 15-day/3-day TWAP asymmetry (mint/burn)
- 1% burn extraction fee
- 8% YEM Reserve scalp before mint split
- Dynamic mint split (3 bands)
- Dynamic SWF drip split with 10% sovereign reserve
- Mint premium PI gate
- LP fee payment per trade (8bps)
- zk-bridge protocol native account
- Wrapped HEAT yield distribution via merkle drops

**Kept:**
- Hearth AMM (AmmPool unchanged)
- HeatMintEngine (consumes redemption price as input)
- CD buyback (uses redemption price)
- BankingIndex / ethereal_xfg / emission formula
- EpochStateSnapshot (extended with new fields)
- popBlock reversal
- PiControllerState (serialized, not updated in Mode 4)

## Activation

```cpp
const uint32_t UPGRADE_HEIGHT_V12 = 1500000;  // Resolution: Mode 4 + YEM v3 + multi-pair oracle + flat-peg CPI tracking
```

At activation:
1. `HEAT_STABILITY_MODE` switches from 2 → 4
2. First CPI attestation tx sets `cpiLaunchValue` snapshot
3. `peg_usd` = $1.58 = "1 Jan-2009 dollar"
4. Per-block arb begins immediately
5. Overcollateral gate activates
6. CPI updates monthly thereafter

## Open items (Phase 4+ work)

- **Threshold CPI signing** — migrate from single signer to k-of-n threshold (rate-limited multisig)
- **zk-bridge implementation** — SP1 zkVM circuits for cross-chain HEAT lock/mint proofs
- **Cross-chain HEAT pools tracking** — pull pool prices from Base/Sol/Tron/Arb as additional Tier 1 inputs
- **Sovereign reserve custody** — solve trust-minimized BTC/gold/XMR custody for the 10% SWF allocation
- **Pyth Network sanity oracle** — optional Tier 2 cross-check (XFG-on-Sui pair would enable this)

## Companion documents

- `docs/design/HEAT_FLATCOIN_FORMULA.md` — formula reference
- `docs/design/SWAPXFG_CHAIN_RECOMMENDATIONS.md` — atomic swap chain priority list
- `docs/superpowers/plans/2026-05-27-heat-flatcoin-implementation.md` — phased dev plan
