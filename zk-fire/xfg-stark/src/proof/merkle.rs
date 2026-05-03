//! Merkle Tree Implementation
//! 
//! This module provides a complete Merkle tree system with cryptographic security,
//! efficient tree construction, and secure inclusion proofs for STARK proofs.
//! 
//! ## Features
//! 
//! - **Cryptographic Hashing**: SHA256-based hash functions for security
//! - **Efficient Tree Construction**: Optimized tree building algorithms
//! - **Inclusion Proofs**: Secure proof generation and verification
//! - **Batch Operations**: Efficient batch proof generation
//! - **Memory Optimization**: Minimal memory footprint for large trees

use crate::types::FieldElement;
use std::fmt::{Display, Formatter};
use sha2::{Sha256, Digest};

/// Merkle tree node
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MerkleNode {
    /// Node hash
    pub hash: [u8; 32],
    /// Node level in the tree
    pub level: usize,
    /// Node index at this level
    pub index: usize,
}

impl MerkleNode {
    /// Create a new Merkle node
    pub fn new(hash: [u8; 32], level: usize, index: usize) -> Self {
        Self { hash, level, index }
    }

    /// Create a leaf node
    pub fn leaf(data: &[u8]) -> Self {
        let hash = Self::hash_data(data);
        Self::new(hash, 0, 0)
    }

    /// Create an internal node from two children
    pub fn internal(left: &MerkleNode, right: &MerkleNode) -> Self {
        let mut hasher = Sha256::new();
        hasher.update(&left.hash);
        hasher.update(&right.hash);
        let hash = hasher.finalize().into();
        
        Self::new(hash, left.level + 1, left.index / 2)
    }

    /// Hash data using SHA256
    fn hash_data(data: &[u8]) -> [u8; 32] {
        let mut hasher = Sha256::new();
        hasher.update(data);
        hasher.finalize().into()
    }
}

impl Display for MerkleNode {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "MerkleNode(level={}, index={}, hash={:02x?})",
            self.level, self.index, &self.hash[..8]
        )
    }
}

/// Merkle tree implementation
#[derive(Debug, Clone)]
pub struct MerkleTree {
    /// Tree root
    pub root: MerkleNode,
    /// Tree depth
    pub depth: usize,
    /// Number of leaves
    pub num_leaves: usize,
    /// Tree nodes (for efficient proof generation)
    nodes: Vec<Vec<MerkleNode>>,
}

impl MerkleTree {
    /// Create a new Merkle tree from leaf data
    pub fn new(leaves: &[Vec<u8>]) -> Result<Self, MerkleError> {
        if leaves.is_empty() {
            return Err(MerkleError::EmptyLeaves);
        }

        let num_leaves = leaves.len();
        let depth = Self::calculate_depth(num_leaves);
        let mut nodes = Vec::with_capacity(depth + 1);

        // Create leaf nodes
        let mut current_level: Vec<MerkleNode> = leaves
            .iter()
            .enumerate()
            .map(|(i, data)| {
                let mut node = MerkleNode::leaf(data);
                node.index = i;
                node
            })
            .collect();

        nodes.push(current_level.clone());

        // Build tree levels bottom-up
        for level in 0..depth {
            let next_level = Self::build_level(&current_level, level + 1)?;
            nodes.push(next_level.clone());
            current_level = next_level;
        }

        let root = current_level[0].clone();

        Ok(Self {
            root,
            depth,
            num_leaves,
            nodes,
        })
    }

    /// Calculate tree depth from number of leaves
    fn calculate_depth(num_leaves: usize) -> usize {
        if num_leaves <= 1 {
            return 0;
        }
        
        let mut depth = 0;
        let mut leaves = num_leaves;
        
        while leaves > 1 {
            leaves = (leaves + 1) / 2; // Ceiling division
            depth += 1;
        }
        
        depth
    }

    /// Build a level of the tree from the previous level
    fn build_level(prev_level: &[MerkleNode], level: usize) -> Result<Vec<MerkleNode>, MerkleError> {
        let mut current_level = Vec::new();
        
        for i in (0..prev_level.len()).step_by(2) {
            let left = &prev_level[i];
            let right = if i + 1 < prev_level.len() {
                &prev_level[i + 1]
            } else {
                // Duplicate the last node if odd number
                &prev_level[i]
            };
            
            let mut node = MerkleNode::internal(left, right);
            node.index = i / 2;
            current_level.push(node);
        }
        
        Ok(current_level)
    }

