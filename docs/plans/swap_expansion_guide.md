# Fuego Atomic Swaps Expansion Plan

This document outlines the high-level strategy and technical requirements for expanding Fuego's atomic swap capabilities (currently supporting SOL, ETH, XMR, BCH) via COMIT + MuSig2 adaptor signatures to the requested target chains.

## Core Architectural Requirements

All new swap implementations MUST adhere to Fuego's existing state machine:
1. `ADAPTOR_KEYS_EXCHANGED`
2. `ADAPTOR_ESCROW_FUNDED`
3. `ADAPTOR_PRESIGS_READY`
4. `ADAPTOR_CTR_LOCKED`
5. `ADAPTOR_SECRET_REVEALED`
6. `ADAPTOR_XFG_SPENT` / `ADAPTOR_REFUNDED`

Integration requires that the target chain supports:
1. **Time-locks:** The ability to lock funds until a specific block height or timestamp.
2. **Hash-locks:** The ability to lock funds contingent on the revelation of a preimage to a cryptographic hash.
3. **Signature aggregation compatibility:** For optimal privacy/efficiency, MuSig2/adaptor signature compatibility is preferred, though standard HTLCs can serve as a fallback.

## Phase 1: High Compatibility (UTXO / Bitcoin-derived)

These chains share significant DNA with Bitcoin/BCH and are highly compatible with script-based HTLCs and potentially MuSig2 depending on their specific scripting engine upgrades (e.g., Schnorr support).

### 1. Bitcoin (BTC)
*   **Feasibility:** Very High. The COMIT protocol was primarily designed around Bitcoin.
*   **Implementation Strategy:**
    *   Utilize Taproot (P2TR) for optimal privacy, hiding the swap script unless a refund is necessary.
    *   Alternatively, standard P2WSH or P2SH HTLC scripts.
    *   **Requires:** Adding BTC node RPC integration to `SwapDaemon`, adapting the BCH locking script logic for Bitcoin consensus rules.

### 2. Litecoin (LTC)
*   **Feasibility:** Very High. Nearly identical to Bitcoin.
*   **Implementation Strategy:**
    *   Clone the BTC implementation. Litecoin supports SegWit and has activated MWEB (MimbleWimble Extension Blocks), providing excellent primitives for scriptless scripts or standard HTLCs.

### 3. Decred (DCR) & DCRDEX Insights
*   **Feasibility:** High. Decred is a Bitcoin derivative with a powerful scripting system.
*   **Architectural Analysis (DCRDEX):**
    *   DCRDEX operates on a client-server model for *matching* but remains strictly P2P for *settlement*. The server uses an "epoch-based" matching system to eliminate front-running and miner-extractable value (MEV). 
    *   Settlement utilizes standard, deterministic on-chain HTLCs with `OP_SHA256` and `OP_CHECKLOCKTIMEVERIFY`.
*   **Implementation Strategy:**
    *   Fuego can adapt Decred's highly audited HTLC contract standards for the `SwapDaemon`. 
    *   While Fuego uses LP pools instead of an orderbook server, Fuego can emulate DCRDEX's deterministic transaction construction to fulfill the `ADAPTOR_CTR_LOCKED` and `ADAPTOR_SECRET_REVEALED` states seamlessly.

### 4. Komodo (KMD) & AtomicDEX Insights
*   **Feasibility:** Very High. Komodo is a pioneer in cross-protocol atomic swaps.
*   **Architectural Analysis (AtomicDEX):**
    *   Komodo's DeFi Framework (formerly AtomicDEX) successfully bridges UTXO and EVM chains using purely P2P networks and HTLCs.
    *   Crucially, it relies heavily on **SPV (Simplified Payment Verification) light clients** rather than full nodes.
*   **Implementation Strategy:**
    *   Since KMD is a Zcash/Bitcoin fork, standard HTLC opcodes are available. Fuego can integrate KMD easily via its standard UTXO path.
    *   **Architecture Upgrade:** Fuego's `SwapDaemon` should adopt Komodo's SPV client approach. Requiring the `SwapDaemon` to run a full node for every supported asset (BTC, ETH, LTC, KMD, etc.) is unscalable. Fuego must implement or integrate SPV verification for the counterparty locks (`ADAPTOR_CTR_LOCKED`) to scale safely.

## Phase 2: EVM / Account-Based Expansion

Fuego already supports ETH. Expanding to other EVM chains is relatively straightforward, primarily requiring new RPC endpoints and chain ID configurations.

