#![no_main]
sp1_zkvm::entrypoint!(main);

use fuego_prover_core::{
    CircuitWitness, Hash, ProofPublicValues,
    compute_checkpoint_hash, compute_merkle_root, parse_heat_commitments,
};
use tiny_keccak::{Hasher, Keccak};

/// Compute keccak256 of the raw pow_bytes of a block header.
/// Used inside the ZK circuit in place of the cn_upx2-based block_id,
/// since cryptonight is not cost-effective inside the zkVM.
/// The prover CLI must feed blocks whose previous_block_hash fields
/// were computed with this same function.
fn keccak256_block_id(pow_bytes: &[u8]) -> Hash {
    let mut k = Keccak::v256();
    k.update(pow_bytes);
    let mut out = [0u8; 32];
    k.finalize(&mut out);
    out
}

pub fn main() {
    // -----------------------------------------------------------------------
    // 1. Read the witness from SP1 stdin.
    // -----------------------------------------------------------------------
    let witness: CircuitWitness = sp1_zkvm::io::read::<CircuitWitness>();

    let blocks = &witness.blocks;
    let prev_leaves = &witness.prev_leaves;
    let public = &witness.public;

    // -----------------------------------------------------------------------
    // 2. Handle the empty-block-range edge case.
    //    If no blocks are provided the circuit still verifies that the public
    //    values are consistent with prev_leaves and zero new commitments.
    // -----------------------------------------------------------------------
    if blocks.is_empty() {
        let root = compute_merkle_root(prev_leaves);
        let cp = compute_checkpoint_hash(&root, public.height_end, prev_leaves);
        assert_eq!(root, public.new_merkle_root, "empty range: merkle root mismatch");
        assert_eq!(cp, public.new_checkpoint_hash, "empty range: checkpoint hash mismatch");
        commit_public_values(public);
        return;
    }

    // -----------------------------------------------------------------------
    // 3. PoW version gate: enforce Dandelion+ / current protocol fork.
    // -----------------------------------------------------------------------
    assert!(
        blocks[0].header.major_version >= 10,
        "version gate: major_version must be >= 10 (Dandelion+)"
    );

    // -----------------------------------------------------------------------
    // 4. Chain linkage: verify consecutive blocks link via previous_block_hash.
    //    We use keccak256(pow_bytes) as the in-circuit block_id.
    //    The prover CLI must populate previous_block_hash fields consistently
    //    with this function when constructing the witness.
    // -----------------------------------------------------------------------
    for i in 1..blocks.len() {
        let parent_id = keccak256_block_id(&blocks[i - 1].header.pow_bytes());
        assert_eq!(
            blocks[i].header.previous_block_hash,
            parent_id,
            "chain linkage failure at block index {i}: previous_block_hash mismatch"
        );
    }

    // -----------------------------------------------------------------------
    // 5. Commitment scan: collect all new commitment hashes from tx_extras.
    // -----------------------------------------------------------------------
    let mut new_hashes: Vec<Hash> = Vec::new();
    for block in blocks.iter() {
        for tx_extra in block.tx_extras.iter() {
            let commitments = parse_heat_commitments(tx_extra);
            new_hashes.extend_from_slice(&commitments);
        }
    }

    // -----------------------------------------------------------------------
    // 6. Rebuild the full leaf set and compute the new Merkle root.
    // -----------------------------------------------------------------------
    let mut all_leaves: Vec<Hash> = prev_leaves.clone();
    all_leaves.extend_from_slice(&new_hashes);

    let new_root = compute_merkle_root(&all_leaves);

    // -----------------------------------------------------------------------
    // 7. Compute the new checkpoint hash.
    // -----------------------------------------------------------------------
    let new_cp = compute_checkpoint_hash(&new_root, public.height_end, &all_leaves);

    // -----------------------------------------------------------------------
    // 8. Assertions: circuit outputs must match the declared public values.
    // -----------------------------------------------------------------------
    assert_eq!(new_root, public.new_merkle_root, "merkle root mismatch");
    assert_eq!(new_cp, public.new_checkpoint_hash, "checkpoint hash mismatch");

    // -----------------------------------------------------------------------
    // 9. Commit public values to the SP1 proof journal.
    // -----------------------------------------------------------------------
    commit_public_values(public);
}

/// Serialize ProofPublicValues in deterministic little-endian order and
/// commit the bytes to the SP1 proof journal via commit_slice.
///
/// Layout (112 bytes total):
///   prev_checkpoint_hash  [32]
///   new_checkpoint_hash   [32]
///   new_merkle_root       [32]
///   height_start          [ 4] LE u32
///   height_end            [ 4] LE u32
///   difficulty_target     [ 4] LE u32
///                              (+ 4 bytes padding to align to 8)
fn commit_public_values(pv: &ProofPublicValues) {
    let mut buf = Vec::with_capacity(108);
    buf.extend_from_slice(&pv.prev_checkpoint_hash);
    buf.extend_from_slice(&pv.new_checkpoint_hash);
    buf.extend_from_slice(&pv.new_merkle_root);
    buf.extend_from_slice(&pv.height_start.to_le_bytes());
    buf.extend_from_slice(&pv.height_end.to_le_bytes());
    buf.extend_from_slice(&pv.difficulty_target.to_le_bytes());
    sp1_zkvm::io::commit_slice(&buf);
}