    /// Generate inclusion proof for a leaf
    pub fn generate_proof(&self, leaf_index: usize) -> Result<MerkleProof, MerkleError> {
        if leaf_index >= self.num_leaves {
            return Err(MerkleError::InvalidLeafIndex(leaf_index));
        }

        let mut proof = MerkleProof {
            leaf_index,
            siblings: Vec::new(),
            path: Vec::new(),
        };

        let mut current_index = leaf_index;
        
        for level in 0..self.depth {
            let level_nodes = &self.nodes[level];
            let sibling_index = if current_index % 2 == 0 {
                current_index + 1
            } else {
                current_index - 1
            };

            if sibling_index < level_nodes.len() {
                proof.siblings.push(level_nodes[sibling_index].hash);
                proof.path.push(current_index % 2 == 0);
            }

            current_index /= 2;
        }

        Ok(proof)
    }

    /// Generate batch inclusion proofs
    pub fn generate_batch_proofs(&self, leaf_indices: &[usize]) -> Result<Vec<MerkleProof>, MerkleError> {
        let mut proofs = Vec::new();
        
        for &index in leaf_indices {
            let proof = self.generate_proof(index)?;
            proofs.push(proof);
        }
        
        Ok(proofs)
    }

    /// Verify inclusion proof
    pub fn verify_proof(&self, leaf_data: &[u8], proof: &MerkleProof) -> Result<bool, MerkleError> {
        if proof.leaf_index >= self.num_leaves {
            return Ok(false);
        }

        // Start with leaf hash
        let mut current_hash = MerkleNode::hash_data(leaf_data);
        
        // Follow the proof path
        for (i, &is_left) in proof.path.iter().enumerate() {
            if i >= proof.siblings.len() {
                return Ok(false);
            }
            
            let sibling_hash = proof.siblings[i];
            
            // Combine with sibling based on path
            let mut hasher = Sha256::new();
            if is_left {
                hasher.update(&current_hash);
                hasher.update(&sibling_hash);
            } else {
                hasher.update(&sibling_hash);
                hasher.update(&current_hash);
            }
            
            current_hash = hasher.finalize().into();
        }
        
        // Check if we reach the root
        Ok(current_hash == self.root.hash)
    }

    /// Get root hash
    pub fn root_hash(&self) -> [u8; 32] {
        self.root.hash
    }

    /// Get tree statistics
    pub fn stats(&self) -> MerkleStats {
        MerkleStats {
            depth: self.depth,
            num_leaves: self.num_leaves,
            total_nodes: self.nodes.iter().map(|level| level.len()).sum(),
        }
    }
}

impl Display for MerkleTree {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "MerkleTree(depth={}, leaves={}, root={:02x?})",
            self.depth, self.num_leaves, &self.root.hash[..8]
        )
    }
}

/// Merkle inclusion proof
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MerkleProof {
    /// Leaf index
    pub leaf_index: usize,
    /// Sibling hashes along the path
    pub siblings: Vec<[u8; 32]>,
    /// Path direction (true = left, false = right)
    pub path: Vec<bool>,
}

impl MerkleProof {
    /// Create a new proof
    pub fn new(leaf_index: usize) -> Self {
        Self {
            leaf_index,
            siblings: Vec::new(),
            path: Vec::new(),
        }
    }

    /// Add a sibling hash
    pub fn add_sibling(&mut self, sibling_hash: [u8; 32], is_left: bool) {
        self.siblings.push(sibling_hash);
        self.path.push(is_left);
    }

    /// Verify proof against a root hash
    pub fn verify(&self, leaf_data: &[u8], root_hash: [u8; 32]) -> bool {
        let mut current_hash = MerkleNode::hash_data(leaf_data);
        
        for (i, &is_left) in self.path.iter().enumerate() {
            if i >= self.siblings.len() {
                return false;
            }
            
            let sibling_hash = self.siblings[i];
            
            let mut hasher = Sha256::new();
            if is_left {
                hasher.update(&current_hash);
                hasher.update(&sibling_hash);
            } else {
                hasher.update(&sibling_hash);
                hasher.update(&current_hash);
            }
            
            current_hash = hasher.finalize().into();
        }
        
        current_hash == root_hash
    }

