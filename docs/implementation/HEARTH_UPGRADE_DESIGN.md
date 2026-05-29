# Fuego Efficiency & Synergy Upgrade
**Hearth Rebalancer, XFG Price Beacon, and CD Market**

This document outlines the finalized design for the three-part ecosystem upgrade bridging the Hearth AMM, SwapXFG daemon, and the Certificate of Deposit (CD) markets.

## 1. Understanding Summary
*   **What we are building:** A 3-part ecosystem upgrade focusing on treasury efficiency, trustless pricing, and CD market liquidity.
*   **Why it exists:** To stop LP dilution from Hearth rebalancing, provide a reliable XFG price to the network without creating feedback loops, and activate the deferred CD secondary market.
*   **Who it is for:** Hearth Liquidity Providers, SwapXFG users, and network developers needing an XFG price oracle.
*   **Key constraints:** Hearth ratio (HEAT/XFG) must not be influenced by the external USD price beacon. Protocol must not dilute existing LPs during rebalancing.

## 2. Decision Log
1.  **Rebalancer Mechanism:** *Decided* to upgrade the protocol treasury to act as a bidirectional market maker. It will use up to 12% of reserves per epoch to *swap* assets in Hearth to maintain the CPI-band, generating 0.3% fees for LPs instead of just single-sided adding.
2.  **Dilution Fallback:** *Decided* that if a one-sided add is absolutely necessary as a last resort (e.g., severe directional imbalance), the newly minted LP shares will be distributed pro-rata to existing users, ensuring zero dilution. If the treasury runs out of HEAT, it can burn its excess XFG to mint new HEAT internally to swap into the pool.
3.  **Price Beacon Independence:** *Decided* the SwapDaemon JSON-RPC price beacon will derive USD value *only* from Exbitron (CEX) and Atomic Swap TWAPs. Hearth will only provide the HEAT/XFG ratio, preventing a circular "echo chamber" price loop.
4.  **Display Convention:** *Decided* SwapXFG cross-chain swaps stay XFG-denominated (`SOL 214K`), while Hearth and the beacon use a HEAT-centric display (`XFG 8.00 H`). 
5.  **P2P CD Market:** *Decided* to finalize the deferred CD market backend by adding P2P relay endpoints (`/getcdoffers`, `/submitcd`, `/acceptcd`) to `fuegod`, utilizing the existing collaborative signature protocol for transfers.

## 3. Assumptions & Open Questions
*   **Assumption:** The Exbitron REST API is accessible and provides a reliable `XFG/USDT` ticker.
*   **Assumption:** The protocol treasury collects enough fee revenue/emissions to sustain its 12% per-epoch market-making budget.
*   **Assumption:** We are keeping `HEAT_STABILITY_MODE = 2` (8:1 full float) or `0` (CPI-banded) as the active network configuration.

---

## 4. Implementation Plan

### Phase 1: Treasury Rebalancer Overhaul (The "Market Maker" Upgrade)
Modify `computeRebalanceAmount` and the epoch logic in `Blockchain.cpp`.
*   Instead of single-sided LP deposits, the protocol executes standard AMM swaps using a max of 12% of treasury reserves per epoch.
*   **If XFG-heavy:** Treasury swaps HEAT for XFG (absorbs XFG, pushes HEAT price down).
*   **If HEAT-heavy:** Treasury swaps XFG for HEAT (absorbs HEAT, pushes HEAT price up).
*   **Fallback:** If out of HEAT, burn excess treasury XFG to mint HEAT, then swap. If one-sided add is absolutely required, allocate resulting LP shares to existing LP providers instead of protocol.

### Phase 2: XFG Internal Price Beacon (SwapDaemon)
Implement `get_xfg_price` JSON-RPC endpoint in SwapDaemon (or enrich `fuegod` `/getswapprice`).
*   **USD Anchor:** Computed strictly from **Exbitron (CEX)** + **Atomic Swap TWAP**.
*   **Internal Ratio:** Sourced from **Hearth Pool** (displayed strictly as `HEAT/XFG` ratio, e.g., 8.00 H).
*   **Derived Output:** Calculate HEAT's USD value implicitly (`XFG_USD / Hearth_Ratio`). Hearth's ratio *never* feeds back into the XFG USD price calculation.
*   **UI Updates:** Update `swapxfg` ticker to show: `XFG 8.00 H  |  XFG $8.28  |  HEAT $1.04`

### Phase 3: P2P CD Secondary Market Backend
Make the stubbed-out CD market in the `swapxfg` TUI fully functional.
*   Add `/getcdoffers`, `/submitcd`, `/acceptcd`, and `/cancelcd` to the `fuegod` RPC server.
*   Add a `CdOfferRelay` (mirroring the existing `SwapOfferRelay`) to handle P2P propagation of CD sell orders.
*   Wire the `swapxfg` Go backend to sign and broadcast these collaborative transfers.
