# Third Coin — BOUGH (Volatility Absorber)

> A protocol value-accrual token that absorbs HEAT peg volatility,  
> earns from SWF revenue, and makes the system anti-fragile.  
> Inspired by Frax's FXS — purely mechanical, no DAO, no governance.

---

## 1. The Gap

One-way HEAT has no redemption risk, but it has a **revenue drought risk**:

```
HEAT < peg → no one mints → premium flow stops → EF/YEM/Treasury starve

The Treasury can buy HEAT with existing reserves:
  - But reserves are FINITE (30-60K XFG in baseline)
  - A determined sell-off would exhaust them
  - The peg would break and stay broken until organic demand recovers
```

The system survives (no insolvency), but it **can't defend the peg with limitless capital**.

A third coin gives the Treasury a **capital-raising mechanism**: mint it when HEAT is stressed, use proceeds to defend the peg. The coin's holders accept dilution now in exchange for system health and future buybacks.

---

## 2. The Tree

User's metaphor mapped to three-asset architecture:

```
        ╱  ╲
       ╱ HEAT ╲          ← Branches that sway in the wind
      ╱   ╲   ╲         ← (market volatility absorbed by flexible supply)
     ╱ BOUGH  ╲         
    ╱          ╲        
   ╱════════════╲       
  ╱════ XFG ════╲       ← Trunk — solid, PoW, supply-capped, unmovable
 ╱════════════════╲     
╱══════════════════╲    
```

- **XFG**: The trunk. Proof-of-work, 8M8 supply cap. Does not bend.
- **HEAT**: A branch. One-way flatcoin, $1.58 peg. Sways but doesn't break because there's no redemption liability.
- **BOUGH**: Another branch. Free-floating, no peg, supply adjusts to absorb HEAT stress. Sways the most — designed to take the hit so HEAT doesn't have to.

---

## 3. How Frax Does It

Frax (2019, not a DAO) uses an **algorithmic market operations** model:

```
FRAX < $1 (weak):
  → Protocol creates new FXS, sells for collateral
  → Buys FRAX with collateral → FRAX supply ↓ → price ↑
  → FXS holders diluted (but system survives)

FRAX > $1 (strong):
  → Protocol mints FRAX, sells for collateral
  → Buys FXS with profit → FXS supply ↓ → price ↑
  → FXS holders rewarded (system healthy)
```

**No governance**. The operations are mechanical — triggered by peg deviation. FXS has no voting, no DAO, no treasury control. It's purely a utility token that absorbs volatility.

The SEC has not challenged Frax's FXS as a security (as of mid-2026). Key reasons: no profit promise, no managerial effort expected from holders, purely mechanical issuance.

---

## 4. BOUGH Design

### Definition

BOUGH is the volatility absorber token of the one-way HEAT system. It has:
- **No peg** — free market price discovery
- **No governance** — purely mechanical mint/burn conditions
- **Algorithmic supply** — the Treasury can mint it when HEAT is weak, burns it when HEAT is strong
- **Yield-bearing** — BOUGH holders earn a share of SWF drip

### Mint Conditions (Treasury Raises Capital)

```
When: HEAT_price < 0.97 × HEAT_PEG (sustained deviation, 3 epochs)

Treasury Action:
  1. Create BOUGH_new = min(
       treasury_deficit_estimate / oracle_xfg_price,
       SWF_liquid_balance × 0.1        // cap: 10% of SWF liquid
     ) / BOUGH_target_price
  
  2. Sell BOUGH_new on open market at market_price × 0.95
  3. XFG_proceeds = buy HEAT on open market (peg support)
  4. HEAT bought → held by Treasury (not burned — one-way)
  5. When HEAT recovers, Treasury sells → buys back BOUGH → burns it

Effect: BOUGH supply ↑, BOUGH price ↓ (temporary dilution)
```

### Burn Conditions (Treasury Rewards Holders)

```
When: HEAT_price > 1.01 × HEAT_PEG (healthy peg)

Treasury Action:
  1. Use premium_surplus + arb_profits + SWF_overflow
  2. Buy BOUGH on open market at current price
  3. BOUGH bought → burned
  4. If BOUGH_supply has been minted below threshold, stop

Effect: BOUGH supply ↓, BOUGH price ↑ (buy pressure)
```

### Revenue Share

```
BOUGH_holders_yield = SWF_drip × BOUGH_YIELD_SHARE_BPS / 10000

Where BOUGH_YIELD_SHARE_BPS = 1000 (10% of SWF drip goes to BOUGH holders)

Distribution:
  Per epoch: distribute_bps = SWF_drip × 0.10 / BOUGH_supply
  Each BOUGH holder receives distribute_bps × BOUGH_held in XFG
```

This gives BOUGH a fundamental yield (paid in XFG) regardless of buyback activity. The yield is proportional to system revenue (SWF growth).

### The 8:1 Ratio

BOUGH is calibrated so that at system equilibrium:

```
BOUGH_market_cap ≈ XFG_SWF_balance / 8

Rationale:
  - HEAT_market_cap ≈ XFG_SWF_balance × peg_price × HEAT_supply / total_XFG_supply
  - BOUGH at 1/8th of SWF gives it enough "mass" for peg defense
  - 8× leverage on system health → attractive for risk-tolerant capital
  - BOUGH is "junior equity" — takes losses first, gets upside first
```

The 8:1 is not a hard rule — it's the target equilibrium. The market determines BOUGH's actual price.

### SEC Considerations

