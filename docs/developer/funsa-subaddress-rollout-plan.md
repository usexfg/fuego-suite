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

| V10 | HEAT send to a funsa recipient: per-output keys for the recipient's commitment output, as `WalletTransactionSender::makeHeatTransferV10Request` does. V1–V9 shipped refusing it, short of V5 | `transaction_builder.rs` (`build_commitment_spend_transaction`), `wallet_service.rs` (`send_heat`) | a Valise HEAT send to a funsa address passes `validateSubaddressTxExtra` and is credited by fuego-suite |
| V11 | Alias registration (section 4.4): `register_alias` builds the 0xEA field with a funsa owner at (0, `ALIAS_MINOR_BASE` + next k) and `addressHash = H(D ‖ C)`; refused before v12. Today `register_alias` always returns "not part of the transaction builder path" | `serialization.rs`, `wallet_service.rs`, Dart `alias_registration_screen.dart` | the registration passes F9 consensus in a suite test |
| V12 | View-only wallets, released with network v12: open from view secret + spend public key; scan with the main and 0x06 keys; credit V2 outputs flagged unverified (no Janus check without the spend secret); amounts only, no spend detection, since key images need the spend secret; v12 hidden amounts decrypt with the view key | `scanner.rs`, `wallet_service.rs`, Dart wallet import | view-only scan of the golden transactions credits the same amounts as the full wallet and flags every V2 output |
| V13 | ProofV2 in `get_tx_proof`: one item per output key that paid the address `(tx key index, r_n·X, DLEQ signature)`, as `SubaddressTxProof.cpp` | `transaction_builder.rs`, `wallet_service.rs` | a Valise ProofV2 verifies through daemon `check_tx_proof` |

Order: V1 → V3 → V4 (receive works) → V2 → V5 (send works) → V6–V8 → V9 → V10, V11, V13 → V12 with network v12.
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

Reviewed 2026-11-25: usexfg/fuego-explorer @ 8f58988 and usexfg/xfg-explorer @ 475b69e.
Both are the same static site (jQuery pages over daemon JSON-RPC or the gateway's
`/v1/daemon/*`) with diverged copies of the tools. Neither knows funsa, 0x06 or 0x09.

Defects found, funsa or not:

| # | Defect | Where | Effect |
|---|---|---|---|
| X1 | The tx checker reads address keys at fixed offsets for a 2-byte prefix (fuego-explorer, `t = 4` hex) or a 4-byte prefix (xfg-explorer, `s = 8`). `fire` (1753191) is a 3-byte varint | `js/check_txn.js` in both | every mainnet check compares the wrong 32 bytes: owned outputs are never found and a correct view key is reported as not matching |
| X2 | Address length is hard-coded (98 / 99) and the prefix is never checked | same | funsa (101) is refused; any network's address with a matching checksum is accepted |
| X3 | fuego-explorer divides amounts by 10^6; XFG has 7 decimals (`coinUnits = 10000000`) | `js/check_txn.js` | owned amounts shown 10× too large |
| X4 | Mixin is hard-coded to 4 on the tx page; the tooltip says 8–18 | `pages/blockchain_transaction.html` | every tx shows a false ring size |
| X5 | Amounts are rounded to 6 of 7 decimals | `blockchain_transaction.html` (`getReadableCoins(…, 6)`) | the last digit is dropped |
| X6 | Outputs render `target.data.key`; a commitment output (HEAT / CD, v11) has `commitKey` | `blockchain_transaction.html` | commitment outputs show `undefined` |
| X7 | Search accepts heights, 64-hex hashes and payment IDs only | `index.html` | an address cannot be searched |

Work, for whichever explorer is deployed (pick one; maintaining both copies is how X1–X3 diverged):

| # | Work |
|---|---|
| E1 | Fix X1–X6. The tx checker decodes the prefix varint, accepts `fire`/`TEST` and funsa/`Tsub` of the selected network, and for a funsa address checks the view key as `C == a·D` (not `a·G`), tries the main key, then each 0x06 key, and reports which matched |
| E2 | Show 0x06 and 0x09 in decoded tx extra, with the per-output key next to each output |
| E3 | Address search: validate through gateway A2 and show the kind. An explorer cannot list a subaddress's payments (it has no view key), so show it as valid but unscannable |
| E4 | Payment-proof checker: ProofV1 and ProofV2 through gateway A1 |
| E5 | Count subaddress-shaped txs per block only as an aggregate; do not label single transactions, as that amplifies the 0x09 fingerprint. After F8's grace window every key-input tx carries 0x09, so the label means nothing per tx anyway |

## 4. fuego-suite follow-ups

