# Transforming HEAT into a Stablecoin: Architectural Vision

## Context
Currently in the Fuego ecosystem, HEAT is a cross-chain ERC-20 token minted by permanently burning XFG via a zero-knowledge commitment and bridge mechanism. Currently, HEAT is treated as a speculative reward token for reducing the XFG supply. The Fuego blockchain incorporates confidential deposits (CDs), earning swap fees from LP pools, native atomic swaps, and zero-knowledge privacy out of the box.

Reimagining HEAT as a stablecoin (e.g., pegged to $1 USD) changes the core Fuego value proposition from pure speculation into an expansive, privacy-first DeFi network with a highly useful, stable unit of account.

This document explores how transforming HEAT into a stablecoin could revolutionize the Fuego ecosystem, alongside technical approaches for implementation.

## 1. Ecosystem Impact

### A. Atomic Swaps & Decentralized Exchange
Right now, Fuego's atomic swaps facilitate XFG <-> BTC/ETH trades. The volatility of XFG can inhibit use. If HEAT were a stablecoin natively swapped or supported via atomic swaps, users could privately off-ramp volatile crypto into a stable asset without centralized exchanges. The LP pools could offer XFG/HEAT pairs, capturing massive volume and fees since traders would naturally use HEAT as a safe haven during market volatility.

### B. Utility-Driven Demand for XFG
Presently, users burn XFG to mint HEAT hoping the HEAT token gains speculative value. If HEAT is a stablecoin, XFG becomes the primary collateral asset or stabilization mechanism. To mint 100 HEAT ($100), a user must burn or lock a proportional amount of XFG. This creates a concrete, utility-driven sink for XFG that natively tracks demand for a private, stable unit of exchange.

### C. Enhanced Confidential Deposits (CDs)
Fuego features 5-15% APR on CDs via atomic swap LP fees. If users can hold stable HEAT, or if CDs could be denominated in HEAT, the ecosystem effectively provides a high-yield, privacy-first stablecoin savings account. This competes directly with centralized crypto lending platforms but operates completely on-chain and in zero knowledge.

## 2. Technical Models for HEAT as a Stablecoin

Transforming HEAT into a stablecoin requires migrating away from a simple 1-to-1 "burn XFG = get 1 HEAT" mechanism to a price-aware mechanism that manages supply.

### Model 1: Overcollateralized CDP (Private MakerDAO/DAI)
- **Mechanism:** Users lock XFG into a smart contract (or native Fuego deposit structure) as collateral instead of burning it. They can mint HEAT up to a safe collateralization ratio (e.g., 150%).
- **ZK Implementation:** The user locks XFG into a CDP-type 0xD5 commitment on Fuego. They generate a ZK proof to claim the newly minted HEAT on Ethereum.
- **Liquidation:** If the price of XFG falls, liquidators can pay off the HEAT debt on Ethereum (via a contract) and claim the locked XFG on Fuego via a cross-chain proof.
- **Pros:** Proven model (DAI). Backed by tangible XFG value. XFG is not destroyed, just locked, meaning the user retains their XFG upside.
- **Cons:** Requires cross-chain liquidations, which is highly complex to implement trustlessly via ZK proofs.

### Model 2: Algorithmic Mint/Redeem (Frax/Terra Style)
- **Mechanism:** The system targets a $1 HEAT peg by allowing arbitrary 1-to-1 mints and redemptions based on the current *dollar value* of XFG. If XFG is $10, burning 1 XFG mints 10 HEAT. Conversely, destroying 10 HEAT on Ethereum unlocks or mints 1 XFG on Fuego.
- **ZK Implementation:**
  - *Mint:* User burns $100 worth of XFG. The Fuego blockchain incorporates a decentralized price oracle (as planned in the atomic swap phase 3). The 0xD5 commitment encodes the USD value burned. The ZK proof allows minting exactly 100 HEAT on Ethereum.
  - *Redeem:* User burns 100 HEAT on the Ethereum contract. The contract emits an event. The Fuego ZK Bridge proves this Ethereum event and mints $100 worth of XFG on the Fuego chain to the user.
- **Pros:** Highly scalable. Simple user experience. Generates massive demand for XFG as HEAT adoption grows.
- **Cons:** Death spirals. If XFG price crashes rapidly, massive amounts of XFG are minted to redeem HEAT, hyperinflating XFG.

### Model 3: Yield-Backed / Delta-Neutral
- **Mechanism:** Users deposit XFG into a Fuego Confidential Deposit (CD) that is actively deployed into the LP atomic swap pool. The system hedges the XFG price exposure (potentially via shorting XFG on a DEX or utilizing an options market), resulting in a delta-neutral position. The user is issued HEAT representing the USD value of the deposit.
- **Pros:** Backed by real yield from the LP pools. Does not suffer from algorithmic death spirals.
- **Cons:** Highly complex financial engineering required entirely on-chain. Fuego currently lacks native derivative/shorting markets to execute the delta-neutral hedge.

### Model 4: Protocol Revenue Buyback AMM (The AMO Model)
- **Mechanism:** Maintain the current mechanism where XFG is burned to mint HEAT, but dynamically adjust the exchange rate based on the Fuego price oracle. To maintain the peg, the protocol directs LP pool fees (0.3% per trade) and atomic swap fees to a treasury. If HEAT drops below $1, the treasury buys and burns HEAT on the open market.
- **Pros:** Easier to implement than cross-chain CDPs. Integrates natively with the existing LP/CD fee structure.
- **Cons:** The peg is only as strong as the treasury's revenue. If HEAT supply grows much larger than protocol revenue, the peg could break downward.

## 3. Recommended Approach

The most viable near-term path for Fuego without introducing catastrophic risk is a hybrid of **Model 2 (Price-Aware Mint/Redeem)** combined with strict minting caps and collateral reserves.

1. **Implement Decentralized Price Oracles:** As planned in `ATOMIC_SWAP_PLAN.md`, integrate the Exbitron + DEX TWAP oracle directly into the Fuego consensus or ZK prover.
2. **Dynamic Burn Ratio:** Modify the `0xD5` HEAT burn deposit so that the commitment encodes the *fiat value* of the XFG burned at that specific block height, rather than a raw XFG amount.
3. **ZK Proof Updates:** The SP1 zkVM circuit verifies the block's oracle price and the amount of XFG burned, outputting a commitment for `X HEAT` rather than `X XFG`.
4. **Redemptions (The Hard Part):** To maintain the peg, HEAT must be redeemable. Fuego would need to implement a cross-chain proof where burning HEAT on Ethereum trustlessly mints (or unlocks from a reserve) the equivalent USD value of XFG on Fuego.

By turning HEAT into a private stablecoin, Fuego bridges the gap between total financial privacy and practical, everyday DeFi utility.
