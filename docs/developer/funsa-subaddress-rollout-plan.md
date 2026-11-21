# funsa V2 subaddresses: rollout plan after the fuego-suite core

Status on 2026-11-17. The fuego-suite core is done on branch
`claude/subaddy-master-compat-lekzke` (see the top entry of `CHANGELOG.agent.md`).
This plan covers what remains: fuego-valise, xfg-api, the explorer, and fuego-suite
follow-ups. Nothing below is started.

## 0. Wire format every implementation must match

Activation: block major version 11 (`funsaSubaddressActivated`). Before v11, nodes reject
0x06 and 0x09.

| Item | Definition |
|---|---|
| Prefix | mainnet `funsa…`, 101 characters, varint `23047589992`; testnet `Tsub…`, varint `222156832` |
| Index scalar | `m = H_s("FuegoSubAddrV2" ‖ a ‖ major_LE32 ‖ minor_LE32)` |
| Keys | `D = B + m·G`, `C = a·D` (no cofactor), spend secret `b + m` |
| Per-output secret | `r_n = H_s("FuegoAddlTxKey" ‖ r ‖ n_LE64)` |
| Single-key tx | exactly one distinct subaddress destination and no other non-change destination: main key `R = r·D`, `0x09 = [r·G]`, change derived from the sender's `a·R` |
| Per-output tx | `0x06 = [R_n]` with `R_n = r_n·D_n` (subaddress) or `r_n·G` (standard); `0x09 = [J_n]` with `J_n = r_n·G` (subaddress) or a random point (standard); both lists one key per output |
| 0x06 / 0x09 encoding | tag, count byte (1–255), count × 32-byte keys |
| Consensus | 0x06 count = outputs and requires 0x09 count = outputs; 0x09 without 0x06 has one key; no duplicate tags; every key a valid point |
| Crediting | full wallet: a subaddress output counts only if `(b+m)·J == R` for the key that matched it; view-only wallet: credit and flag unverified |
| Messages | for a single-key tx, decrypt with the 0x09 key (`r·G`), not the main key |
| Proof | `ProofV2` + base58 of items `(tx key index, r·X, DLEQ signature)`, index `0xFF` = main key, at most 256 items |

Golden vectors: export them from `tests/UnitTests/SubaddressScannerTest.cpp` and
`SubaddressV2Test.cpp` (fixed seeds) into a JSON file that each port tests against
byte for byte. Without them, a port can agree with itself and still disagree with the chain.

## 1. fuego-valise

Valise does not use fuego-suite's walletd for subaddresses. Its walletd is the Rust
crate `rust-fuego-wallet/core` on top of `fuego-sdk`. Today it derives V1 (`"Sublime"`,
`C = A`, `fire` prefix) subaddresses with a gap-limit lookahead table, scans only the
main tx key, and builds every output from the main key. The Dart send screen refuses any
address that is not `fire…` with 98 characters.

| # | Work | Where | Done when |
|---|---|---|---|
| V1 | V2 derivation, `additionalTxSecret`, `subaddressTxPubKey`, `janusCheck` | `fuego-sdk/fuego-crypto/src/subaddress.rs` | golden vectors match fuego-suite |
| V2 | funsa/Tsub address encode and parse that returns the subaddress flag; standard-only parse for mining and alias owners | `fuego-crypto`, `core/src/base58.rs`, `fuego-sdk/.../vault.rs` | round-trip and refusal tests |
| V3 | Parse 0x06/0x09 lists | `fuego-sdk/.../serialization.rs` | round-trip plus malformed-count tests |
| V4 | Scanner: V2 keys in the lookahead table; try the main key, then `R_n`; record the matched key; strict Janus check for V2 entries; view-only flag | `fuego-sdk/.../scanner.rs` | Janus-probe, decoy-0x06 and view-only tests ported from `SubaddressScannerTest` |
| V5 | Builder: single-key and per-output modes for `build_transaction` and `build_mixed_output_transaction`, HEAT sends included; 0x06/0x09 counts cover every output, fee change included | `fuego-sdk/.../transaction_builder.rs` | a tx built in Rust passes `validateSubaddressTxExtra` and is credited by fuego-suite's scanner (cross-implementation test) |
| V6 | walletd RPC: `create_subaddress` returns funsa; `get_subaddresses` lists V2 and the V1 legacy set; transactions report `unverifiedSubaddressOutputs` | `core/src/wallet_service.rs`, `server.rs` | `subaddress_methods_reach_the_wallet` updated |
| V7 | Legacy migration: existing V1 subaddresses keep scanning and spending; the sweep moves their funds; new ones are V2 only | `wallet_service.rs` (`sweep_legacy_subaddresses` exists) | V1 regression test |
| V8 | Dart: send screen accepts `funsa`/`Tsub` (101/99 characters) through the native parser, not by prefix and length; payment-URI scan (`send_screen.dart:84`) accepts those lengths; receive screen shows funsa; transaction list shows the unverified flag | `lib/screens/transactions/send_screen.dart`, `receive_screen.dart`, `lib/models/network_config.dart` | widget tests for both address forms |
| V9 | Bump the `fuego-suite` submodule to the merged commit (CryptoNight and FFI build from it) | `fuego-suite` submodule | FFI build on each platform the `fuego-ffi` skill lists |

