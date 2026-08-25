# Fuego Deprecated Features Catalog

Toggleable subcategory for brainstorming. These are deprecated, experimental, shelved, or legacy Fuego features that may contain reusable patterns or unsolved problems worth revisiting.

**Activation:** Load this file only when the user enables the deprecated toggle. Do not load by default.

## Deprecated Features

### 1. PI Controller / HEAT Peg Defense (Removed)

**What:** Proportional-Integral controller for HEAT stablecoin peg defense. Used oracle price data to buy/sell HEAT from treasury to maintain target ratio.

**Why removed:** Replaced by hard-peg model at $1.58 USD. Redemption price is now pool-ratio-based.

**Code remnants:** `Core.h:218` (`redemptionRateNum = 0`), `Core.cpp:1416,1531`, `CryptoNoteConfig.h:216` (HEAT_MINT_PREMIUM_BPS = 333)

**Brainstorming value:** The PI controller design (KP=0.08, KI=0.015, ±50%/yr base rate, hill damping M=200%/N=4) is a proven stablecoin feedback mechanism. Could be repurposed for: dynamic AMM fee adjustment, CD yield rate smoothing, or cross-chain price oracle反馈.

### 2. Elderfier / Elderfyre Stayking (Experimental)

**What:** Staking/validator system. Users create stake deposits (min 800 XFG), get 8-char Elderfier ID, register keys to ENindex, participate in Elder Council (consensus requests, voting, Burn2Mint review).

**Why experimental:** Code exists only in TUI variants, not wired into main node. Architecture lists "Staked-node consensus (merkle root signing, quorum)" as shelved.

**Code remnants:** `tui/main.go.bubbletea:145,395-519`, `tui-testnet/main.go.clean`, `fuego-flutter-wallet/README.md:112`

**Brainstorming value:** Validator staking with identity (Elderfier ID) + council governance. Could evolve into: liquid staking, governance tokens, validator selection for ZK proof generation, or DA committee membership.

### 3. C0DL3 Gas Token / L3 Rollup (Shelved)

**What:** L3 rollup with C0DL3 gas token, part of DIGM architecture.

**Why shelved:** Too complex for current priorities.

**Code remnants:** `docs/digm/DIGM_FUEGO_ARCHITECTURE_GUIDE.md:668`

**Brainstorming value:** L3 rollup design for gas abstraction. Could be revisited as: execution layer for DIGM microtransactions, off-chain audio processing with on-chain settlement, or gasless swap UX.

### 4. L2/L3 HEAT/COLD on Arbitrum (Shelved)

**What:** Mint HEAT and COLD tokens on Arbitrum L2 using STARK zero-knowledge proofs of Fuego chain deposits.

**Why shelved:** Listed in DIGM architecture "What We Shelved" section. xfg-stark-cold-starks/ preserved but deprioritized.

**Code remnants:** `docs/digm/DIGM_FUEGO_ARCHITECTURE_GUIDE.md:666,671`

**Brainstorming value:** Cross-chain L2 token minting via ZK proofs. Could be adapted for: multi-chain HEAT liquidity, L2 trading pairs, or privacy-preserving cross-chain bridges.

### 5. Celestia DA for Audio Blob Storage (Shelved)

**What:** Use Celestia as a data availability layer for storing DIGM audio blobs off-chain.

**Why shelved:** Part of shelved DIGM architecture.

**Code remnants:** `docs/digm/DIGM_FUEGO_ARCHITECTURE_GUIDE.md:669`

**Brainstorming value:** DA layer for large binary data. Could be replaced with: Fuego-native chunk-store with erasure coding, IPFS/Filecoin integration, or custom Fuego blob DA with on-chain commitments.

### 6. EVM Composability — NFTs, DeFi on Music (Shelved)

**What:** Enable NFT minting and DeFi primitives on the DIGM music platform via EVM integration.

**Why shelved:** Part of shelved DIGM architecture.

**Code remnants:** `docs/digm/DIGM_FUEGO_ARCHITECTURE_GUIDE.md:670`

**Brainstorming value:** Music NFTs, fractionalized royalties, streaming payment channels. Could be built on existing Fuego primitives (commitment outputs, deposit terms) without EVM.

### 7. DIGM-origins `.digm` Audio Container + HW-Secure-Enclave Proof-of-Recording (Shelved)

