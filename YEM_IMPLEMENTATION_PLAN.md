# YEM (Yield Emission Machine) - Master Implementation Plan

## 1. Vision & Objective
Transform the CD yield model from a passive fee-share into a proactive economic engine. Use Legacy Bond collateral as "paper funding" to seed a Sovereign Wealth Fund (SWF), offering high APYs on HEAT CDs to drive atomic swap volume and ecosystem growth, while maintaining perfect accounting for the eventual repayment of bond holders.

## 2. Core Economic Pillars
- **The SWF (Sovereign Wealth Fund)**: A buffer that smooths yield, prevents "cold starts," and funds Edition CDs.
- **The Debt Repayment Pool**: A dedicated reserve funded by a 8% diversion from all Burn-to-Mint transactions, used specifically to pay bond interest and principal.
- **The 3-CD Hierarchy**: Edition (Fixed), EpochYield (Smoothed), and PureRoll (Raw).
- **The Flywheel**: Legacy Bonds $\rightarrow$ SWF Seed $\rightarrow$ High HEAT APY $\rightarrow$ Increased Volume $\rightarrow$ Higher Organic Fees $\rightarrow$ Bond Repayment.

---

## 3. Implementation Phases

### Phase 0: Foundation (COMPLETED)
- [x] Implement `TX_EXTRA_LEGACY_BOND` (0xCB) and `TX_EXTRA_LEGACY_BOND_CLAIM` (0xCC).
- [x] Implement on-chain validation for bond claims and fee-pool interest payout.
- [x] Implement `migrate_deposit` wallet command.
- [x] Implement `withdraw_bond` wallet command with interest calculation.

### Phase 1: Fee Split & ReBalancer Vault
- [ ] Update `SWAP_FEE_TREASURY_SHARE_PCT` to 16% and `SWAP_FEE_REBALANCER_SHARE_PCT` to 4%.
- [ ] Implement `m_rebalancerVault` state variable in `Blockchain.h`.
- [ ] Remove `CD_YIELD_TREASURY_ROUTE_PCT` routing hack.
- [ ] Update epoch boundary to route 4% of swap fees directly to the ReBalancer vault.

### Phase 2: YEM Core (SWF & Smoothing Engine)
- [ ] Implement `YemState` (swfBalance, rollingRates, lagEpochCounter).
- [ ] Implement the **3-Epoch Lag Period**: Accumulate 100% of CD pool into SWF for first 3 epochs.
- [ ] Implement **Rolling Average Rate**: Calculate target rate based on mean of last 3 epochs.
- [ ] Implement **SWF Smoothing**: Save 60% of surplus; draw from SWF (then Treasury backstop) for deficits.
- [ ] Implement **SWF Drip**: 1% of SWF balance distributed proportionally to EpochYield holders.

### Phase 3: The Debt Repayment Pool (The "Paper" Funding)
- [ ] **Burn Diversion**: Modify Burn-to-Mint logic to divert 8% of burned XFG to the `m_yemDebtRepaymentPool` before routing the remainder to EternalFlame.
- [ ] **Accounting**: Implement "Paper Balance" tracking in `Blockchain.h` for the Debt Pool (tracking credits/debits without needing immediate output creation).
- [ ] **Protocol Issuer Address**: Designate a system-level `YEM_ISSUER_ADDRESS` to hold the "actual" coins associated with the pool.
- [ ] **Coinbase Integration**: Implement a mechanism for the protocol to issue "Debt Repayment" outputs during block production (coinbase) or via system transactions to pay bond holders.

### Phase 4: Time-Tiered Caps
- [ ] Store lock duration per CD in the `Deposit` / `Commitment` structure.
- [ ] Implement linear cap function: `cap(d) = 33% + (min(d, 72) - 1) / 71 × 47%`.
- [ ] Apply these caps during the YEM distribution phase.

### Phase 5: Edition CDs (Fixed-Rate Tranches)
- [ ] Implement `EditionConfig` and the Edition lifecycle state machine.
- [ ] Implement SWF pre-commitment: Edition opens only when `SWF ≥ liability × 0.5`.
- [ ] Implement `create_edition_cd` wallet command.

### Phase 6: PureRoll CDs (The Raw Feed)
- [ ] Implement PureRoll CD type (no smoothing, no caps).
- [ ] Implement `create_pure_cd` wallet command.

### Phase 7: YEM Bond Lifecycle & Staged Repayment
- [ ] Integrate `YemBondIndex` with the Debt Repayment Pool.
- [ ] Implement **Staged Unlock (1 of 4)**: Use the Debt Pool to pay out interest and principal in stages.
- [ ] Implement automated repayment triggers at bond maturity.

### Phase 8: Treasury LP Bootstrapping
- [ ] Implement `TREASURY_LP_CONTRIBUTION_PCT` (5% of treasury $\rightarrow$ Hearth LP).
- [ ] Track `m_protocolLpShares` and route 75% of LP yield to the SWF.
- [ ] Implement bootstrap exit logic.

### Phase 9: Dynamic Mint Premium
- [ ] Implement `computeDynamicPremium()` with slow-TWAP crash detection.
- [ ] Route 50% of mint premium to the SWF.

### Phase 10: Wallet & RPC Integration
- [ ] Implement `yield_status` and `yield_project` commands.
- [ ] Implement `list_editions` and `create_edition_cd` commands.
- [ ] Add RPC endpoints for YEM state, bond listings, and yield projections.

---

## 4. Accounting & Safety Constraints
1.  **Strict Conservation**: $\text{Total Burned} = \text{EternalFlame} + \text{DebtRepaymentPool}$.
2.  **Repayment Priority**: Bond repayment takes precedence over SWF drip and Edition reserves.
3.  **Verification**: Every YEM state change must be reversible on `popBlock`.
4.  **No-Overdraw**: SWF and Debt Pool cannot be drawn below zero; treasury backstop (0.1%/epoch) is the absolute floor.
