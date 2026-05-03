# 📘 Dev Guide: AFK Adaptor Swaps Implementation
 
 ## 1. Executive Summary
 **Goal**: Implement a non-interactive "Post-and-Forget" swap mechanism.
 **Mechanism**: Replace interactive MuSig2 joint-keys with **Single-Party Twisted Signatures** and **Individual ETH HTLC Contracts**.
 **User Experience**: 
 1. Maker locks XFG $\rightarrow$ Posts Offer (including $P_b$, $S$, and $h$).
 2. Taker accepts $\rightarrow$ Deploys unique ETH HTLC contract locked to Bob's $P_b$.
 3. Maker claims ETH $\rightarrow$ Taker claims XFG.
 
 ---
 
 ## 2. Detailed Development Phases
 
 ### Phase 1: Cryptographic Primitives (`src/crypto`)
 *Objective: Implement the "Twisting" and "Untwisting" logic for non-interactive locks.*
 
 - [x] **Define AFK Data Structures**:
     - Create `struct AFKLockData` containing the secret $s$, public point $S$, and the twisted pre-signature $\sigma_{pre}$.
 - [x] **Implement `generate_afk_lock_data`**:
     - Generate random scalar $s$.
     - Compute public point $S = s \cdot G$ and hash $h = \text{SHA256}(s)$.
     - Generate $\sigma_{pre}$ (twisted signature) for the XFG unlock transaction.
     - Returns $\{s, S, h, \sigma_{pre}\}$.
 - [x] **Implement `complete_afk_signature`**:
     - Logic: $\sigma_{final} = \sigma_{pre} + s$.
     - Ensure the resulting signature is a valid Fuego Schnorr signature.
 - [x] **Implement `extract_afk_secret`**:
     - Logic: $s = \sigma_{final} - \sigma_{pre}$.
     - Validate that the extracted secret matches the expected adaptor point $S$.
 - [x] **Unit Testing**: Create a test suite in `src/crypto/adaptor_test.cpp` to verify the round-trip: `Lock $\rightarrow$ Complete $\rightarrow$ Extract`.
 
 ### Phase 2: Wallet Backend (`src/Wallet`)
 *Objective: Enable the wallet to perform on-chain locks and securely store the adaptor secrets.*
 
 - [x] **Update Wallet Storage**:
     - Add a mapping in the wallet file to store `LockID $\rightarrow$ AdaptorSecret (s)`.
     - Ensure secrets are encrypted using the wallet password.
 - [x] **Implement RPC `create_afk_lock`**:
     - Call `generate_afk_lock_data()`.
     - Construct a transaction that locks XFG into a script requiring the completed $\sigma_{final}$.
     - Broadcast transaction and return `LockID`, $S$, $h$, and $\sigma_{pre}$.
 - [ ] **Implement RPC `claim_afk_swap`**:
     - Retrieve secret $s$ from storage.
     - Construct and sign the claim transaction for the target chain (ETH/SOL).
 - [ ] **Implement `refund_afk_lock`**:
     - Create a command to reclaim XFG after the lock timeout (max 200h) has passed.
 
 ### Phase 3: Daemon Orchestration (`src/SwapDaemon`)
 *Objective: Transform the daemon into a validator that monitors both chains to automate the state machine.*
 
 - [x] **Update State Machine (`SwapTypes.h`)**:
     - Add `AFK_OFFER_LOCKED`: Funds are locked on-chain; offer is public.
     - Add `AFK_OFFER_ACCEPTED`: Taker has locked their side; Maker is notified to claim.
 - [ ] **Implement `/submit_afk_offer`**:
     - Validate that the `LockID` exists on the Fuego blockchain.
     - Verify the locked amount matches the offer amount.
     - Add the offer to the public orderbook.
 - [ ] **Implement `/accept_afk_offer`**:
     - **MRT Guard**: Reject acceptance if Maker's lock has $< 1$ hour remaining.
     - **Target Chain Validation**: Verify a new ETH HTLC contract has been deployed.
     - **Identity Match**: Verify the contract is locked to the Maker's public key $P_b$.
     - **Hash Match**: Verify that the target lock uses the hash $h$ provided in the offer.
     - **Timeout Check**: Ensure the Taker's lock timeout is shorter than the Maker's lock.
     - Transition state to `AFK_OFFER_ACCEPTED`.
 - [ ] **Implement Orderbook Management**:
     - Add `/cancel_afk_offer` to remove the offer from the public view (while the lock remains on-chain).
     - **Dynamic Filtering**: Remove offers from orderbook when $< 1$ hour remains.
 
 ### Phase 4: TUI Frontend (`swapxfg`)
 *Objective: Create a guided, intuitive interface for the AFK flow.*
 
 - [x] **Update Command Parser**:
     - Implement `/offer <amount_xfg> <amount_target> <pair> <timeout_hrs>`.
     - Implement `/confirm-offer <amount_xfg> <amount_target> <pair> <timeout_hrs>`.
 - [ ] **Implement AFK Workflow**:
     - `/offer` $\rightarrow$ Pre-flight check and a hint about locking.
     - `/confirm-offer` $\rightarrow$ triggers `create_afk_lock` $\rightarrow$ displays "Funds Locked" $\rightarrow$ submits offer to daemon.
 - [ ] **Implement Taker "Accept" Flow**:
     - `/accept <id>` $\rightarrow$ triggers ETH contract deployment via bridge/wallet $\rightarrow$ verifies lock with daemon.
 - [ ] **Build "My Offers" View**:
     - Create a new tab/view to monitor active pre-locks.
     - Implement real-time status updates: `LOCKED` $\rightarrow$ `ACCEPTED` $\rightarrow$ `CLAIMED`.
 
 ### Phase 5: End-to-End Validation
 *Objective: Stress test the system with real-world scenarios.*
 
 - [ ] **Scenario A (Happy Path)**: Maker locks $\rightarrow$ Posts $\rightarrow$ Taker deploys ETH contract $\rightarrow$ Maker claims ETH $\rightarrow$ Taker claims XFG.
 - [ ] **Scenario B (Griefing)**: Taker deploys ETH contract with fake hash $\rightarrow$ Verify daemon rejects the lock $\rightarrow$ Verify Maker can still refund after timeout.
 - [ ] **Scenario C (AFK Timeout)**: Maker locks funds $\rightarrow$ No one accepts $\rightarrow$ Maker refunds funds after 200 hours.
 - [ ] **Scenario D (Privacy Check)**: Verify that Bob's main ETH address is never exposed in the orderbook.
 
 ---
 
 ## 🛡️ Safety & Security Matrix
 
 | Risk | Mitigation | Verification |
