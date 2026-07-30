# Fuego SwapDaemon: SPV Light Client Architecture

## 1. The Scalability Problem

Currently, Fuego's atomic swaps rely on full nodes to verify the state of counterparty chains (e.g., running a Monero daemon, an Ethereum node, a Solana RPC, etc.). As we expand `swapXFG` to support dozens of chains (BTC, LTC, KMD, DCR, Polygon, Kaspa, etc.), requiring Liquidity Providers (LPs) and the Fuego `SwapDaemon` to run full nodes for every supported chain becomes operationally impossible. It centralizes swap infrastructure to only well-funded institutional actors.

## 2. The Solution: Simplified Payment Verification (SPV)

Following the architectural success of Komodo's AtomicDEX, Fuego must implement SPV (Simplified Payment Verification) capabilities natively within the `SwapDaemon`. 

SPV allows a node to verify that a transaction exists in a block, and that the block is part of the longest PoW chain, without needing to download the entire blockchain history. It achieves this by downloading only the block headers and requesting Merkle proofs for specific transactions.

## 3. SPV Integration in the Fuego State Machine

SPV drastically changes how the `SwapDaemon` validates state transitions, specifically targeting the counterparty lock and claim phases.

### A. Verifying `ADAPTOR_CTR_LOCKED` (Counterparty Lock)
When the counterparty claims they have locked funds in the HTLC/Adaptor contract on their chain (e.g., Bitcoin):
1. **Current:** `SwapDaemon` queries local Bitcoin Core RPC.
2. **SPV Target:** `SwapDaemon` connects to a set of SPV servers (e.g., ElectrumX) or native P2P peers.
3. It requests the block header containing the lock transaction.
4. It requests the Merkle inclusion proof for the transaction ID.
5. It cryptographically verifies the proof against the block header's Merkle Root.
6. It waits for `N` block header confirmations (depth) to ensure finality.
7. Only then does it transition to `ADAPTOR_SECRET_REVEALED`.

### B. Verifying `ADAPTOR_SECRET_REVEALED` (Secret Extraction)
When the counterparty spends the Fuego user's locked funds on the remote chain, they must reveal the secret/preimage:
1. `SwapDaemon` monitors the SPV network for spends of the specific HTLC UTXO.
2. Upon receiving the transaction via SPV, it extracts the `OP_RETURN` or witness data containing the secret.
3. The extracted secret is fed back into Fuego's `src/crypto/musig2.h` engine to unlock the local XFG.

## 4. Architectural Implementation Plan

### Phase 1: UTXO / Bitcoin-Family SPV (Electrum Protocol)
The most efficient path to UTXO SPV is integrating an Electrum client library directly into `SwapDaemon` (C++).
*   **Protocol:** Electrum RPC (JSON-RPC over TCP/TLS).
*   **Privacy:** Implement BIP-157/BIP-158 (Compact Block Filters / Neutrino) to avoid the privacy leaks of traditional BIP-37 Bloom filters. Neutrino allows the `SwapDaemon` to download deterministic filters for blocks and check locally if our swap transactions are involved, rather than telling the server what we are looking for.
*   **Chains Supported:** BTC, LTC, BCH, KMD, DCR.

### Phase 2: EVM Light Clients (Helios / Sync-Committees)
EVM chains don't use UTXO SPV, but modern light clients exist.
*   **Protocol:** Instead of relying on Infura/Alchemy (which requires trust), integrate an Ethereum Light Client like **Helios** (written in Rust, compiled to C-bindings) into `SwapDaemon`.
*   **Mechanism:** Helios verifies Ethereum's sync committee signatures. It provides trustless state proofs for Ethereum smart contract states (verifying the ETH `ADAPTOR_CTR_LOCKED` state without a full Geth node).
*   **Chains Supported:** ETH, Polygon (via state proofs on L1), Arbitrum/Optimism (via L2 state roots).

### Phase 3: P2P Network Overhaul (libp2p)
To reliably find SPV peers and coordinate cross-chain atomic swaps without centralized matchmakers:
*   Integrate **libp2p** into `SwapDaemon`.
*   Use libp2p's DHT (Distributed Hash Table) for peer discovery, allowing Fuego nodes to discover Electrum servers, Ethereum light client peers, and other Fuego swap nodes dynamically.

## 5. Security Model & Trade-offs

1.  **Eclipse Attacks:** An attacker could surround an SPV node with malicious peers that hide the lock transaction or lie about the longest chain. 
    *   **Mitigation:** `SwapDaemon` must strictly connect to multiple, geographically diverse SPV servers and cross-reference block headers.
2.  **Confirmation Depth:** SPV relies heavily on PoW/PoS finality.
    *   **Mitigation:** The `SwapDaemon` configuration must allow dynamic confirmation depths. e.g., 2 confirmations for LTC, but 6 confirmations for BTC before acknowledging `ADAPTOR_CTR_LOCKED`.
3.  **Data Availability:** A light client cannot verify if a block is fully valid (e.g., if coins were printed out of thin air), only that miners built on top of it.
    *   **Mitigation:** This is a known acceptable trade-off for cross-chain swaps. As long as the specific HTLC transaction is valid and included in the longest chain (verified by Merkle proof and depth), the atomic swap can proceed safely.

## 6. Action Items for `SwapDaemon`

1.  Add a new abstraction layer `ISpvClient` alongside the existing full-node RPC clients.
2.  Implement `ElectrumSpvClient` supporting Compact Block Filters (Neutrino).
3.  Modify `SwapStateMachine.cpp` to introduce an `ADAPTOR_WAITING_SPV_CONFIRMATIONS` sub-state to handle asynchronous Merkle proof verification.
4.  Remove the hard requirement for local `bitcoind`/`monerod` binaries in the LP setup documentation.