# Fuego Security Audit — Consolidated Findings

Single source of truth for every finding across both audit rounds.
Round 1 detail, fix recipes and verification steps live in
[`SECURITY_AUDIT_FIX_GUIDE.md`](SECURITY_AUDIT_FIX_GUIDE.md).

- **Round 1 scope:** whole codebase at `origin/master` @ `c2a968b9e` (2026-10-08), source-only
- **Round 2 scope:** consensus economics + wallet files (recommended round-2 scope)
- **Method:** Trail of Bits skill disciplines (static c-review, constant-time source scan,
  zeroize scope, sharp-edges, fp-check triage) + Fuego domain invariants
  (`cryptonote-privacy-audit`, `atomic-swap-security`)
- **Status legend:** FIXED = code landed and verified · OPEN = not fixed · DECISION = needs an
  owner decision before code changes · FP = false positive, do not "fix"

## Summary

| Severity | Count | Fixed | Open / Decision |
|----------|-------|-------|-----------------|
| CRITICAL | 1 | 0 | 1 (R2-1, protocol change) |
| HIGH | 1 | 1 | 0 |
| MEDIUM | 3 | 3 | 0 |
| LOW | 7 | 0 | 7 |
| INFO | 7 | 1 | 6 (5 dead-code/hygiene, 2 documented) |
| FP | 3 | — | n/a |

**No fund-loss primitive was found in round 1's scoped paths.** Round 2 found one in the
treasury vault (R2-1), which is worse than anything round 1 turned up.

## Round 1 — full codebase

| ID | Sev | Title | Location | Status |
|----|-----|-------|----------|--------|
| H-1 | HIGH | Uncaught `boost::bad_get` in `pushTransaction` KeyInput rollback → remote daemon crash | `CryptoNoteCore/Blockchain.cpp:5951` | **FIXED** + 12 tests |
| M-1 | MEDIUM | `pushTransaction` return ignored; partial connect state never rolled back | `Blockchain.cpp:3890`, `5969-6158` | **FIXED** |
| M-2 | MEDIUM | `SwapEscrow` key image missing Ed25519 domain check | `Blockchain.cpp:6984` | **FIXED** |
| M-3 | MEDIUM | Path traversal in `exportWallet` / `exportWalletKeys` (plaintext key write to arbitrary path) | `PaymentGate/WalletService.cpp:727` | **FIXED** |
| L-1 | LOW | `TransactionInputUnified` rejected by validation but referenced by push | `Blockchain.cpp:2653`, `5993` | OPEN |
| L-2 | LOW | ChaCha8 (reduced-round) for swap secret-at-rest encryption | `SwapDaemon/SwapSecretEncryption.cpp` | OPEN |
| L-3 | LOW | Rust `AdaptorSecret` derives `Debug` (preimage logging footgun) | `fuego-swapd-adaptor/src/lib.rs:48` | OPEN |
| L-4 | LOW | No working secure-zero primitive (`sodium_memzero` declared, never defined/used) | `crypto/crypto-util.h:31` | OPEN |
| I-1 | INFO | `oaes_sprintf` / `oaes_key_gen_*` / weak `oaes_get_seed` — all dead and unreachable | `crypto/oaes_lib.c:433,621` | OPEN (delete) |
| I-2 | INFO | `popen("which tor")` etc. — fixed strings, no injection; PATH-hijack caveat | `FuegoTor/Fuegotor.cpp:329` | OPEN (optional) |
| I-3 | INFO | `PointTimelock.sol` notes: permissionless `refund()`, id-squat, on-chain secret | `contracts/point-timelock/src/PointTimelock.sol` | DOCUMENTED |
| I-4 | INFO | Mempool accepted intra-tx duplicate key images | `CryptoNoteCore/TransactionPool.cpp:335` | **FIXED** by H-1 |
| I-5 | INFO | Full epoch-mint dual paths (`Blockchain.cpp:5428/5478`), testnet gate `5431` — not traced | — | DEFERRED (round 3) |

### H-1 in one paragraph
`checkTransactionInputs` tested every input only against *chain* state, so a transaction
spending the same key image twice passed validation and then failed the connect-path rollback,
which erased earlier inputs with a type-blind `boost::get<KeyInput>` and threw an uncaught
`boost::bad_get` on the first non-`KeyInput` element. Reachable from any P2P peer or mempool
submission → `std::terminate`. Fixed with a shared `checkKeyImagesUnique()` guard spanning all
five key-image-carrying input types, a rollback that tracks its own inserts, and a `try/catch`
barrier at the connect site. `popTransaction` was verified type-guarded already, so unwind is safe.

## Round 2 — consensus economics + wallet files

| ID | Sev | Title | Location | Status |
|----|-----|-------|----------|--------|
| R2-1 | **CRITICAL** | Treasury vault spend key derivable from public data | `Treasury/VaultKeys.cpp:19`, `CryptoNoteConfig.h:306` | **DECISION** — in progress on `codex/treasury-vault-auth` |
| R2-2 | LOW | Wallet files use ChaCha8 (AEAD around it is sound) | `Wallet/WalletGreen.cpp:2293` | OPEN |
| R2-3 | INFO | 69/11/20 epoch fee split strands integer dust | `Blockchain.cpp:5082-5084` | OPEN |
| R2-4 | PROCESS | `fuego-currency` skill doc stale (says 80/20; code is 69/11/20) | `~/.config/opencode/skills/fuego-currency/` | OPEN |
| R2-5 | LOW | Early `return false` from mutating epoch work | `Blockchain.cpp:5104`, `:1233` | OPEN |

