# R2-1 Fix Design: Treasury Vault Authorization

**Status:** PROPOSED — **MUST LAND WITH v11.** The vault's first funding source activates
at v11: the Hearth AMM's `cdHearthFeeAccumulator`, debited 70% of the 1% taker fee on every
on-chain Hearth fill (`HEARTH_TAG_CUTOFF_HEIGHT = UPGRADE_HEIGHT_V11 = 1111111`). From the
first such fill the vault holds value, so authorization must be enforced from v11 onward.

Note the two funding paths have different activation heights, and the boundary matters for
the legacy-derivation carve-out below:

| Source | Activates | Enforced from |
|---|---|---|
| Hearth AMM `cdHearthFeeAccumulator` (70% of `HEARTH_FEE_BPS`) | v11 (1,111,111) | v11 |
| CD yield / bonus vault (from CD deposits) | `CD_ACTIVATION_HEIGHT` = v12 (2,666,666) | v12 |
| Swap-fee shares | per `SWAP_FEE_RATE_BPS` routing | v11 |

Pre-v11 blocks keep the legacy `VAULT_KEY_SEED` derivation. From v11 on, every vault spend
must carry an authorization signature — including spends of value that arrived via the v11
Hearth accumulator alone, with no CD ever deposited.
**Branch:** `codex/treasury-vault-auth`
**Finding:** R2-1 in `docs/SECURITY_AUDIT_FINDINGS.md`

## The vulnerability, precisely

`deriveVaultKeys(genesisHash)` (`src/Treasury/VaultKeys.cpp:19-45`) computes:

```
spendKey = hash_to_scalar(keccak(genesisHash ‖ "xfgo_treasury_vault_v1"))
```

`VAULT_KEY_SEED` is a string literal at `src/CryptoNoteConfig.h:306`. The genesis hash is the
public first block. **Both inputs are public, so `vaultSpendKey` is recomputable by anyone.**

The vault is not backed by on-chain coins — it is an in-memory ledger. `addUtxo`
(`VaultUtxoSet.cpp:32-54`) is called with a *fabricated* global index `(height << 32) | counter`
and `m_vaultKeys.viewPub` as the output key. No transaction output is ever created.

That makes the exposure worse than "stealable coins", because there is no signature to forge
against. The vault spend is authorized by `VaultPolicy::isPermitted` (`VaultPolicy.cpp:41-100`),
a pure content predicate:

| Partition | Permitted when |
|---|---|
| `CD_APY_POOL` | any `CommitmentSpend` declares `claimedInterest > 0` |
| `LP_RESERVE` | an AMM add/remove-liquidity tag is present |
| `GENERAL_RESERVE` | a HEAT mint-auth tag is present |
| `SWF`, `BONUS_VAULT` | never |

None of these require proving knowledge of the vault key. So the intended attack is:

1. Derive `vaultSpendKey` from genesis hash + the published seed.
2. Compute `ki = generate_key_image(vaultViewPub, vaultSpendKey)` for a funded vault output.
3. Build a `KeyInput` carrying that key image, with `outputIndexes` pointing at a ring
   containing the attacker's *own* HEAT outputs.
4. `check_tx_input` passes — a ring signature only needs one owned member; the key image is not
   bound to a specific output.
5. `isVaultKeyImage(ki)` is true, so the connect path classifies it as a vault spend.
6. Satisfy `isPermitted` with a dummy `CommitmentSpend` (`claimedInterest > 0`) or an AMM tag.
7. `m_vault.spendUtxos` debits the real vault balance; the attacker receives the HEAT.

**Impact:** unbacked HEAT minted into attacker-controlled outputs, bounded per epoch by
`m_feePoolBalance` and the partition balance. The fee pool is the sole backing for every CD
interest claim, so this drains depositor yield as well as protocol revenue.

**Prerequisite:** getting the transaction into a block. Not a free remote steal, but the only
cryptographic barrier is absent.

### Related finding — LAUNCH BLOCKER for the dev address

`src/CryptoNoteConfig.h:519` hardcodes `FUEGO_DEV_FUND_VIEW_SECRET_KEY_HEX` — a view secret key
in source, self-described as a throwaway that "MUST be replaced before mainnet launch".

> **MUST BE FIXED BEFORE v11 LAUNCH.** A CryptoNote view secret key decrypts every incoming
> payment to the address it belongs to. This one is in the repository, in every release binary
> built from it, and — unlike R2-1 — is not gated behind any activation height, because the
> alias-registration fee destination check at `Blockchain.cpp:4794-4831` reads it on every
> v10+ block that registers an alias.

**Scope of the fix (owner deferred this; recording the requirement only):**

1. Generate a real production dev-fund wallet; the current pair is a throwaway.
2. Replace `FUEGO_DEV_FUND_ADDRESS` and `FUEGO_DEV_FUND_VIEW_SECRET_KEY_HEX` with the real pair,
   keeping both constants from the same wallet (mismatched pairs fail the fee check on every
   alias registration).
3. Move the view secret out of source entirely — the fee check only needs it to *derive* the
   expected output key. Either ship it as a build-time secret, or restructure the check to
   compare against a precomputed expected-key constant instead of deriving from the secret.
4. Never commit the production view secret, in any branch.

