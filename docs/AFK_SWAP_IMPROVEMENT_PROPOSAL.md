# Fuego AFK Swap Improvement Proposal

## 1. Executive Summary

Fuego's atomic swap protocol uses adaptor signatures to allow trustless cross-chain trades without a centralized intermediary. Currently, an **AFK (Away From Keyboard) Maker** must use the `create_afk_lock` RPC command to generate an adaptor point and pre-signature, locking up their XFG on-chain before publishing an offer to the network.

While this ensures cryptographic commitment, it creates capital inefficiency and degrades UX. Makers must tie up funds for every open order. This proposal analyzes competitor approaches and suggests a redesigned "intent-based" orderbook model for `swapxfg` to improve liquidity, convenience, and automation.

## 2. Competitor Analysis

### 2.1 Monero (COMIT / Farcaster)
Monero atomic swaps typically require both parties to be online to negotiate keys and DLEQ proofs interactively. The COMIT protocol relies on a maker-taker model where the maker runs a daemon that responds to takers in real-time. Farcaster attempted to create a more automated matching system.
*   **Takeaway**: Interactive matching requires high uptime, which is difficult for average users.

### 2.2 THORChain
THORChain uses Threshold Signature Schemes (TSS) and continuous liquidity pools (AMMs) rather than discrete orderbooks. Users deposit into pools and earn fees, while traders swap against the pool. The network itself acts as the "always-on" counterparty.
*   **Takeaway**: The passive LP experience is highly convenient but requires complex consensus and network-controlled vaults, which differs from Fuego's peer-to-peer ethos.

### 2.3 Decred DEX
Decred DEX operates an open-source, non-custodial exchange client and server. The server matches orders, but users must leave their DEX client running (online) to execute the on-chain swap transactions once a match is found. If a user is offline, the swap fails and penalizes the user.
*   **Takeaway**: Server-assisted matching with client-side execution is standard but penalizes AFK users.

## 3. Current Fuego Bottlenecks

1.  **Capital Lockup**: In the `AFK_OFFER_LOCKED` state, makers must pre-lock XFG *before* a taker is found. A user wanting to market-make across multiple pairs or prices must segment and lock their portfolio entirely.
2.  **Rigid Timeouts**: The timeout for the lock is set statically. If no one takes the order, the maker must wait for the timeout or explicitly issue a cancel transaction (which costs network fees).
3.  **Taker Friction**: Takers must manually find and execute a specific offer ID.

## 4. Proposed Redesign: Intent-Based P2P Swaps

To achieve true "AFK" trading without capital lockup, we propose separating **Order Intent** from **On-Chain Commitment**, combining it with a robust local background daemon.

### 4.1 "Soft" Orders (The Orderbook)
Instead of calling `create_afk_lock` immediately, makers publish a **Signed Swap Intent** (Soft Order) to the P2P network.
*   The Intent specifies: `Pair`, `Amount`, `Price`, `MakerPubKey`, and an `ExpirationTime`.
*   The Intent is signed by the maker's wallet to prove authenticity but *does not* broadcast a lock transaction to the blockchain.
*   The user's funds remain unlocked in their wallet.

### 4.2 Automated Taker Matching & Execution (The "Always-On" Client)
The core innovation is moving the execution logic into the `SwapDaemon` to act as an automated agent on behalf of the AFK user:
1.  **Taker Accepts**: A taker sees the soft order in the `swapxfg` terminal and clicks "Accept".
2.  **P2P Handshake**: The taker's daemon sends a direct P2P `SWAP_REQUEST` to the maker's daemon.
3.  **Maker Daemon Auto-Locks**: The maker's daemon (running in the background) receives the request, verifies the taker's credentials, and *automatically* executes `create_afk_lock` to generate the adaptor signature and lock the XFG.
4.  **Swap Continues**: The maker's daemon sends the `AdaptorPoint` and `PreSig` back to the taker, who then locks their counterparty coin (`CTR_LOCKED`).

### 4.3 UX Enhancements for `swapxfg`

1.  **Background Mode**: Allow `swapxfg` or `walletd` to run quietly in the background (or system tray). As long as the daemon is running, soft orders are active. If the daemon goes offline, the P2P network drops the soft orders (no capital is stuck).
2.  **Auto-Cancel & Auto-Adjust**: If the market price drifts beyond a user-defined threshold, the daemon automatically updates the soft order's price without incurring on-chain fees.
3.  **Partial Fills**: Because funds aren't pre-locked, the daemon can accept partial fills by generating a lock only for the amount requested by the taker.

## 5. Security & Griefing Protection

Moving to Soft Orders introduces the risk of "Taker Griefing" (a taker pings a maker to lock funds, but never follows through).

**Mitigations:**
*   **Reputation System**: Track taker public keys. If a taker requests a lock but fails to complete the counterparty lock (`CTR_LOCKED`) within a short window (e.g., 15 minutes), the maker's daemon penalizes them.
*   **Short Maker Locks**: Because the taker is already online and ready, the maker's `create_afk_lock` timeout can be very short (e.g., 1 hour), minimizing the duration funds are tied up if the taker abandons the trade.
*   **Proof of Funds / Staking**: Takers could be required to provide a cryptographic proof of counterparty funds (e.g., a signed message from their ETH/XMR address) during the handshake *before* the maker locks XFG.

## 6. Implementation Plan

1.  **Phase 1: Soft Offers in Relay**: Update `SwapOfferRelay.cpp` to support `SWAP_INTENT` messages that do not require an on-chain lock ID.
2.  **Phase 2: Daemon Auto-Execution**: Modify `SwapDaemon.cpp` to listen for `SWAP_REQUEST` messages and auto-trigger `create_afk_lock` for active intents.
3.  **Phase 3: TUI Integration**: Update `swapxfg/app/tui.go` to publish Soft Orders (Intents) instead of calling `create_afk_lock` immediately on the `confirm-offer` command.

### Future Additions
- **Cross-Chain Alias ZK Mapping**: Future updates will research the use of Fuego aliases (e.g. `@alice`) securely mapping to counterparty chain addresses via ZK proofs, providing excellent UX without revealing transparent cross-chain identity on the Fuego chain.