| # | Work | Why |
|---|---|---|
| F1 | `fuego-chainscan`: walks a node's blocks over RPC and reports every tx whose extra parses with a 0x06 or 0x09 field, every commitment output, and every block the current subaddress rules would reject | upgraded nodes reject such a block before v11; one hit means resync breaks; a commitment output would be stranded by F5 |
| F2 | Subaddress kind and index in the key record (section 4.2) | a cache that fails to load turned V2 records into standard ones: Janus off, linkable address shown |
| F3 | walletd JSON-RPC `createSubaddress {major, minor}` → `{address}` and `getSubaddresses` → `{subaddresses: [{major, minor, address}]}`, through `IWallet::createSubAddress` / `getSubAddresses` | WalletGreen had the functions but walletd exposed no method |
| F4 | Clang, macOS and Windows builds of the branch | only Linux GCC is verified |
| F5 | `commitment_owner_bound_tests`: 33e787a5 removed the legacy commitment form (spendable by the sender and any view-key holder) but kept five tests that asserted it. The tests now assert the refusal; F1 counts commitment outputs on mainnet to confirm no legacy HEAT/CD exists that the removal would strand | the CI test step runs `tests/`, so this showed red |
| F6 | Not needed: no aliases are registered on mainnet, so none needs moving to a funsa owner. Correction 2026-11-25: only WalletGreen issues funsa owners; SimpleWallet's registration always fails and Valise has none. See F9 | closed 2026-11-17 |
| F7 | Explain subaddress-to-primary linking in user docs: do not publish the primary address next to subaddresses | the Janus check does not cover it |
| F8 | Every key-input tx carries 0x09 after the grace window (section 4.1) | approved 2026-11-17; removes the single-key fingerprint at 34 bytes per tx |
| F9 | From v11 every alias owner is a funsa subaddress (section 4.4) | owner request 2026-11-25; an alias on a standard address links the alias to the wallet's primary address forever |

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

### 4.3 F1: running the mainnet scan

Run against a fully synced mainnet node built from any version:

```
fuego-chainscan --daemon-address 127.0.0.1:18180
```

It pages through `/getblocks.bin` from genesis (`--from` / `--to` narrow the range) and prints one
line per finding, `height=… tx=… finding=0x06|0x09|commitment-output|rejected detail="…"`, then a
summary. A reorganisation during the scan is detected and the new branch re-inspected.
Exit status 0 means nothing was found and the gate item "F1 scan result recorded" can be signed;
1 means findings (any `rejected` line before v11 breaks a resync with this release, and any
`commitment-output` is a HEAT/CD the F5 removal would strand); 2 means the scan did not finish.
Record the summary line and the node's height in the changelog.

Status 2026-11-25: not run. The agent container reaches the internet only through an
HTTPS proxy; the seed nodes' P2P port 10808 and RPC port 18180 refuse its connections,
and no public HTTPS node RPC exists in any of these repositories. Run it on a machine with
a synced node.

### 4.4 F9: alias owners are funsa subaddresses

Problem, as of 467b8562:
- Consensus never parses a registration's `ownerAddress`, and never checks `addressHash`
  against it. Any string with any hash registers; one-alias-per-address is enforced on a
  hash the registrant chose.
- WalletGreen sets `addressHash` from the master keys while the owner is a funsa
  subaddress. Anyone holding the master address computes the hash, finds the alias, and
  reads the funsa owner from it: the funsa address is linked to the master wallet.
- WalletGreen picks the owner at a random 32-bit major index. A wallet restored from seed
  never scans that index, so payments to the alias are not found.
- SimpleWallet parses the funsa owner with the standard-only parser (fails), then sets
  `ownerAddress = ""` (fails `isValid`). Its registration cannot succeed.
- Valise has no registration.

| Item | Rule |
|---|---|
| Consensus, from v12 (owner decision 2026-11-27: v11 blocks and the pool refuse every alias tag) | a transaction whose extra has a 0xEA registration is invalid unless `ownerAddress` parses as a funsa subaddress of this network and `addressHash == cn_fast_hash(D ‖ C)` of that owner. A 0xED transfer is invalid unless `newOwnerAddress` is a funsa subaddress of this network. Applied at mempool admission, block template and block validation, like `validateSubaddressTxExtra` |
| Before v11 | unchanged: blocks before v11 are history, and funsa does not exist yet. Wallets refuse to register before v11 rather than create an alias on a standard address |
| Release, old owner | an alias registered before v11 keeps its standard owner; release and transfer still verify against it, and a transfer moves it to a funsa owner |
| Wallet owner | owners are `(0, ALIAS_MINOR_BASE + k)` with `ALIAS_MINOR_BASE = 0x80000000` and k from 0: the first owner not in the wallet and not already an alias owner. Payment subaddresses are issued from minor 1 upward and never reach the base, so an owner is never a subaddress given to a payer, even after a restore. A restore scans a lookahead window at minor 1 and a second window starting at `ALIAS_MINOR_BASE`, each with its own gap limit. Valise adopts the same base. Never a random index |
| Wallet hash | `addressHash` from the owner's own keys, never the master keys |
| Ownership proof | release and transfer are signed with the owner's spend secret, `b + m` for a funsa owner. No wallet in these repositories builds 0xEC or 0xED today |

Consequences:
- After v11, any third-party tool that registers aliases on standard addresses has its transactions rejected.
- A registrant can still put a victim's funsa address in a registration (paying the fee) and so take that address's one alias. The victim's fix is a new subaddress. A registration ownership signature would close this; it is not part of F9.
- `/get_alias_by_address` with a master address stops finding the alias, by design.

## 5. Gate before v11 activation

1. fuego-suite branch merged after review, CI green on all platforms (F4, F5).
2. F1 scan result recorded.
3. Valise V1–V9 released, V5 including the F8 filler, with the cross-implementation test passing in both directions.
4. xfg-api A1–A4 deployed.
5. Testnet soak: funsa sends between fuego-suite walletd, simplewallet and Valise, including
   HEAT sends, change from a subaddress, view-only wallets, restart and reload, and ProofV2
   checks through the gateway.
6. F9 merged, with WalletGreen, SimpleWallet and Valise (V11) registering funsa owners.