Order: V1 → V3 → V4 (receive works) → V2 → V5 (send works) → V6–V8 → V9.
Risk: V5. A count mismatch makes consensus reject the tx; a wrong derivation loses funds.

## 2. xfg-api (usexfg/xfg-api @ 1a19fd4)

The gateway passes requests through to daemon and wallet RPC, so most funsa behaviour
arrives without code. Gaps:

| # | Work | Where |
|---|---|---|
| A1 | Route for daemon `check_tx_proof` (none today; only `check_tx_key`, `check_tx_with_view_key`, `check_reserve_proof`), documented as accepting ProofV1 and ProofV2 | `gateway/server.js`, `openapi/fuego-openapi.yaml` |
| A2 | Route for daemon `validateaddress` (`valid`, `is_subaddress`, `subaddress_version`) | same |
| A3 | OpenAPI: destination addresses accept funsa/Tsub; transaction objects carry `unverifiedSubaddressOutputs`; `getblocktemplate`/mining responses document the subaddress refusal | `openapi/fuego-openapi.yaml`, `gateway/openapi.yaml` |
| A4 | Regenerate the ten client SDKs from the updated spec | `openapi/generate_sdks.sh` |
| A5 | Gateway tests for A1–A2 against a stub RPC, like `gateway/test/public-audit-route.test.js` | `gateway/test/` |

Check: `/v1/node/alias/by-address/:address` returns nothing for a funsa address until
aliases can be owned by one after v11. Document that rather than changing it.

## 3. Explorer

No explorer repository is attached to this session. xfg-api's
`explorer-integration/README.md` describes moving the explorer onto the gateway. Work for
whichever explorer is deployed:

- Show 0x06 and 0x09 in decoded tx extra, with the per-output key next to each output.
- Recognise funsa/Tsub in search and address pages; an explorer cannot list a
  subaddress's payments (it has no view key), so show the address as valid but unscannable.
- Payment-proof checker: ProofV1 and ProofV2 through A1.
- Count subaddress-shaped txs per block only as an aggregate; do not label single
  transactions on the page, as that amplifies the 0x09 fingerprint.

## 4. fuego-suite follow-ups

| # | Work | Why |
|---|---|---|
| F1 | Scan mainnet for any historical tx whose extra parses with a well-formed 0x06 or 0x09 field | upgraded nodes reject such a block before v11; one hit means resync breaks |
| F2 | Subaddress kind and index in the key record (section 4.2) | a cache that fails to load turned V2 records into standard ones: Janus off, linkable address shown |
| F3 | walletd JSON-RPC methods for `createSubAddress` / `listSubAddresses` | WalletGreen has the functions but walletd exposes no method |
| F4 | Clang, macOS and Windows builds of the branch | only Linux GCC is verified |
| F5 | `commitment_owner_bound_tests` (5 cases fail on `master` since 33e787a5) | the CI test step now runs `tests/`, so this shows red until fixed |
| F6 | Not needed: no aliases are registered on mainnet, so none needs moving to a funsa owner. Registration already issues funsa owners | closed 2026-11-17 |
| F7 | Explain subaddress-to-primary linking in user docs: do not publish the primary address next to subaddresses | the Janus check does not cover it |
| F8 | Every key-input tx carries 0x09 after the grace window (section 4.1) | approved 2026-11-17; removes the single-key fingerprint at 34 bytes per tx |

### 4.1 F8: 0x09 on every key-input transaction

