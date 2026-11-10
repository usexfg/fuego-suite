# Cross-curve point locks (F33, R12a)

> Version 1.0 — 2026-11-08 — Status: IN IMPLEMENTATION
> Owner decisions (2026-11-08): R12 option (a); hash-locked pairs stay ACTIVE until their point lock
> ships; BTC, LTC and BCH move to ACTIVE (remediation 1.8); R11 (delete `src/SwapDaemon/CLV/`) approved;
> the Valise suite pin may change; all suite fixes go to usexfg/fuego-suite as one pull request.

## 1. Problem (F33)

Alice locks the counterparty coin to a hash lock H that Bob sends. Nothing proves H = hash(t) for the t
behind Bob's adaptor point T, so a modified Bob can claim with a preimage of his own and refund his XFG.
The fix replaces the hash lock with a lock that opens only with t itself, and proves that the point the
lock uses and the XFG adaptor point share t.

## 2. Target protocol

1. Bob draws t below 2^252 (a valid scalar on ed25519 and secp256k1), publishes T_ed = t·G_ed (XFG
   adaptor point, unchanged) and T_secp = t·G_secp, and a cross-curve proof π that both have the same
   discrete log, bound to the swap's DLEQ context.
2. Alice verifies π before she funds anything. For a pair her client can lock by point, she refuses any
   hash-lock type (a modified Bob cannot downgrade her to F33). For a pair without a point lock the hash
   lock remains, with F33.
3. The counterparty lock opens only with t:

| Family | Lock | Claim | Alice learns t from | Needs π |
|---|---|---|---|---|
| EVM (ACTIVE and ADAPTER chains) | `PointTimelock.lock(recipient, address(T_secp), timeout)` | `claim(id, t)`; the contract checks t·G_secp through `ecrecover` | the claim's calldata or contract storage | yes |
| SOL | `lock_ptlc(T_ed)` | a claim that checks t·B = T_ed on chain (curve25519 syscall) | the claim instruction data | no (same curve as XFG) |
| BTC, LTC | P2TR, unspendable internal key, two leaves: claim `<A> CHECKSIGVERIFY <B> CHECKSIG`, refund `<timeout> CLTV DROP <A> CHECKSIG` | Bob completes Alice's BIP340 adaptor pre-signature (encrypted to T_secp) on the claim transaction and adds his own signature | s − s' from Alice's completed signature in the witness | yes |
| BCH | P2SH with the same two branches | the same, with BCH Schnorr signatures (FORKID sighash) | the same | yes |

4. XFG side unchanged: Alice completes the XFG adaptor signature with t; Bob refunds XFG after its
   timeout if he never claims.

## 3. Work items

| # | Item | Done when |
|---|---|---|
| X1 | Cross-curve DLEQ. Revised 2026-11-10 (owner): use a library with production exposure instead of an in-house proof. `sigma_fun` 0.9.0 (`ext::dl_secp256k1_ed25519_eq`, the proof xmr-btc-swap and eigenwallet use; 0BSD) behind a C interface in `src/SwapDaemon/Crypto/cross_curve_dleq` (Rust static library, `Cargo.lock` committed, built by cargo from CMake). Its two Pedersen generators are hashed to the curves here, because the proof is unsound when their discrete logs are known. The in-house `cross_dleq.cpp` proof is removed from use; `cross_dleq_generate_scalar` stays | Rust tests: completeness, statement binding, points equal t times each base point, range, one-size encoding with no slack, tampering, torsion point refused, forgery succeeds with base points as generators and fails with the hashed ones. C++ `test_cross_dleq` through the C interface; `xfg-swapd` links it on Linux, macOS and Windows |
| X2 | Protocol: π in `ADAPTOR_EXCHANGE`; Bob's t from `cross_dleq_generate_scalar`; Alice verifies π and T_secp before binding; lock-type policy as in §2.2; the verification result is persisted | Engine tests: valid bundle accepted, missing or forged π refused, a hash-lock type refused for a point-capable pair, downgrade refused |
| X3 | EVM point lock in `EthChainClient`: lock, verify (amount, recipient, point address = address(T_secp), timeout runway), claim with t, t read back without the claim txid, refund; registry address per chain, runtime code-hash pin (R3) and CREATE2 deployment (R4) for `PointTimelock` | Anvil gate: point-lock happy path, wrong point refused, claim inside the runway refused, Alice reads t, refund; `PointTimelock` deployed through the deterministic deployer |
| X4 | SOL: point claim that checks t·B = T_ed on chain; client lock, verify, claim, extraction, refund; CI job with `solana-test-validator` loading the built program at its `declare_id` | Solana gate passes the same scenarios |
| X5 | BIP340 adaptor signatures: BIP340 challenge with x-only keys, even-y handling of R + T, pre-sign, verify, complete, extract | BIP340 reference vectors pass through the signer; adaptor round trip; a completed signature verifies under BIP340 |
| X6 | BTC/LTC Taproot point lock: script tree, BIP341/342 script-path sighash, claim and refund builders; Alice sends the claim pre-signature with the lock notice; Bob verifies it before claiming; Alice extracts t from the witness; full node and Bob-only SPV | Regtest gate (Bitcoin Core 28.1, Litecoin Core 0.21.4): happy path with t extracted without the claim txid, wrong pre-signature refused, refund at the timeout, no early refund |
| X7 | BCH point lock: BCH Schnorr adaptor (challenge over R.x, compressed P and the FORKID digest; R with a quadratic-residue y), P2SH script, pre-signature exchange as X6 | BCHN 29.2.0 regtest gate passes the X6 scenarios |
| X8 | Catalog: BTC, LTC, BCH from GATED to ACTIVE after X6 and X7 pass (1.8); relay executability, generated docs table, Valise generated catalog | `test_swap_pair_catalog`, docs check and Valise catalog tests pass |
| X9 | Valise: suite pin moved to the delivered suite commit; R7 keychain storage for chain keys through the pinned daemon keystore | Valise tests pass against the new pin |
| X10 | Delivery: the branch pushed to usexfg/fuego-suite and one pull request to `master` with every suite commit | PR open with CI results |

