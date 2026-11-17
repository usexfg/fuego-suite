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
| F2 | Make subaddress metadata durable outside the cache (or rebuild it by scanning indexes) | a cache that fails to load turns V2 records into standard ones: Janus off, linkable address shown |
| F3 | walletd JSON-RPC methods for `createSubAddress` / `listSubAddresses` | WalletGreen has the functions but walletd exposes no method |
| F4 | Clang, macOS and Windows builds of the branch | only Linux GCC is verified |
| F5 | `commitment_owner_bound_tests` (5 cases fail on `master` since 33e787a5) | the CI test step now runs `tests/`, so this shows red until fixed |
| F6 | Alias move to a funsa owner after v11 (0xED transfer) and an owner-signed release flow | plan section G; registration already issues funsa owners |
| F7 | Explain subaddress-to-primary linking in user docs: do not publish the primary address next to subaddresses | the Janus check does not cover it |
| F8 | Optional: every v11 tx carries a 0x09 key so subaddress txs are not recognisable | removes the single-key fingerprint at about 34 bytes per tx |

## 5. Gate before v11 activation

1. fuego-suite branch merged after review, CI green on all platforms (F4, F5).
2. F1 scan result recorded.
3. Valise V1–V9 released, with the cross-implementation test passing in both directions.
4. xfg-api A1–A4 deployed.
5. Testnet soak: funsa sends between fuego-suite walletd, simplewallet and Valise, including
   HEAT sends, change from a subaddress, view-only wallets, restart and reload, and ProofV2
   checks through the gateway.