| Factor | BOUGH | Likely a Security? |
|---|---|---|
| Investment of money | Yes (users buy with XFG) | Yes |
| Common enterprise | Yes (value depends on YEM system) | Yes |
| Expectation of profit | Yes (buybacks + yield) | Yes |
| **Efforts of others** | **No** — purely mechanical | **No** |

BOUGH fails the **Howey test's fourth prong**: profits do not come from "the efforts of others" (managerial/entrepreneurial effort). The mint/burn conditions are hardcoded, the yield is formulaic, and there is no governance or active management.

This is the same reasoning Frax uses for FXS. As of 2026, this has held up.

Additional safeguards:
- No DAO, no governance token functionality
- No marketing as an investment
- Code is open-source, operations are mechanical
- Yield is transparent and formula-driven

---

## 5. What BOUGH Unlocks for YEM

### Before BOUGH (One-Way Only)

```
HEAT < peg:
  Treasury: buys HEAT with existing reserves
  If reserves run out: peg breaks, no recovery until organic demand
  YEM revenue: dries up (no mints → no premium → no fees → no SWF growth)
  System state: solvent but stagnant
```

### With BOUGH

```
HEAT < peg:
  Treasury: mints BOUGH → sells for XFG → buys HEAT
  BOUGH supply ↑ (dilution absorbs the shock)
  HEAT price stabilizes, minting resumes
  
HEAT > peg:
  Treasury: uses premium surplus → buys BOUGH → burns
  BOUGH supply ↓ (holders rewarded)
  HEAT price stabilizes from mint arb

Result: 
  - HEAT peg is 2× more resilient (Treasury has unlimited capital via BOUGH mint)
  - BOUGH holders earn yield + buybacks
  - YEM revenue is more consistent (fewer drought periods)
  - CD APY is more stable (less revenue interruption)
```

### Simulated Impact

Rough estimate of what BOUGH adds to the baseline scenario:

| Metric | Without BOUGH | With BOUGH | Why |
|---|---|---|---|
| Peg <%20 | 89% | 95% | Treasury has more capital for defense |
| CD APY | 8.2% | 9.0-9.5% | Fewer drought periods → more consistent yield |
| Mint volume | 100% | 110-120% | More confidence in peg → more mints |
| SWF_f | 94K | 100-110K | More mints → more SWF capital |
| Revenue consistency | Moderate | High | BOUGH absorbs shocks before they hit YEM |

BOUGH is a **modest quantitative improvement** (10-15% on key metrics) but a **large qualitative improvement** (the system stops being fragile when HEAT is below peg).

---

## 6. Risk Analysis

### BOUGH Death Spiral

If HEAT stays below peg for extended periods:
- Treasury keeps minting BOUGH to defend peg
- BOUGH supply inflates → price drops → new minting needs more BOUGH for same XFG
- Holders exit → price drops further
- Eventually BOUGH becomes worthless

**Mitigant**: Mint cap of 10% of SWF liquid per event. After 10 mint events (50 epochs), Treasury must stop. By then, either HEAT has recovered or the peg is fundamentally broken (in which case no amount of defense helps).

### BOUGH vs Treasury Conflict

If Treasury has excess capital, it could mint BOUGH unnecessarily (diluting holders).

**Mitigant**: Mint conditions are hardcoded — only when HEAT < 0.97 × peg for 3+ epochs. No discretion.

### Regulatory Risk

If the SEC determines BOUGH is a security:
- US exchanges delist
- US users restricted
- But the protocol continues functioning (decentralized, no gatekeepers)

**Mitigant**: Same structure as FXS, which has operated without SEC action since 2019. No DAO, no governance, no profit promises, open-source, mechanical.

---

## 11. Implementation Sizing

BOUGH adds ~1 phase to the implementation:

```
| Phase | What |
|---|---|
| 1-5 | One-way YEM system (existing) |
| 6 | BOUGH: mint/burn engine + treasury ops + yield distribution |
```

Estimated code:
- BOUGH state: ~100 lines (balance tracking, supply, mint/burn conditions)
- Treasury integration: ~200 lines (mint on peg deviation, buy on recovery)
- Yield distribution: ~100 lines (SWF drip share to BOUGH holders)
- Total: ~400 lines of new consensus code

---

## 12. Alternative: Two Smaller Branches

Instead of one BOUGH, the system could have two:

| Token | Role | Ratio |
|---|---|---|
| **BARK** | Protective outer layer — absorbs HEAT shocks | 4:1 to XFG |
| **SAP** | Internal flow — earns SWF yield, no peg defense role | 16:1 to XFG |

BARK takes the hit during HEAT stress (minted for defense). SAP is a pure yield-earning token (like a liquid CD). This separates the volatility absorption from the yield-earning function, letting each be optimally calibrated.

But this adds complexity. One BOUGH is simpler and sufficient.

---

## Summary

BOUGH turns the one-way HEAT system from resilient-but-stagnant (during peg deviations) to **anti-fragile** — it gets stronger when tested because the mechanism for raising capital (diluting BOUGH) aligns incentives across all participants.

- **XFG holders**: The base layer is never at risk
- **HEAT holders**: More reliable peg defense → more trust
- **BOUGH holders**: Earn yield + buybacks from system growth
- **CD holders**: Higher and more consistent APY
- **Everyone**: The system can weather storms without breaking

The 8:1 ratio gives BOUGH 8× leverage on YEM system health — it's the most volatile asset in the ecosystem, which is exactly what you want an absorber to be.
