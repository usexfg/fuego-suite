use serde::{Deserialize, Serialize};
use tiny_keccak::{Hasher, Keccak};

// Mirrors C++ Crypto::Hash (32 bytes)
pub type Hash = [u8; 32];

// Mirrors C++ BlockHeader
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BlockHeader {
    pub major_version: u8,
    pub minor_version: u8,
    pub nonce: u32,
    pub timestamp: u64,
    pub previous_block_hash: Hash,
}

impl BlockHeader {
    /// Canonical serialization for PoW hashing (matches C++ serialization order)
    pub fn pow_bytes(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(43);
        out.push(self.major_version);
        out.push(self.minor_version);
        out.extend_from_slice(&self.timestamp.to_le_bytes());
        out.extend_from_slice(&self.previous_block_hash);
        out.extend_from_slice(&self.nonce.to_le_bytes());
        out
    }
}

// Mirrors C++ CommitmentEntry::Type
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
pub enum CommitmentType {
    Heat = 0,
    Cold = 1,
}

// Mirrors C++ CommitmentEntry (fields relevant to ZK circuit)
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CommitmentEntry {
    pub commitment: Hash,
    pub tx_hash: Hash,
    pub block_height: u32,
    pub amount: u64,
    pub term: u32,
    pub entry_type: CommitmentType,
    pub target_chain_id: u32,
}

// Public inputs committed to by the SP1 proof
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProofPublicValues {
    pub prev_checkpoint_hash: Hash,
    pub new_checkpoint_hash: Hash,
    pub new_merkle_root: Hash,
    pub height_start: u32,
    pub height_end: u32,
    pub difficulty_target: u32,
}

// Wire type for RPC block_range response
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RpcBlock {
    pub header: BlockHeader,
    /// Raw tx_extra bytes per transaction
    pub tx_extras: Vec<Vec<u8>>,
}

// Witness data fed to the circuit via SP1 stdin
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CircuitWitness {
    pub blocks: Vec<RpcBlock>,
    /// All commitment hashes already in the index before height_start (ordered)
    pub prev_leaves: Vec<Hash>,
    pub public: ProofPublicValues,
}

pub const TX_EXTRA_HEAT_TAG: u8 = 0x08;
pub const DEPOSIT_TERM_FOREVER: u32 = 0xFFFF_FFFF;

/// Parse HEAT commitment hashes out of raw tx_extra bytes.
/// Tag format: 0x08 || commitment[32]
pub fn parse_heat_commitments(tx_extra: &[u8]) -> Vec<Hash> {
    let mut out = Vec::new();
    let mut i = 0;
    while i < tx_extra.len() {
        let tag = tx_extra[i];
        i += 1;
        match tag {
            TX_EXTRA_HEAT_TAG => {
                if i + 32 <= tx_extra.len() {
                    let mut commitment = [0u8; 32];
                    commitment.copy_from_slice(&tx_extra[i..i + 32]);
                    out.push(commitment);
                    i += 32;
                }
            }
            // Skip other known tags; for unknown tags we stop (conservative)
            _ => break,
        }
    }
    out
}

/// Compute merkle root from an ordered list of leaves using keccak256.
/// Matches C++ CommitmentIndex::computeMerkleRoot():
///   - internal nodes: keccak256(left || right)
///   - odd leaf: keccak256(leaf || leaf)
pub fn compute_merkle_root(leaves: &[Hash]) -> Hash {
    if leaves.is_empty() {
        return [0u8; 32];
    }
    if leaves.len() == 1 {
        return leaves[0];
    }
    let mut level: Vec<Hash> = leaves.to_vec();
    while level.len() > 1 {
        let mut next = Vec::with_capacity((level.len() + 1) / 2);
        let mut i = 0;
        while i < level.len() {
            let left = level[i];
            let right = if i + 1 < level.len() { level[i + 1] } else { left };
            next.push(keccak256_pair(&left, &right));
            i += 2;
        }
        level = next;
    }
    level[0]
}

fn keccak256_pair(left: &Hash, right: &Hash) -> Hash {
    let mut buf = [0u8; 64];
    buf[..32].copy_from_slice(left);
    buf[32..].copy_from_slice(right);
    let mut k = Keccak::v256();
    k.update(&buf);
    let mut out = [0u8; 32];
    k.finalize(&mut out);
    out
}

/// Compute checkpoint hash:
///   keccak256(merkle_root || height_end_le32 || keccak256(leaves_concat))
pub fn compute_checkpoint_hash(merkle_root: &Hash, height_end: u32, leaves: &[Hash]) -> Hash {
    // Hash all leaves concatenated
    let mut k = Keccak::v256();
    for leaf in leaves {
        k.update(leaf);
    }
    let mut leaves_hash = [0u8; 32];
    k.finalize(&mut leaves_hash);

    // Final hash
    let mut k2 = Keccak::v256();
    k2.update(merkle_root);
    k2.update(&height_end.to_le_bytes());
    k2.update(&leaves_hash);
    let mut out = [0u8; 32];
    k2.finalize(&mut out);
    out
}

/// Verify a merkle proof for a leaf at leaf_index against a known root.
/// proof is the ordered list of sibling hashes from leaf to root.
pub fn verify_merkle_proof(leaf: &Hash, proof: &[Hash], leaf_index: usize, root: &Hash) -> bool {
    let mut current = *leaf;
    let mut index = leaf_index;
    for sibling in proof {
        current = if index % 2 == 0 {
            keccak256_pair(&current, sibling)
        } else {
            keccak256_pair(sibling, &current)
        };
        index /= 2;
    }
    &current == root
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn merkle_single_leaf() {
        let leaf = [1u8; 32];
        assert_eq!(compute_merkle_root(&[leaf]), leaf);
    }

    #[test]
    fn merkle_two_leaves_roundtrip() {
        let a = [1u8; 32];
        let b = [2u8; 32];
        let root = compute_merkle_root(&[a, b]);
        assert!(verify_merkle_proof(&a, &[b], 0, &root));
        assert!(verify_merkle_proof(&b, &[a], 1, &root));
    }

    #[test]
    fn parse_heat_commitments_basic() {
        let commitment = [0xABu8; 32];
        let mut extra = vec![TX_EXTRA_HEAT_TAG];
        extra.extend_from_slice(&commitment);
        let parsed = parse_heat_commitments(&extra);
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0], commitment);
    }
}