    /// Get proof size in bytes
    pub fn size(&self) -> usize {
        self.siblings.len() * 32 + self.path.len() + std::mem::size_of::<usize>()
    }
}

impl Display for MerkleProof {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "MerkleProof(leaf={}, siblings={}, path_len={})",
            self.leaf_index, self.siblings.len(), self.path.len()
        )
    }
}

/// Merkle tree statistics
#[derive(Debug, Clone)]
pub struct MerkleStats {
    /// Tree depth
    pub depth: usize,
    /// Number of leaves
    pub num_leaves: usize,
    /// Total number of nodes
    pub total_nodes: usize,
}

impl Display for MerkleStats {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "MerkleStats(depth={}, leaves={}, nodes={})",
            self.depth, self.num_leaves, self.total_nodes
        )
    }
}

/// Merkle tree error types
#[derive(Debug, thiserror::Error)]
pub enum MerkleError {
    /// Empty leaves
    #[error("Empty leaves")]
    EmptyLeaves,

    /// Invalid leaf index
    #[error("Invalid leaf index: {0}")]
    InvalidLeafIndex(usize),

    /// Invalid proof
    #[error("Invalid proof")]
    InvalidProof,

    /// Hash computation error
    #[error("Hash computation error: {0}")]
    HashError(String),

    /// Tree construction error
    #[error("Tree construction error: {0}")]
    ConstructionError(String),
}

/// Generate Merkle commitment for field elements
pub fn generate_commitment<F: FieldElement>(data: &[F]) -> Vec<u8> {
    let mut hasher = Sha256::new();
    
    for element in data {
        let bytes = element.to_bytes();
        hasher.update(&bytes);
    }
    
    hasher.finalize().to_vec()
}

/// Verify Merkle inclusion proof
pub fn verify_inclusion_proof<F: FieldElement>(
    root: &[u8],
    proof: &MerkleProof,
    leaf_data: &[F],
) -> bool {
    let leaf_bytes: Vec<u8> = leaf_data.iter()
        .flat_map(|element| element.to_bytes())
        .collect();
    
    proof.verify(&leaf_bytes, root.try_into().unwrap_or([0; 32]))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::field::PrimeField64;

    #[test]
    fn test_merkle_tree_creation() {
        let leaves = vec![
            b"leaf1".to_vec(),
            b"leaf2".to_vec(),
            b"leaf3".to_vec(),
            b"leaf4".to_vec(),
        ];

        let tree = MerkleTree::new(&leaves);
        assert!(tree.is_ok());
        
        let tree = tree.unwrap();
        assert_eq!(tree.num_leaves, 4);
        assert_eq!(tree.depth, 2);
    }

    #[test]
    fn test_merkle_proof_generation() {
        let leaves = vec![
            b"leaf1".to_vec(),
            b"leaf2".to_vec(),
            b"leaf3".to_vec(),
            b"leaf4".to_vec(),
        ];

        let tree = MerkleTree::new(&leaves).unwrap();
        let proof = tree.generate_proof(0);
        assert!(proof.is_ok());
    }

    #[test]
    fn test_merkle_proof_verification() {
        let leaves = vec![
            b"leaf1".to_vec(),
            b"leaf2".to_vec(),
            b"leaf3".to_vec(),
            b"leaf4".to_vec(),
        ];

        let tree = MerkleTree::new(&leaves).unwrap();
        let proof = tree.generate_proof(0).unwrap();
        
        let result = tree.verify_proof(b"leaf1", &proof);
        assert!(result.is_ok());
        assert!(result.unwrap());
    }

    #[test]
    fn test_field_element_commitment() {
        let elements = vec![
            PrimeField64::new(1),
            PrimeField64::new(2),
            PrimeField64::new(3),
        ];

        let commitment = generate_commitment(&elements);
        assert_eq!(commitment.len(), 32);
    }

    #[test]
    fn test_batch_proof_generation() {
        let leaves = vec![
            b"leaf1".to_vec(),
            b"leaf2".to_vec(),
            b"leaf3".to_vec(),
            b"leaf4".to_vec(),
        ];

        let tree = MerkleTree::new(&leaves).unwrap();
        let indices = vec![0, 2];
        
        let proofs = tree.generate_batch_proofs(&indices);
        assert!(proofs.is_ok());
        assert_eq!(proofs.unwrap().len(), 2);
    }
}