Problem: only single-key subaddress txs carry 0x09 without 0x06, so the tag alone marks
a payment to a subaddress.

| Item | Rule |
|---|---|
| Filler | a tx with no subaddress destination carries `0x09 = [J]`, `J = H_s("FuegoJanusFiller" ‖ r)·G`. Without `r`, `J` is indistinguishable from the `r·G` of a single-key subaddress tx. Deterministic, so two swap parties building from one `r` agree |
| Builders | add the filler once the chain tip is a v11 block: `TransactionImpl` seals it before the first signature (WalletGreen, WalletLegacy), `constructTransaction`, SwapDaemon `fundEscrow` and `buildUnsignedEscrowSpend`. Swap claim and refund txs spend `TransactionInputSwapEscrow`, not key inputs, and stay unchanged |
| Consensus | from `upgradeHeight(11) + JANUS_BASE_GRACE_BLOCKS` (720 blocks, about 4 days), a tx with at least one `KeyInput` must have an extra that parses and contains 0x09. Coinbase and txs without key inputs are exempt |
| Grace | between v11 and v11 + 720, 0x09 is optional. Mempool txs built before v11 (pool lifetime 12 h = 90 blocks) and saved swap txs can still be mined |
| Mempool | admission and `fill_block_template` apply the rule at the height they target, so a tx admitted during grace without 0x09 is never placed in a block that enforces the rule |
| Messages | the receiver picks the message key by its own address kind: 0x09 key only for a funsa recipient of a single-key tx; standard recipients always use the main key. Before F8, a lone 0x09 was enough, which would break messages to standard addresses once every tx carries a filler |

Consequences:

- After v11 + 720, every wallet, exchange, pool payout script and third-party builder that does not add 0x09 has its txs rejected. This is a hard fork for tx builders, not only for nodes.
- A key-input tx whose extra does not parse is rejected after the grace window. No builder in this repository writes a tag the parser refuses; external tools that put arbitrary bytes in extra will break.
- Valise V5 must add the filler before v11 + 720.
- SwapDaemon parties on different versions build different escrow-spend prefixes and fail to co-sign. Both must upgrade before v11 + 720.

### 4.2 F2: subaddress kind and index in the key record

Problem: whether an address is a V2 subaddress, and its (major, minor), live only in the
wallet cache. A cache that fails to load, or a file saved with keys only, turns V2
records into standard ones: the Janus check goes off and the linkable `fire` form is
shown. The kind cannot be recomputed: the stored spend key `b + m` looks like any random
key without the index, and `createSubAddress` accepts any 32-bit index, so no bounded
search is complete.

| Item | Rule |
|---|---|
| Record v10 | each address record's encrypted, HMAC-protected plaintext is `spend public ‖ spend secret ‖ timestamp ‖ kind (u8) ‖ major (u32 LE) ‖ minor (u32 LE)`; kind 0 = standard, 2 = V2 subaddress, 0xFF = not yet recorded. File version 10 |
| Authority | the record's kind wins over the cache. A recorded V2 kind is checked on load: `B + H_s(a, major, minor)·G` must equal the stored spend public key, or loading fails |
| Upgrade | a v8 or v9 file is rewritten as v10 on load (original kept as `<file>.v9.backup`). Non-primary records start as "not yet recorded"; the first cache load fills their kind, checks the derivation, and writes it into the record. If the cache never loads, those records stay unrecorded and are treated as standard, the pre-F2 behaviour, until a cache load succeeds |
| Old binaries | refuse a v10 file ("Unsupported wallet version") instead of loading V2 records as standard ones |
| Record crypto | one module seals and opens records; sealing always takes the next IV and advances it |

Pre-existing defects fixed on the way, all ChaCha8 keystream reuse:
`reset()` re-encrypted every record under one IV; the legacy-file conversion encrypted the
first spend record under the view keys' IV; the v7→v8 migration encrypted every record
under one IV.

## 5. Gate before v11 activation

1. fuego-suite branch merged after review, CI green on all platforms (F4, F5).
2. F1 scan result recorded.
3. Valise V1–V9 released, V5 including the F8 filler, with the cross-implementation test passing in both directions.
4. xfg-api A1–A4 deployed.
5. Testnet soak: funsa sends between fuego-suite walletd, simplewallet and Valise, including
   HEAT sends, change from a subaddress, view-only wallets, restart and reload, and ProofV2
   checks through the gateway.