**What:** Custom audio container format (`.digm`) using hardware Secure Enclaves to prove original recording authenticity.

**Why shelved:** Format shelved, but economic decisions (PARA economics, OGG-Opus, FLAC, curator share, chunk size) inherited by current system.

**Code remnants:** `docs/digm/DIGM_FUEGO_ARCHITECTURE_GUIDE.md:672`

**Brainstorming value:** Hardware-backed proof-of-authenticity for digital media. Could be adapted to: TEE-based oracle attestations, secure enclave key management for validators, or hardware-backed identity for Elderfier system.

### 8. Old STARK Burns / MVP Commitments (Deprecated)

**What:** DepositCommitmentGenerator using `keccak(secret || "commitment")` without amount/network/term binding. Original HEAT burn cross-chain proof system.

**Why deprecated:** v3 commitment scheme (StarkCommitmentGenerator) binds amount, network_id, chain_id, version — far more robust. Old scheme was a security risk.

**Code remnants:** `DepositCommitment.h:80-127` (marked `[[deprecated]]`), `DepositCommitment.cpp:130-245`, `docs/features/(derecated)old-stark-burns/`

**Brainstorming value:** Simpler commitment scheme for low-security use cases (e.g., ephemeral identities, rate limiting, or spam prevention where full binding isn't needed).

### 9. 3-Term Adaptor Signature combineSpendKeys (Deprecated)

**What:** 3-term `a+b+adaptor` shared spend key for XMR atomic swaps.

**Why deprecated:** Wrong XMR key model. XMR-XFG swap uses 2-term `a+b` (adaptor secret IS one party's spend share). 3-term produced an address no one funded.

**Code remnants:** `Monero/AdaptorSignature.h:101-114` (marked `[[deprecated]]`), `tests/test_xmr_keys.cpp:79-85`

**Brainstorming value:** The 3-term model is wrong for XMR but could be correct for other chains with different key derivation. Worth revisiting if new counterparty chains use additive key combining.

### 10. Rollover Vault (Deprecated, Serialization-Only)

**What:** Tracked accumulated rollover fees and vault balance as part of CD yield pipeline. 10% swap fee share went to rollover vault.

**Why deprecated:** Replaced by direct fee distribution. Fields kept only for serialization compatibility.

**Code remnants:** `Blockchain.h:455,460`, `Blockchain.cpp:228,232`

**Brainstorming value:** Rollover fee compounding could be reintroduced as: auto-compounding CDs, fee reinvestment strategies, or yield optimization vaults.

### 11. voidAlias (Deprecated, No Auth)

**What:** Allow any caller to void/delete an alias from the alias registry.

**Why deprecated:** No authentication model — any caller could void any alias.

**Code remnants:** `AliasIndex.cpp:131-136`

**Brainstorming value:** Signed revocation path (owner's spend key) is planned but not designed. Could extend to: alias marketplace, time-locked aliases, or alias-based identity recovery.

### 12. Meshtastic Integration (Experimental Stub)

**What:** Fuego P2P communication over Meshtastic LoRa mesh radios — node connectivity without internet.

**Why experimental:** All methods are stubs (return true/false, no-ops).

**Code remnants:** `P2p/Meshtastic/MeshtasticIntegration.h`, `MeshtasticIntegration.cpp`

**Brainstorming value:** Off-grid blockchain networking. Could enable: disaster-recovery nodes, rural area participation, or censorship-resistant block propagation.

### 13. Legacy Bond / COLD Migration (Active but Legacy)

**What:** Pre-v3 bond deposits (from bug-era) migration path. Legacy bonds use separate 50% CD share track.

**Why legacy:** Handles historical deposits from before v3 commitment upgrade. Necessary for chain compatibility.

**Code remnants:** `TransactionExtra.h:68-71`, `Blockchain.h:375,440-441`, `Blockchain.cpp:2926-3271`

**Brainstorming value:** Debt-capped legacy instruments. Pattern could be used for: time-limited promotional rates, grandfathered terms for early adopters, or migration incentives.

### 14. V1 Swap Protocol (Legacy)

**What:** Original atomic swap state machine (MakerSwap/TakerSwap), message-driven with timeout-based transitions.

**Why legacy:** Superseded by V2 (TPU Protocol) with deterministic phases.

**Code remnants:** Design preserved in protocol documentation

**Brainstorming value:** V1's message-driven approach had simpler failure modes. Could be useful for: lightweight swap implementations on resource-constrained devices, or as fallback when V2 persistence layer fails.

### 15. ZK Rollup Chain Integrations (Planned, Not Implemented)

**What:** Cross-chain bridge integrations with ZK rollup L2s (Manta Pacific, Aztec, zkSync, Linea, Scroll).

**Why not implemented:** Strategic planning documents, not code.

**Code remnants:** `AFKswapXFG/SWAPXFG_CHAIN_REACH_STRATEGY.md:50-51`, `plans/swap_expansion_guide.md:76`

**Brainstorming value:** Privacy-focused L2 integrations. Could enable: private HEAT trading, ZK-verified cross-chain identity, or rollup-based scaling for DIGM microtransactions.

### 16. BPDF/STARK Wallet Methods (Removed)

**What:** Wallet-level helpers for Burn Proof Data Format proofs.

**Why removed:** Security fixes (C3, C8).

**Code remnants:** `WalletLegacy.h:186`, `WalletLegacy.cpp:1960`

**Brainstorming value:** The BPDF format itself (proof data serialization) could be standardized for: cross-chain proof interoperability, portable ZK credentials, or audit trail formatting.

### 17. Tendermint process_history_loop (Deprecated)

**What:** Transaction history polling for Tendermint/Cosmos coins.

**Why deprecated:** Replaced by tx_history_v2.

**Code remnants:** `tendermint_token.rs:591`, `tendermint_coin.rs:3513`

**Brainstorming value:** Polling vs push tradeoffs. Could inform: event-driven wallet updates, WebSocket subscription patterns, or push notification architecture for swap state changes.

### 18. handle_get_objects / handle_incoming_tx (Deprecated)

**What:** CryptoNote P2P protocol handlers for serving blockchain objects and processing incoming transactions.

**Why deprecated:** Legacy CryptoNote protocol handlers marked for cleanup.

**Code remnants:** `ICore.h:78,95`, `Core.h:56,83`, `Core.cpp:261,948`, `Blockchain.h:178`, `Blockchain.cpp:1718`

**Brainstorming value:** Direct P2P object serving. Could be adapted for: peer-to-peer chunk distribution (DIGM audio), direct block relay optimization, or mobile node lightweight sync.

### 19. AMM Pool legacyFees (Deprecated Field)

**What:** Accumulated legacy LP fees in AMM pool serialization.

**Why deprecated:** Replaced by unified fee pool system.

**Code remnants:** `AmmPool.cpp:131-132`

**Brainstorming value:** Historical fee tracking pattern. Could be useful for: fee analytics dashboards, LP performance reporting, or backtesting AMM fee strategies.

### 20. Legacy V1 Wallet Format

**What:** Original wallet serialization format (WalletSerializationV1).

**Why legacy:** Current format uses V2 with keccak integrity + ChaCha8 encryption.

**Code remnants:** `WalletSerializationV1.cpp`, `LegacyKeysImporter.cpp/h`

**Brainstorming value:** V1's simplicity could inform: lightweight wallet formats for constrained devices, watch-only wallet optimizations, or account abstraction patterns.

### 21. EUR/CPI Display Mode (Removed)

**What:** Display HEAT prices in Euros using CPI conversion.

**Why removed:** HEAT hard-pegged at $1.58 USD.

**Code remnants:** `Currency.cpp:889-894`

**Brainstorming value:** Multi-currency display. Could be extended to: real-time FX feeds, purchasing power parity visualization, or regional pricing for DIGM content.

### 22. Flutter Wallet Fee Default (Deprecated)

**What:** Hardcoded default fee of 0.1 XFG.

**Why deprecated:** Should use dynamic fee estimation.

**Code remnants:** `fuego_wallet_adapter.dart:255`

**Brainstorming value:** Fee estimation patterns. Could inform: dynamic fee UI, fee bumping UX, or priority fee tiers.

### 23. voidAlias Authentication Gap

**What:** No signed revocation path for aliases.

**Status:** Planned but not designed.

**Brainstorming value:** Alias lifecycle management. Could enable: domain-name-like alias marketplace, identity recovery via trusted parties, or time-locked alias delegation.
