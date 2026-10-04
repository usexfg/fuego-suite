# STARK library replacement for Winterfell — evaluation

**Date:** 2026-09-30
**Question:** a suitable replacement for Winterfell for the v11 PQ shielded-pool note-spend proof.
**Status:** Plonky3 with hiding FRI is the engineering recommendation for the [v11 implementation guide](v11-pq-shielded-stark-implementation-guide.md). No revision has been frozen, vendored, audited for Fuego or benchmarked.

## Why this is not a drop-in swap

[Winterfell's README](https://github.com/facebook/winterfell/blob/main/README.md) calls it unaudited research software and states it lacks
perfect zero knowledge for secret inputs. The shielded pool needs the opposite:
it must hide which leaf was spent, the note's asset and value, and the
input-to-output mapping. That is precisely the secret-input hiding that Winterfell
does not claim. So this is a re-selection, not an upgrade.

Two requirements dominate everything else:

1. **Hiding on secret inputs** must hold, and the exact strength (statistical vs
   perfect) must be stated rather than assumed.
2. **The final on-chain verifier must be a pure STARK verifier.** Any wrapper
   using a curve SNARK (halo2/KZG/BN254) or a classical signature gate voids the
   end-to-end PQ claim for that path. This eliminates several otherwise-attractive
   options.

## Candidates

### Plonky3 (`p3-uni-stark` with `HidingFriPcs`)

- **Audit:** the [Least Authority report](https://leastauthority.com/wp-content/uploads/2024/11/Updated_071124_Polygon_Plonky3_Final_Audit_Report.pdf) examined 2024 revisions. It did not establish zero knowledge for Fuego's final stack.
- **Hiding:** the current [architecture](https://github.com/Plonky3/Plonky3/blob/main/docs/architecture.md) explicitly pairs `p3-uni-stark` with `p3-fri::HidingFriPcs` for its ZK path. Ordinary `TwoAdicFriPcs` is non-hiding. The architecture also warns that a hiding PCS does not establish end-to-end ZK for the application.
- **Robustness:** the [2024 audit](https://leastauthority.com/wp-content/uploads/2024/11/Updated_071124_Polygon_Plonky3_Final_Audit_Report.pdf) marked verifier panic and proof-shape findings partly resolved. For a consensus node accepting proofs from hostile peers, reproducible rejection without process crashes is a release gate.
- **Known audit findings relevant to us:** missing verifier sanity checks on proof
  parameters; incomplete Fiat-Shamir initialisation; FRI verifier buffer-overflow
  robustness; assertions used where errors were recommended. Several are marked
  only *partially* resolved. Note also the Multi-FRI collinearity check deviation
  the auditors could not fully reason about.
- **Verdict:** best direct base for Fuego's dedicated note AIR. Freeze and audit the exact revision and full composition before any consensus use. Review [published advisories](https://github.com/Plonky3/Plonky3/security/advisories) and [caller obligations](https://github.com/Plonky3/Plonky3/blob/main/docs/caller-obligations.md).

### `openvm-org/stark-backend`

- **Status:** the [current README](https://github.com/openvm-org/stark-backend) recommends v2.0.0+ and says its SWIRL backend received an external zkSecurity audit. The [protocol document](https://github.com/openvm-org/stark-backend/blob/main/docs/stark-backend.md) describes multi-chip AIRs, sumchecks and WHIR openings.
- **Hiding gap:** those documents do not establish private-witness ZK for this exact composition. A production recommendation for sound computation proofs is not a private-note proof guarantee.
- **Verdict:** useful comparison if Fuego needs multi-chip interactions; do not select it for the shielded pool without a documented and audited hiding path.

### Stwo (StarkWare)

- **Status:** genuinely production. It runs inside SHARP, StarkWare's shared
  prover, securing Starknet, with real proofs verified at scale.
- **Field:** Circle STARK over M31, `no_std` verifier, SIMD prover.
- **Blockers for us:**
  - Its [README](https://github.com/starkware-libs/stwo/blob/dev/README.md) does not document a private-witness hiding configuration, and says external audits will be linked when published.
  - Soundness is **conjectured** and its parameters must be chosen by the consumer.
  - It is a prover framework, not a gadget library, so we would build more of the
    stack ourselves.
- **Verdict:** production use for computation proofs does not answer Fuego's witness-hiding requirement.

### SP1 — already in this repository

`digm-platform/fuego-core/fuego-prover/fuego-circuit/Cargo.toml` depends on
`sp1-zkvm = "3"`, and `fuego-prover-cli` depends on `sp1-sdk = "3"`. The [SP1 advisory](https://github.com/succinctlabs/sp1/security/advisories/GHSA-c873-wfhp-wx5m) identifies versions before 4.0.0 as affected by verifier soundness defects and says the old verifiers are being deprecated. The local circuit builds a HEAT commitment checkpoint from block data; it does not prove private note membership, spend authority, nullifiers or asset conservation. The existing design also describes a Groth16/Plonk final proof.

SP1 can be reconsidered at a supported version if its STARK-only proof and native verifier are selected explicitly and the private note program is written and audited. The existing version-3 checkpoint code is not a shielded-pool implementation.

## Recommendation

1. **Use `p3-uni-stark` with `p3-fri::HidingFriPcs` as the engineering base** for the v11 note AIR; select a supported revision only after checking all advisories.
2. **Treat these as blocking gates, not follow-ups:**
   - a written, cited argument for the hiding property actually used, at the
     strength claimed;
   - a panic-freedom argument for the verifier under adversarial proof bytes, with
     the residual Least Authority findings closed or explicitly accepted;
   - independent verification that the on-chain verifier contains no curve
     operation.
3. **Do not adopt a wrapper that terminates in halo2/KZG or Groth16**, and do not accept a
   fixed recursive-proof size as a design input. Aggregate only if its final
   verifier passes the same review.
4. **Keep RISC Zero as a fallback** if the dedicated AIR cannot pass the audit and resource gates. Its [README](https://github.com/risc0/risc0/blob/main/README.md) documents ZK and a native STARK path, but its default 98-bit conjectured security target and optional Groth16 layer require separate parameter and verifier decisions.

## What still has to happen

This is a candidate selection, not a mainnet-ready verifier. Before any consensus code is activated:

- Fix the exact circuit: note opening, Merkle membership, owner/nullifier, value
  ranges, per-asset conservation, maturity, action binding (section 4 of the
  [shielded STARK guide](v11-pq-shielded-stark-implementation-guide.md)).
- Choose the field and hash, and state the collision and preimage targets **under
  quantum attack**, not just classically.
- Benchmark proving on the actual Valise target phones and desktops and
  verification on supported nodes. Derive block limits and fee policy from measured
  worst cases, including adversarial proofs.
- Obtain independent cryptographic sign-off. Winterfell's unaudited status is the
  reason this search exists; replacing it with a different unaudited assumption
  repeats the original mistake.
