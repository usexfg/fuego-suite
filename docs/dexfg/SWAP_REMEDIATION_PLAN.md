# DeXFG / SwapXFG Remediation Plan

> Version 1.2 — 2026-11-07 — Status: IN IMPLEMENTATION (1.1 adds F22-F29 and tasks 1.0, 1.10; 1.2 adds F33 and R12)
> Scope: the defects found in the 2026-11-06/07 audit of `src/SwapDaemon/`, the swap relay,
> CI, and Fuego Valise's swap layer, plus implementation recommendations.
> Every phase that touches source needs a `CHANGELOG.agent.md` task list and sign-off.

## 1. Findings this plan fixes

| # | Finding | Evidence | Effect |
|---|---|---|---|
| F1 | GCC rejects `bool CryptoNote::isHeatwaveTag` / `carriesHeatwaveTag` defined inside `namespace CryptoNote` | `src/CryptoNoteCore/Blockchain.cpp:150,173`; Build check fails on Ubuntu, Sanitizers and Windows, on this branch and on `claude/usexfg-merge-redo` | No Linux or Windows build |
| F2 | Clang on Linux rejects an initializer list (`long long` vs `long`) | `src/SwapDaemon/tests/test_price_policy.cpp:497` | Price policy tests do not build |
| F3 | CI builds but never runs tests | `check.yml` has no `ctest`; tests are registered at `src/CMakeLists.txt:1129` | Test regressions merge silently |
| F4 | Every UTXO client hard-codes `isReadyForNewSwap()` to `false`; the relay excludes ids 2,3,6,8,9,10,20,21,22 | `BtcChainClient.h:21` and siblings; `SwapOfferRelay::isExecutablePair` | No UTXO or XMR swap can start |
| F5 | Full-node mode cannot verify the counterparty lock: `verifyLock` calls `listunspent`, which only sees wallet-watched addresses, and Bob's path never imports the address | `BtcRpcClient.cpp:512`, same in LTC, BCH, DOGE, DASH, ZEC | Verification always fails (fail-closed) |
| F6 | `importaddress` is the only import path, and descriptor wallets (Bitcoin Core default since 23.0) reject it | `BtcRpcClient.cpp:432` | Watch-only setup breaks on current nodes |
| F7 | Full-node mode cannot discover a claim; the secret is recovered only from a claim txid the counterparty sends in `SECRET_REVEAL` | `tryExtractClaimedSecret` in all UTXO clients; `SwapDaemon.cpp:4742` | A modified Bob can claim, withhold the txid, then refund XFG after `xfgTimeoutHeight`; Alice loses her coin |
| F8 | SPV cannot lock (no coin selection or funding) | `BtcChainClient.cpp:104` | Alice's role is impossible over SPV |
| F9 | SPV headers are checked only against their own `bits`; no retarget rules or `powLimit` | `SpvHeaderStore.cpp` `addHeader` → `SpvHeader::meetsPoW` | A server can serve a low-difficulty fake chain |
| F10 | SPV uses double-SHA256 and 80-byte headers for every chain | `SpvHeader.cpp:87`; `ElectrumSpvClient.cpp:140`; `NeutrinoSpvClient.cpp:391` | Wrong for LTC (scrypt), KMD (Equihash, ~1487-byte headers), DCR (BLAKE-256, 180-byte headers) |
| F11 | Electrum TLS never verifies certificates | `ElectrumConnection.cpp:38` `SSL_VERIFY_NONE` | Anyone on the path can impersonate a server; combined with F9 a lock can be forged |
| F12 | Shipped defaults use one Electrum server per chain, so the multi-server majority cross-check is off | `*_spv_min_servers: 1`; wallet `_defaultServers`; `ElectrumSpvClient.cpp:150` | F9 + F11 need a single attacker |
| F13 | UTXO clients hard-code mainnet address prefixes | `BtcChainClient.cpp` (`"bc"`), Litecoin (`"ltc"`) | No regtest or testnet harness possible |
| F14 | EVM registry check accepts any contract whose `getContract` decodes | `EthRpcClient.cpp:455` | A misconfigured or malicious registry passes readiness |
| F15 | Flat-config ACTIVE EVM chains reuse ETH's registry address | `docs/dexfg/swap-security-residual-notes.md`; `SwapDaemon.cpp` `applyHtlcConfig(..., chainCfg.ethHtlcRegistry, ...)` | Readiness fails unless the same address is deployed on each chain |
| F16 | The example config's flat EVM keys are never read (`eth_host`, `eth_wif`, `rh_host`) | `swap_config.example.json` vs `ChainClientConfig.cpp` | Operators following it configure nothing |
| F17 | PEAQ and GLEEC have no verified price feed id | `AssetCatalog.cpp` | New swaps refused for both |
| F18 | Tempo has no native coin; `HashedTimelock.lock()` escrows native value only | `HashedTimelock.sol` | Tempo cannot be a pair |
| F19 | Valise pins suite commit `c787f7f`, which neither remote serves | `git submodule update` fails with "not our ref" | Recursive checkouts and desktop CI cannot fetch the daemon source |
| F20 | `SwapOfferRelay::getSeedRate` returns 0 above id 6 and labels cases in a different id order | `SwapOfferRelay.cpp:439` | Wrong `getswapprice` display |
| F21 | Docs CI fails on 51 broken links in 5 files | Docs check, Mintlify `broken-links` | Docs CI is permanently red |
| F22 | No peer message carries Alice's lock reference (txid or contract id, output index, lock script) to Bob outside the AFK flow | `PeerMessageType` has no lock notice; Bob's `verifyLock` runs with an empty `ctrLockTxId` | No standard-flow swap of any family passes Bob's lock check |
| F23 | Bob's UTXO claim pubkey is never sent; `SwapParams::ctrPubKey` is never set by the protocol | grep: only read, never assigned | Alice's UTXO lock fails unless an operator supplies the key |
| F24 | UTXO refunds pay `params.ctrAddress`, which both roles set to Bob's receive address | `BtcRpcClient::refundHtlc(..., params.ctrAddress, ...)`; same in LTC, BCH | Alice's refund pays Bob |
| F25 | `sendtoaddress` amounts are formatted with `std::to_string(double)`, which keeps 6 decimals | `BtcRpcClient::lockHtlc`, `sendToAddress` | The lock differs from `ctrAmount` by up to 50 base units; short locks fail verification |
| F26 | Claim and refund spend output 0, but `sendtoaddress` places the HTLC output at a random index | `BtcChainClient::claim`/`refund` pass vout `0` | About half of claims and refunds reference the change output |
| F27 | P2WSH claim and refund witnesses push `0x51` and `0x00` as the `OP_IF` selector; witness v0 policy (MINIMALIF) requires `0x01` and empty | `BtcHtlcScript::createClaimWitness`, `createRefundWitness` | Bitcoin Core rejects both transactions as non-standard |
| F28 | The claim signs with `nSequence = 0xFFFFFFFD`, but the raw-tx builder writes `0xFFFFFFFE` | `BtcRpcClient::claim` vs `BtcHtlcScript::buildRawSegWitTx` | Every BTC claim signature is invalid (BIP143 commits to nSequence) |
| F29 | v11 Hearth CD share is floored per taker fill, so small fills pay CD holders 0 (`test_orderbook_auction` fails) | `OrderbookAuction.cpp` fee split | Consensus code outside the swap path; needs an owner decision before any change |
| F30 | Release builds pass `-DNDEBUG`, so the ~600 `assert()` checks in 21 test files compile to nothing; CTest reported them as passing | `CMakeLists.txt` `CMAKE_CXX_FLAGS_RELEASE`; `#include <cassert>` tests | Three tests had been failing unseen (`test_btc_htlc`, `test_ltc_htlc` fixtures; one BCH message check) |
| F31 | Electrum connections never use TLS: `connectToServers` calls `connect(host, port)` with TLS defaulted off, including for the shipped `:50002` TLS ports | `ElectrumSpvClient::connectToServers` | Every SPV query is plaintext and unauthenticated |
| F32 | The AFK flow (taker locks from a maker's fill result) carries the maker's receive address but no claim key | `SwapRequestResult` has `ctrAddress` only | UTXO pairs cannot run through AFK; they are interactive-only until the relay carries the key |
| F33 | Alice locks the counterparty coin to the hash lock Bob sends in `ADAPTOR_EXCHANGE`, but nothing proves that hash is H(t) for the t behind the adaptor point T. The DLEQ proves only log_G(T) = log_E(Q); Alice's pre-lock gate checks that the hash is non-zero; the match of the preimage to T is checked only after Bob has claimed | `SwapDaemon.cpp`: `ADAPTOR_EXCHANGE` sets `params.hashLock` from the message; the CTR lock gate; the post-claim check in `handleCtrLocked` | A modified Bob publishes hash(p) for a p of his choosing, claims Alice's coin with p, and refunds his XFG after `xfgTimeoutHeight`. Alice's check rejects p, after the loss. It applies to every hash-locked pair: SOL, the ACTIVE and ADAPTER EVM chains, and the GATED UTXO chains |

## 2. Roles on a UTXO chain (ground truth from code)

- **Alice** holds the UTXO coin and wants XFG. She **locks** on the UTXO chain, must **discover Bob's claim**
  to learn the adaptor secret, and **refunds** after the UTXO timeout.
- **Bob** holds XFG and wants the UTXO coin. He funds the XFG escrow, **verifies Alice's lock**, then
  **claims** on the UTXO chain. He can refund XFG unilaterally after `xfgTimeoutHeight`.

A transport is safe for Alice only with lock + independent claim discovery + refund, and for Bob only
with independent lock verification + claim.

## 3. Phases

### Phase 0 — Make build and CI truthful (prerequisite for all phases)

| Task | Change | Gate |
|---|---|---|
| 0.1 | Drop the redundant `CryptoNote::` qualifier at `Blockchain.cpp:150,173` (no semantic change) | Full build on GCC 13, Clang 18, MSVC |
| 0.2 | Fix the initializer-list element types at `test_price_policy.cpp:497` | `test_price_policy` builds and passes on GCC and Clang |
| 0.3 | Add `ctest --output-on-failure` after the Linux build in `check.yml`; mark manual-only targets (`test_sol_e2e`) with a CTest label and exclude it | CI red on any failing registered test |
| 0.4 | Add a Clang job beside GCC on Linux | Both compilers green |
| 0.5 | Repair or exclude the 51 broken links (source-path links do not resolve under Mintlify; convert them to code spans) | Docs check green |
| 0.6 | Valise: repin `fuego-suite` to a commit that exists on `usexfg/fuego-suite` and contains `SwapPairCatalog.h` ids 29-45 | `git submodule update --init` succeeds; `fuegod-wire-check` green |

### Phase 1 — Full-node UTXO path for BTC, LTC, BCH (first executable UTXO pairs)

Full node first: locking already works there, and verification and discovery both reduce to
standard node RPCs that need neither a wallet watch nor `-txindex`.

| Task | Change | Detail |
|---|---|---|
| 1.0 | Lock notice and claim key on the wire (F22, F23) | Bob sends his UTXO claim pubkey in `ADAPTOR_EXCHANGE`. Alice sends a signed `CTR_LOCKED` notice (lock reference, output index, lock script) after funding, for every family. Bob binds the first value and rejects a conflicting one. |
| 1.1 | Carry the exact lock outpoint | Alice's `lock()` returns `txid:vout` in `chainState`; the wire and `SwapParams` keep it. No more `vout` 0..3 guessing. |
| 1.2 | Wallet-independent lock verification (F5) | Replace `listUnspent` with `gettxout <txid> <vout>`. Derive the expected `scriptPubKey` from the redeem script (P2WSH) or tweaked key (P2TR); require exact script match, `value >= ctrAmount`, and `confirmations >= requiredConfirmations`. |
| 1.3 | Remove `importaddress` (F6) | Not needed once 1.2 and 1.4 land. Alice's funding stays on the node wallet (`sendtoaddress`, descriptor-wallet compatible). |
| 1.4 | Independent claim discovery (F7) | Record the lock block height. Scan `getblockhash`/`getblock <hash> 2` from that height to `min(tip, ctrTimeoutBlock + margin)` for an input spending the lock outpoint. Extract the secret from that block's tx hex with the existing `extractSecret` logic. Persist the scan cursor and each scanned block hash in `SwapDatabase`; on a hash mismatch, rewind (reorg). Act only after `requiredConfirmations`. `gettxspendingprevout` (Bitcoin Core 24+, mempool only) is an optional early signal where the node supports it. |
| 1.5 | `hasConfirmedClaim` from chain data | Implement from 1.4's result, so Alice never depends on `SECRET_REVEAL`. A P2P-supplied txid becomes a hint that is re-verified on chain. |
| 1.6 | Network parameters (F13) | Replace hard-coded HRPs and address versions with per-network params (mainnet, testnet, regtest). |
| 1.7 | Readiness from capability (F4) | `isReadyForNewSwap()` returns true only in full-node mode with the RPC reachable, `initialblockdownload == false`, a loaded wallet for Alice's role, and the chain id/genesis matching the configured network. SPV mode stays not-ready until Phase 2. |
| 1.8 | Relay executability | Remove ids 3, 9, 10 from `isExecutablePair` only after 1.9 passes (see R1 for removing the hand-numbered list). |
| 1.10 | Spendable claim and refund transactions (F24-F28) | Exact 8-decimal amounts; the lock output index read from the wallet transaction; `0x01`/empty `OP_IF` selectors; the signed `nSequence` written into the transaction; refunds pay the refunder's own key. Sighash and witness bytes checked against vectors produced by an independent implementation (python-bitcoinlib). |
| 1.9 | Regtest end-to-end gate | Two `xfg-swapd` + fuegod testnet + `bitcoind -regtest` (then LTC, BCH). Scenarios: happy path; Bob withholds the claim txid and Alice still claims XFG; fake lock (wrong script, short amount, unconfirmed) rejected; claim block reorged out; daemon restart mid-scan; Alice timeout refund; Bob timeout refund. Runs in CI. |

Rollout: BTC, then LTC and BCH (same client family), then KMD, DOGE, DASH, ZEC full-node clients
(same RPC pattern). Lift the "testnet counterparty support is EVM-only" gate in
`getChainReadiness` per chain once its regtest and testnet runs pass.

### Phase 2 — SPV (optional; only after Phase 1)

| Task | Change |
|---|---|
| 2.1 | TLS: `SSL_VERIFY_PEER` with the system CA store, plus optional per-server certificate pinning for self-signed Electrum servers (F11) |
| 2.2 | Per-chain header rules (F9, F10): a chain-params table with header size, PoW hash, `powLimit` and retarget algorithm. BTC: double-SHA256, 2016-block retarget. BCH: double-SHA256, ASERT (aserti3-2d). LTC: scrypt, 2016-block retarget. |
| 2.3 | KMD and DCR: drop SPV; full node only, unless dedicated Equihash and BLAKE-256 header validators are written and tested |
| 2.4 | Defaults (F12): new swaps over SPV require at least 3 servers from distinct operators; a single-server SPV client reports not-ready |
| 2.5 | Bob-only SPV first: verification + claim over SPV, lock stays full-node. Alice-side SPV (coin selection over Electrum, local signing, fee estimation, change) is a separate task. |
| 2.6 | Tests: fake low-difficulty chain rejected; genuine LTC header vectors accepted under scrypt; certificate mismatch rejected; one dishonest server outvoted by two honest ones |

### Phase 3 — Pricing and catalog gaps

| Task | Change |
|---|---|
| 3.1 | PEAQ feed id (F17): `coingecko:peaq` and `coingecko:peaq-network` fail; `coingecko:peaq-2` is the untested candidate. Verify against DeFiLlama and CoinGecko, add it, and flip `test_price_policy`'s PEAQ assertion. |
| 3.2 | GLEEC feed id (F17): same procedure |
| 3.3 | Tempo (F18): separate plan — token-escrow HTLC contract (approve + lock of a TIP-20 token), a token lock/claim/refund path in `EthRpcClient`, a chosen settlement token with a verified feed, catalog id 46 |
| 3.4 | Seed rates (F20): delete `SwapOfferRelay::getSeedRate` or derive it from the catalog and live feeds |

## 4. Implementation recommendations

| # | Recommendation | Why |
|---|---|---|
| R1 | Put executability in the catalog: a per-pair capability (or per transport) that `isExecutablePair`, readiness and the wallet all read | The relay's hand-numbered id list is the last mirror of the catalog in the suite |
| R2 | Generate Valise's `SwapPairSdk`, `ChainTypeSdk` ids and ticker map from the catalog (a `xfg-swapd --dump-catalog` JSON consumed by `tool/gen_chains.dart`); make `SwapPairSdk.fromId` fail closed instead of returning ETH | The wallet still mirrors the catalog by hand in three places, held together only by tests |
| R3 | Pin the registry's runtime bytecode hash in `hasDeployedHtlcRegistry` (F14) | An ABI probe proves shape, not code |
| R4 | Deploy `HashedTimelock` and `PointTimelock` through one deterministic CREATE2 deployer so the address is identical on every EVM chain | One address to pin and audit; the current ETH-registry default (F15) becomes correct instead of a readiness failure |
| R5 | Retire the flat per-chain EVM fields; `evm_chains` becomes the only EVM config path, and the example config's flat EVM section goes (F16) | One parser path, no dead keys |
| R6 | Wire `PythFeed` into the price worker as the corroborating source | Both current feeds key off the same CoinGecko ids; Pyth metadata already exists in `AssetCatalog.cpp` |
| R7 | Store chain private keys and the XFG swap key in the OS keychain (Valise) or an encrypted keystore (daemon) instead of plaintext `swap_config.json` | The file is owner-only (0600) but still plaintext at rest |
| R8 | Valise: show xfg-swapd's live `readinessError` in chain info instead of static text | `ChainInfo.info['BTC']` says "No setup needed" for a chain that cannot start a swap |
| R9 | One end-to-end harness for every family: regtest UTXO nodes, `anvil` for EVM, `solana-test-validator`, two daemons; a scenario matrix per pair in CI | No pair has recorded two-daemon evidence today; the smoke scripts start no counterparty chain |
| R10 | Generate `docs/dexfg/DEXFG_SWAP_CHAINS.md`'s table from `SwapPairCatalog.h` with a script checked in CI | The table already drifted once in the wallet docs |
| R11 | Delete the orphan `src/SwapDaemon/CLV/` adapter or give it a catalog id | Dead code in the swap path |
| R12 | Bind every counterparty lock to T (F33). (a) Recommended: a cross-group DLEQ proof (ed25519 to secp256k1, bit-decomposition construction, as in COMIT's xmr-btc-swap) plus point locks: `PointTimelock` on EVM, Taproot adaptor signatures on BTC and LTC. This is also the proof AUDIT 3.9 lacks, so it unblocks pure PTLC. (b) A zero-knowledge proof that hash(t) = H and t·G = T on ed25519, keeping the hash contracts; the proof covers SHA-256 or keccak plus non-native ed25519 arithmetic. (c) EVM only: a timelock whose claim checks p·G = T on ed25519 on chain; gas cost to be measured. Until one ships, hash-locked pairs are atomic only against an unmodified XFG-side daemon | F33 lets the XFG side take the counterparty coin at no cost |

## 5. Implementation status (2026-11-07)

| Item | Status |
|---|---|
| 0.1-0.5 | Done. CI: GCC, Clang, MSVC (Windows) and macOS builds pass, sanitizers pass; `ctest` fails only on F29 (`test_orderbook_auction`) |
| 0.6 | Done in Valise (pin `a2df2f3`) |
| 1.0-1.7, 1.10 | Done (`src/SwapDaemon/Utxo/`); signatures verified against python-bitcoinlib digests |
| 1.8, 1.9 | 1.9 (transport): BTC and LTC pass in CI against bitcoind 28.1 and litecoind 0.21.4. BCH passes 28/28 against BCHN 29.2.0 (CI run 42408891349; 28.x expired at the May 2026 upgrade); its first run found that BCHN reports CashAddr, so the duplicate-lock guard never matched (fixed: outputs compared by script). Claims are refused inside the claim runway (review fix). 1.8 open: BTC, LTC, BCH stay GATED until the owner accepts a transport-only gate; no two-daemon XFG run exists; F33 applies to them as well |
| 2.1 | Done (verified TLS, pinning, loopback-only plaintext; F31) |
| 2.2-2.6 | Done for Bob (BTC, LTC, BCH mainnet and regtest): header rules, quorum policy, Bob-only SPV path, role-aware readiness. KMD/DCR SPV refused (2.3); Valise stops writing it. Not run against public Electrum servers. Alice over SPV not implemented |
| 3.1, 3.2 | 3.1 done: PEAQ priced from `coingecko:peaq-2` (identity checked live in CI). 3.2: no feed exists for GLEEC (`gleec-coin` resolves nowhere); GLEEC stays UNPRICED |
| 3.3 | Plan written (`TEMPO_PLAN.md`); not implemented. Steps 0-2 (chain facts, issuer token, feed) decide whether the pair is worth building |
| 3.4 | Done (relay `getSeedRate` deleted) |
| R1, R3, R4, R5, R6, R10 | Done. R4: tooling and default address only; no contract deployed |
| R2, R8 | Done in Valise (catalog table generated; live readiness shown) |
| R7 | Daemon: done (encrypted keystore). Valise: open until the suite pin carries the keystore |
| R9 | UTXO transport gate (1.9) in CI; EVM gate against anvil passes 19/19 (CI run 42406472393). Open: Solana (`solana-test-validator`), and two-daemon runs with an XFG testnet |
| R11 | Open: deleting `src/SwapDaemon/CLV/` needs the owner's explicit approval |
| F29 | Open: consensus fee split; owner decision |
| F33, R12 | Open: protocol change; the owner chooses (a), (b) or (c), and whether hash-locked pairs stay ACTIVE meanwhile |
| F30-F32 | F30, F31 fixed. F32 fixed: fill results carry the claim key and are signed by the offer's maker (they were unsigned gossip); AFK itself stays disabled by `AFK_SOFT_ORDER_EXECUTION_ENABLED` |

## 6. Order

1. Phase 0 (blocks everything; no verified build exists today)
2. Phase 1 for BTC, gated on its regtest matrix; then LTC and BCH
3. R3 + R4 + R5 (EVM trust and config) in parallel with Phase 1
4. Phase 3.1 / 3.2 (small, needs network access to the feeds)
5. Phase 2 and Phase 3.3 (Tempo) only if their value justifies the work