Every item gets an adversarial review pass with mutation checks (the new test must fail on the
pre-change code) before it is marked done, and a signed row in `CHANGELOG.agent.md`.

## 4. Facts the owner should weigh

- No audited ed25519/secp256k1 discrete-log equality implementation was found (searched 2026-11-10):
  Serai's cross-group proof is marked experimental and was excluded from its audit; COMIT's original is
  an archived proof of concept. π is `sigma_fun`'s proof, written by the secp256kfun author and used in
  production by xmr-btc-swap and eigenwallet; it has no formal audit either. Swaps of material value
  still warrant a review of the proof and of the wrapper here.
- `sigma_fun` leaves the choice of the two commitment generators to the caller. Passing the curves'
  base points (as the archived COMIT repository did) lets a proof for unequal logarithms verify; the
  wrapper's tests show the forgery and that it fails against the hashed generators used here.
- π carries no swap context. Replaying another swap's (T, T_secp, π) gains nothing for a party who does
  not know t, and the signed `ADAPTOR_EXCHANGE` still covers the proof bytes.
- Building `xfg-swapd` now needs a Rust toolchain (1.85 or newer) and crates.io access or a vendored
  registry on every platform.
- π is about 52 KB per swap (hex in JSON: about 105 KB); the peer transport accepts 1 MB.
- Peers on older daemons cannot complete point-locked swaps; such swaps fail closed.
- No `PointTimelock` is deployed on any chain today. Until the owner deploys it on a chain (a funded
  key, one transaction per chain through the deterministic deployer), that chain keeps the hash lock and
  F33 (owner decision: stay ACTIVE meanwhile).
- The SOL program source already holds the point claim (`lock_ptlc`, `claim_ptlc`). No mainnet deployment
  of it is known. The daemon reads the configured program's code: until a program carrying both point
  instructions is deployed under a program id the owner holds, SOL stays on the hash lock and F33.
- SOL point locks bind the escrow to the XFG adaptor point directly; the cross-curve proof is not used.
- BTC, LTC and BCH point locks add a step: Alice must be online after locking to send the claim
  pre-signature. Alice pre-signs up to four claim fees (x1, x3, x9, x27 of her estimate, capped at a
  tenth of the value). How Bob raises a stalled claim depends on each chain's default node policy:
  BTC replaces it with the next pre-signed fee; LTC cannot (Litecoin Core 0.21.4 refuses replacements
  by default), so Bob chains up to 8 children paying for it, which works only while the claim pays his
  own key; BCH cannot raise a broadcast claim at all (no replacement, and BCHN dropped
  child-pays-for-parent mining in v23.0.0), so Bob claims at the top pre-signed fee. A BCH claim still
  unconfirmed at the refund timeout races Alice's refund, which nodes reject only while they hold the
  claim (first seen), and Alice can already read t.
- DOGE, DASH, ZEC, KMD, DCR and the other hash-only chains keep F33 and stay GATED.