### R2-1 in one paragraph
`deriveVaultKeys()` builds the treasury vault spend key as
`hash_to_scalar(keccak(genesisHash ‖ VAULT_KEY_SEED))`. `VAULT_KEY_SEED` is a hardcoded string
in `CryptoNoteConfig.h:306`; the genesis hash is the public first block. Both inputs are public,
so anyone recomputes the spend key. The only other gate, `VaultPolicy::isPermitted`
(`Treasury/VaultPolicy.cpp:41-100`), is a pure content predicate — CD_APY_POOL passes if any
`CommitmentSpend` declares `claimedInterest > 0`, LP_RESERVE if an AMM add/remove tag is
present, GENERAL_RESERVE if a mint-auth tag is present — and never checks key knowledge. The
vault pubkey never backs an on-chain output in the consensus path, so there is no ring signature
to fall back on either. Exposed: CD_APY_POOL (the sole backing for every CD interest claim, per
the fee-pool aggregate cap in `checkCommitmentSpendInput`), BONUS_VAULT, and treasury reserves.
Getting a spend on-chain still requires block inclusion, so it is not a free remote steal — but it
is the only cryptographic barrier, and it is not one. Rotating the derivation is a protocol change
needing an activation/migration plan, hence DECISION rather than a patch.

## False positives — do not "fix" these

Three prior claims were rejected against source. Re-filing them would waste a day and risk
"fixing" correct code.

| Claim | Location | Why it is wrong |
|-------|----------|-----------------|
| `sc_check` timing leak | `crypto/crypto-ops.c:2881` | Branchless straight-line compare; operates on to-be-validated **public** scalars |
| `signum` "division by -a" timing leak | `crypto/crypto-ops.c:2877` | Factually wrong: body is `(a>>63)-((-a)>>63)` — shifts only, no division, no branches |
| `sc_mulsub` timing leak | `crypto/crypto-ops.c:2555` | Standard ref10 field arithmetic; no secret branches or secret-indexed loads. Local count is not a timing signal |

Also rejected on review: `oaes_sprintf` "buffer overflow" (it length-checks up front at
`oaes_lib.c:442-449` and has zero callers) and the `popen` "command injection" (all four
commands are string literals with no user input; PATH-hijack is a local-attacker prerequisite).

## Verified sound (sampled, with evidence)

- **Key images:** domain gates ×5, global `spentKeys` + per-type insert/erase, multisig via
  `checkMultisignatureInputsDiff` + `isUsed`, escrow dual-marking + timeout + amount/script
  checks (`Blockchain.cpp:6984-7076`)
- **Swaps:** DLEQ verified before binding with clean zero-out reject (`SwapDaemon.cpp:4288-4294`),
  first-wins adaptor binding (nonce-reuse comment at `4220-4229`), TOFU key binding + per-message
  signature verify (`4190`, `4213`), PTLC-downgrade block, per-family hashlock digests correct
  (keccak EVM/Solana, SHA256 UTXO), wall-clock `timelockOrderingOk` enforced (`1173`, `1422`),
  MuSig2 nonce single-use + zeroed
- **Economics:** fee split cannot over-allocate (69+11+20=100, all floor divisions); epoch CD
  denominator correctly excludes CDs created in the closing block; `MINT_BURN` 50/50 and
  treasury 60/40 sub-allocation match `AGENTS.md`
- **Wallet files:** AEAD with proper key separation (HMAC-derived MAC key, domain label
  `"fuego-wallet-hmac"`), constant-time `CRYPTO_memcmp` tag verify, fail-closed (`WalletGreen.cpp:2255`)
- **Surface:** Levin 20 MB pre-alloc cap + connection limits; SwapP2P 1 MB frame cap and
  `swapId <= 256`; RPC localhost-default + restricted mode + basic auth; wallet logs pubkeys only;
  RNG `/dev/urandom`-seeded and fork-reseeded

## Not covered — recommend round 3

OSPEAD decoy bias · MLSAG/RingCT math · full epoch-mint trace (dual paths `5428/5478`) ·
difficulty/emission arithmetic · `popTransactions` completeness per accumulator ·
deep-reorg-beyond-history divergence (logged, by design) · wallet-file/mnemonic entropy and
`PaymentGate` auth · Go `swapxfg/app` + tui fee-split logic · Rust↔C++ FFI wiring · vendored deps ·
any dynamic or fuzz testing.

## Verification log

| Gate | Result |
|------|--------|
| Full build (`build/release`, cmake regenerated) | PASS, no new warnings |
| `ctest` | 6/6 PASS |
| `core_tests` | 202/202 PASS, exit 0 |
| `key_image_uniqueness_tests` | 12/12 PASS, exit 0 |
| H-1 suite mutation check | 7 duplicate cases FAIL when guard neutered, 5 controls still PASS — test discriminates |

Note: `build/release` was configured from an older commit and still listed the deleted
`AssetType.cpp`, breaking every build until `cmake ../..` was re-run. Not a source change.

## Related

- Fix recipes and per-finding test plans: [`SECURITY_AUDIT_FIX_GUIDE.md`](SECURITY_AUDIT_FIX_GUIDE.md)
- Implementation plan for the remaining OPEN items: [`superpowers/plans/2026-10-08-security-audit-remediation.md`](superpowers/plans/2026-10-08-security-audit-remediation.md)
- Per-change task lists and sign-offs: root `CHANGELOG.agent.md`