**Why the current code needs the secret at all:** the check derives the stealth address from
each output's public tx key and compares it to the output key, proving the fee went to the dev
fund. That derivation needs `devViewSecretKey`. Restructuring it so only a derived *public*
value is committed removes the secret from the trust boundary entirely — worth considering
during the fix, but not required to close the exposure.

**Interaction with R2-1:** both are "a secret that should not be in source". They are otherwise
independent — the dev fund is a normal on-chain account, the vault is synthetic accounting. Fix
the dev address before v11; fix the vault **in the same v11 activation**.

## Why a key rotation alone is insufficient

The obvious fix — remove the hardcoded seed — does **not** close the hole. Nodes must agree on
the vault key image to validate spends, so the key must be *derivable by every node*. Any
consensus-visible derivation is public by construction. `isPermitted` would still be the only
thing standing between an attacker and the vault, and it is a content predicate that an
attacker controls.

**Conclusion: rotating the seed changes nothing.** The defect is the absence of an
authorization check, not the predictability of the key. This is why R2-1 was classified as a
protocol change rather than a patch.

## Proposed fix

Make the vault spend require a cryptographic authorization that a content predicate cannot
substitute for. Two viable shapes:

### Option A — bind the vault spend to a real on-chain output (recommended)

Stop treating the vault as unbacked accounting. When epoch work credits a vault partition,
create a real `KeyOutput` whose key is `vaultViewPub`-derived, and register the vault UTXO in
the global output index (`m_indexManager.outputs()`) rather than only in `VaultUtxoSet`.

Then a vault spend is an ordinary `KeyInput` spend: the ring signature is checked against a
real output key, and forging it requires the vault spend key — which, yes, is public. So this
option alone still fails unless combined with B.

### Option B — proof of authority distinct from the key image (required either way)

Add a vault authorization tag to the transaction extra, signed by a key that is **not**
consensus-derivable, and gate `isPermitted` on that signature:

- Generate the authority keypair offline. Commit only the public key.
- Store the private key outside the repo (and outside any release artifact).
- `VaultPolicy::isPermitted` verifies a signature over the transaction prefix under that public
  key, in addition to the existing content predicate.
- Gate the whole thing behind `BLOCK_MAJOR_VERSION_11` so history stays valid.

This restores the property the vault needs: the party that can move protocol funds is not
knowable from chain data plus source code.

**Cost:** a hard fork, plus an operational commitment to keeping the authority key offline.
`VAULT_KEY_SEED` can stay as a legacy derivation for pre-v11 blocks, exactly as the codebase
already does for pre-v11 CD claims ("Pre-v11 blocks keep the ORIGINAL cap — resync safety").

### Rejected alternatives

- **Rotate `VAULT_KEY_SEED`.** Rejected: as shown above, changes nothing.
- **Tighten `isPermitted` content rules.** Rejected: the rules are already exactly as strict as
  they can be while remaining content-based; an attacker controls all inputs to them.
- **Remove the vault.** Rejected: `CD_APY_POOL` is the sole backing for CD interest claims
  (round-1 M-1 analysis relied on the fee-pool cap). Removing it breaks every CD.
- **Rate-limit or cap the vault drain.** Rejected as a primary control: it bounds the loss
  without removing the ability to steal. Acceptable as defence-in-depth *after* B.

## Implementation sequence (on approval)

1. Add `BLOCK_MAJOR_VERSION_12` and a `TransactionExtraVaultAuth` tag carrying a signature.
2. Add the authority public key as a new genesis-bound constant; generate the pair offline first.
3. Verify the signature inside `VaultPolicy::isPermitted` before any partition is permitted.
4. Gate on block major version; leave pre-v12 blocks on the legacy path untouched.
5. Tests: forge-without-signature rejected; wrong-key rejected; tampered prefix rejected; valid
   signature accepted; pre-v12 block still validates.
6. Separately: rotate `FUEGO_DEV_FUND_*` and remove the view secret from source.

## Open questions for the owner

1. **Who holds the authority key?** A multisig? A single operator? This determines whether the
   fix is meaningful or just relocates the trust.
2. **Hard fork acceptable?** Option B requires one. Is a v12 activation already planned?
3. **Rotate the dev-fund key now?** It is a live secret in source. Rotating changes consensus
   (the alias-fee destination check) and therefore also needs an activation height.
4. **Is the vault actually funded on mainnet yet?** — No HEAT, Hearth, CD, or atomic-swap
   transaction has has ever occurred on mainnet, so the vault has never held real value.
   **However, the vault is a v11 feature** — it is funded by v11 epoch work. So the
   authorization fix must be part of the v11 activation, not deferred to v12. The
   window is: before block 1,111,111 on mainnet.

## Files this will touch

- `src/CryptoNoteConfig.h` — v12 constant, authority pubkey, remove `VAULT_KEY_SEED` usage for v12
- `src/Treasury/VaultPolicy.{h,cpp}` — signature verification
- `src/CryptoNoteCore/TransactionExtra.{h,cpp}` — new tag
- `src/CryptoNoteCore/CryptoNoteSerialization.{h,cpp}` — serialize the tag
- `src/CryptoNoteCore/Blockchain.cpp` — version gating
- `tests/UnitTests/` — new coverage
