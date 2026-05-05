# CD/XFG Adaptor Swap Analysis

## Overview
This document analyzes the integration of Certificate of Deposit (CD) atomic swaps into the Fuego Adaptor Swap mechanism.

## Mechanism
The adaptor swap mechanism for CDs allows users to trustlessly exchange Fuego CDs for XFG. Since CDs represent locked XFG with an associated yield, the market naturally prices them based on their maturity, principal, and accrued interest. The swap protocol allows a maker (CD seller) to lock their CD in an HTLC/Musig2 escrow, while the taker (XFG buyer) locks raw XFG.

## Pricing and Price Oracle
- The base market for CD/XFG is essentially 1:1, meaning 1 CD unit corresponds to 1 atomic unit of XFG.
- In `PriceOracle.cpp`, the base seed rate is hardcoded to 1.0.
- The `ctrDivisor` for CD pairs is `1e7`, matching the atomic unit divisor of XFG.

## Fee Structure Exemption
During CD creation, a tier-based "Banking Fee" is charged and permanently burned as a HEAT commitment, supporting network tokenomics. Consequently, when swapping a CD for XFG, the standard 1% sender surcharge on the swap initiation (escrow funding) is skipped (`params.pair == SwapPair::CD` exemption in `SwapDaemon.cpp`) to prevent double-taxing the user.

## Potential Gaps & Holes
1.  **Interest Rate Arbitrage**: If the TWAP for CD/XFG drifts significantly below 1.0 (e.g., 0.9 XFG per CD), arbitrageurs could buy CDs at a discount, wait for maturity, and withdraw the principal + interest for a risk-free profit. The oracle floor checks (`RateCheck::BELOW_FLOOR`) mitigate malicious pricing but may not fully prevent sophisticated TWAP manipulation.
2.  **Maturity Information Asymmetry**: The swap params currently lack an explicit field for CD maturity height. A buyer must verify the CD's `deposit_term` off-band before accepting the swap, otherwise they might buy a 1-year CD thinking it's a 3-month CD.
3.  **Liquidity Fragmentation**: Because every CD has a unique maturity date and interest accrual schedule, standard AMM pools (v11) cannot efficiently aggregate them. They must be traded peer-to-peer via orderbooks.

## Recommendations
- **Explicit Maturity Verification**: Add CD maturity block height and accrued interest to the `SwapParams` struct and enforce validation during `SwapPeerProtocol` negotiation.
- **Dynamic Floor Pricing**: Instead of a static TWAP-based floor for CD/XFG, the oracle should calculate the absolute floor based on `principal + accrued_interest`. Any swap offer pricing a CD below its current liquidatable value should be flagged.
- **Yield-Aware TWAP**: Implement a specialized TWAP curve for CDs that accounts for time decay (similar to zero-coupon bond pricing).
