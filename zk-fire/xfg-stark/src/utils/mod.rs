//! Utilities Module
//! 
//! This module provides utility functions for the XFG STARK project.

/// Cryptographic utilities
pub mod crypto {
    use sha2::{Sha256, Digest};
    
    /// Compute SHA-256 hash
    pub fn sha256(data: &[u8]) -> [u8; 32] {
        let mut hasher = Sha256::new();
        hasher.update(data);
        hasher.finalize().into()
    }
    
    /// Compute Merkle tree root
    pub fn merkle_root(leaves: &[Vec<u8>]) -> [u8; 32] {
        if leaves.is_empty() {
            return [0u8; 32];
        }
        
        if leaves.len() == 1 {
            return sha256(&leaves[0]);
        }
        
        let mut current_level: Vec<[u8; 32]> = leaves.iter()
            .map(|leaf| sha256(leaf))
            .collect();
        
        while current_level.len() > 1 {
            let mut next_level = Vec::new();
            
            for chunk in current_level.chunks(2) {
                let mut combined = Vec::new();
                combined.extend_from_slice(&chunk[0]);
                if chunk.len() > 1 {
                    combined.extend_from_slice(&chunk[1]);
                } else {
                    combined.extend_from_slice(&chunk[0]);
                }
                next_level.push(sha256(&combined));
            }
            
            current_level = next_level;
        }
        
        current_level[0]
    }
}

/// Mathematical utilities
pub mod math {
    /// Compute modular exponentiation
    pub fn mod_exp(base: u64, exponent: u64, modulus: u64) -> u64 {
        if modulus == 1 {
            return 0;
        }
        
        let mut result = 1;
        let mut base = base % modulus;
        let mut exp = exponent;
        
        while exp > 0 {
            if exp & 1 == 1 {
                result = (result * base) % modulus;
            }
            base = (base * base) % modulus;
            exp >>= 1;
        }
        
        result
    }
    
    /// Check if a number is prime
    pub fn is_prime(n: u64) -> bool {
        if n < 2 {
            return false;
        }
        if n == 2 {
            return true;
        }
        if n % 2 == 0 {
            return false;
        }
        
        let sqrt_n = (n as f64).sqrt() as u64;
        for i in (3..=sqrt_n).step_by(2) {
            if n % i == 0 {
                return false;
            }
        }
        
        true
    }
}

/// Serialization utilities
pub mod serialization {
    use serde::{Serialize, Deserialize};
    use bincode;
    
    /// Serialize to bytes
    pub fn to_bytes<T: Serialize>(value: &T) -> Result<Vec<u8>, bincode::Error> {
        bincode::serialize(value)
    }
    
    /// Deserialize from bytes
    pub fn from_bytes<T: for<'de> Deserialize<'de>>(bytes: &[u8]) -> Result<T, bincode::Error> {
        bincode::deserialize(bytes)
    }
}