| :--- | :--- | :--- |
| **Secret Leak** | Adaptor signatures wrap $s$ in a signature | Check that $s$ is not visible in `get_tx_info` |
| **Fake Target Lock** | Daemon verifies $H(S)$ match on-chain | Test with incorrect hash lock |
| **Funds Trapped** | Hard timeout limit (max 200h) | Test refund after timeout |
| **Double Spend** | Lock is a standard UTXO commit | Verify via `fire_wallet` balance |
| **Symmetry Attack** | MRT Guard (1h) and 2:1 Time Ratio | Verify accept fails at T-59 mins |
| **Identity Leak** | Contract-per-swap using $P_b$ | Verify no Maker address in orderbook |
 
 ---
 
 ## ⏱️ Deliverables Checklist
 - [x] `src/crypto/adaptor.h/cpp` $\rightarrow$ AFK Primitives
 - [x] `src/Wallet/WalletRpcServer.cpp` $\rightarrow$ `create_afk_lock`
 - [x] `src/SwapDaemon/SwapStateMachine.cpp` $\rightarrow$ AFK State transitions
 - [ ] `src/Rpc/RpcServer.cpp` $\rightarrow$ `/submit_afk_offer` & `/accept_afk_offer`
 - [x] `swapxfg/app/tui.go` $\rightarrow$ New AFK command set & "My Offers" view
 - [ ] `tests/afk_swap_test.sh` $\rightarrow$ Integration test script