### 5. Polygon (MATIC/POL)
*   **Feasibility:** Very High.
*   **Implementation Strategy:**
    *   Deploy the exact same HTLC / Swap smart contract currently used for Fuego-ETH swaps onto the Polygon PoS chain.
    *   Update `SwapDaemon` with Polygon RPC endpoints and Polygon's Chain ID.
    *   Gas economics will require adjustment in the fee estimation logic.

### 6. Kaspa (KAS)
*   **Feasibility:** Medium. Kaspa is a UTXO-based blockDAG, not EVM.
*   **Implementation Strategy:**
    *   Kaspa currently has limited smart contract capabilities, but is implementing smart contracts (KRC-20 and beyond).
    *   We must evaluate if Kaspa's current scripting supports necessary `OP_CHECKLOCKTIMEVERIFY` and `OP_SHA256` equivalents. If so, a standard UTXO HTLC applies. If not, this must wait for their smart contract rollout.

## Phase 3: Advanced Privacy / ZK Chains

These chains require bespoke cryptographic integration, similar to the existing XMR implementation.

### 7. Aztec
*   **Feasibility:** High, but complex. Aztec is a privacy-first ZK rollup on Ethereum.
*   **Implementation Strategy:**
    *   Aztec's architecture (Noir language) supports custom logic. We must write a Noir contract that enforces the HTLC logic (time-lock and hash-lock) within the private state.
    *   Requires writing an Aztec RPC client for `SwapDaemon` that can generate and submit ZK proofs for the lock and claim actions.

### 8. Beam
*   **Feasibility:** Medium/High. Beam uses Mimblewimble.
*   **Implementation Strategy:**
    *   Mimblewimble natively supports scriptless scripts and adaptor signatures.
    *   The integration will be heavily cryptographic, requiring Fuego's `src/crypto/musig2.h` to interact with Beam's MW implementation to construct a joint transaction that reveals the secret upon broadcasting.

### 9. Zano
*   **Feasibility:** High. Zano is a CryptoNote/RingCT derivative (like Monero and Fuego itself).
*   **Implementation Strategy:**
    *   Since Fuego already has XMR support, Zano integration should leverage the same architectural patterns.
    *   Zano has recently implemented advanced atomic swap primitives (Zarcanum). We must bridge Zano's specific swap API with Fuego's state machine.

## Phase 4: Non-Standard / Enterprise Architecture

These represent the highest integration friction due to fundamentally different consensus and architectural models.

### 10. IBC / Cosmos (ATOM)
*   **Feasibility:** High, but requires paradigm shift.
*   **Implementation Strategy:**
    *   IBC is designed for message passing, not just HTLCs.
    *   **Option A (HTLC):** Deploy a CosmWasm contract on a target chain (e.g., Osmosis) that acts as the counterparty HTLC.
    *   **Option B (Native IBC):** This is much harder. Fuego is not a Cosmos SDK chain. To interact natively via IBC, Fuego would need to implement an IBC light client within its consensus layer, which is a massive architectural overhaul. Option A is the pragmatic path for swaps.

### 11. Sia (SC)
*   **Feasibility:** Low/Medium. Sia is highly specialized for storage.
*   **Implementation Strategy:**
    *   Sia uses a UTXO model but its scripting language is limited and highly tailored to file contracts.
    *   Investigation is required to determine if Sia's scripting supports the absolute minimum requirements for an HTLC (hash lock + time lock). If it only supports 2-of-2 multisig file contracts, a standard atomic swap cannot be built without trusting a federation.

### 12. Hyperledger (Fabric/Besu)
*   **Feasibility:** N/A (Contextually Mismatched).
*   **Implementation Strategy:**
    *   Hyperledger is a framework for *permissioned* blockchains.
    *   Atomic swaps rely on public, permissionless censorship resistance to ensure the time-lock mechanism cannot be censored by validators.
    *   While you *can* write chaincode (smart contracts) in Fabric to do an HTLC, the counterparty must be a permissioned participant in that specific Hyperledger consortium. This does not fit Fuego's permissionless, decentralized exchange model unless Fuego is specifically bridging to a known, federated enterprise network.

## Next Steps

1.  **Prioritization:** Begin with BTC, LTC, DCR, and Polygon as they represent the lowest hanging fruit given Fuego's existing architecture.
2.  **Architecture Upgrade:** Begin research into adapting Komodo's SPV (Simplified Payment Verification) model for the `SwapDaemon` to alleviate node-hosting requirements for new chains.
3.  **Code Scaffolding:** Create stubs in `src/SwapDaemon/` for `BtcClient`, `LtcClient`, and `PolygonClient`.
4.  **Smart Contracts:** Verify the existing ETH contract works unmodified on Polygon. Draft the P2TR/HTLC scripts for BTC/LTC.
