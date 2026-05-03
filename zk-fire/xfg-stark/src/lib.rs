//! XFG STARK Proof Implementation
//! 
//! This crate provides a comprehensive implementation of STARK (Scalable Transparent Argument of Knowledge)
//! proofs for cryptographic security.
//! 
//! ## Core Components
//! 
//! - **Field Arithmetic**: Type-safe field element operations
//! - **Polynomial Operations**: Efficient polynomial arithmetic and evaluation
//! - **STARK Proof System**: Complete STARK proof generation and verification
//! - **Type System**: Comprehensive type definitions for all cryptographic operations


#![cfg_attr(not(feature = "std"), no_std)]
#![cfg_attr(feature = "constant_time", feature(const_fn_floating_point_arithmetic))]
#![deny(missing_docs)]
#![deny(unsafe_code)]
#![warn(clippy::all)]
#![warn(clippy::pedantic)]

pub mod field;
pub mod polynomial;
pub mod stark;
pub mod types;
pub mod utils;
pub mod air;
pub mod proof;
pub mod winterfell_integration;
pub mod benchmarks;
pub mod burn_mint_air;
pub mod burn_mint_prover;
pub mod burn_mint_verifier;
pub mod proof_data_schema;
pub mod test_data_generator;
/// Fuego daemon RPC client for querying commitments, merkle proofs, and consensus data
pub mod fuego_rpc;


pub use field::*;
pub use polynomial::*;
pub use stark::*;
pub use types::*;
pub use utils::*;
pub use air::*;
pub use proof::*;
pub use winterfell_integration::*;
pub use benchmarks::*;
pub use burn_mint_air::*;
pub use burn_mint_prover::*;
pub use burn_mint_verifier::*;
pub use proof_data_schema::*;
pub use test_data_generator::*;
pub use fuego_rpc::*;


/// Re-exports for common cryptographic operations
pub mod crypto {
    pub use winter_crypto::*;
    pub use winter_math::*;
}

/// Re-exports for Winterfell framework integration
pub mod winterfell {
    pub use winterfell::*;
}

/// Error types for the XFG STARK implementation
#[derive(Debug, thiserror::Error)]
pub enum XfgStarkError {
    /// Field arithmetic error
    #[error("Field arithmetic error: {0}")]
    FieldError(#[from] field::FieldError),
    
    /// Polynomial operation error
    #[error("Polynomial error: {0}")]
    PolynomialError(#[from] polynomial::PolynomialError),
    
    /// STARK proof error
    #[error("STARK proof error: {0}")]
    StarkError(#[from] stark::StarkError),
    
    /// Type system error
    #[error("Type error: {0}")]
    TypeError(#[from] types::TypeError),
    
    /// Serialization error
    #[error("Serialization error: {0}")]
    SerializationError(#[from] bincode::Error),
    
    /// JSON serialization error
    #[error("JSON serialization error: {0}")]
    JsonError(#[from] serde_json::Error),
    
    /// I/O error
    #[error("I/O error: {0}")]
    IoError(#[from] std::io::Error),
    
    /// Parse error
    #[error("Parse error: {0}")]
    ParseError(String),
    
    /// Anyhow error
    #[error("General error: {0}")]
    AnyhowError(#[from] anyhow::Error),
    
    /// Boxed error
    #[error("Boxed error: {0}")]
    BoxError(#[from] Box<dyn std::error::Error>),
    
    /// Cryptographic error
    #[error("Cryptographic error: {0}")]
    CryptoError(String),
}

/// Result type for XFG STARK operations
pub type Result<T> = std::result::Result<T, XfgStarkError>;

/// XFG STARK version information
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// XFG STARK authors information
pub const AUTHORS: &str = env!("CARGO_PKG_AUTHORS");

/// XFG STARK description information
pub const DESCRIPTION: &str = env!("CARGO_PKG_DESCRIPTION");

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version_info() {
        assert!(!VERSION.is_empty());
        assert!(!AUTHORS.is_empty());
        assert!(!DESCRIPTION.is_empty());
    }

    #[test]
    fn test_network_id_hashing() {
        use sha3::{Digest, Keccak256};
        use hex;
        
        // Test the Fuego network ID hashing
        let fuego_network_id = "93385046440755750514194170694064996624";
        let mut hasher = Keccak256::new();
        hasher.update(fuego_network_id.as_bytes());
        let result = hasher.finalize();
        let hash = format!("0x{:x}", result);
        
        // Verify the hash is correct (this is the expected hash from our example)
        assert_eq!(hash, "0x6430829be74c2d9892a5122aa2f2daac3ee9850f086a8985941e7fb4bde60fcf");
        
        // Test conversion to field element
        let clean_hash = hash.trim_start_matches("0x");
        let bytes = hex::decode(clean_hash).unwrap();
        let mut network_id_bytes = [0u8; 8];
        network_id_bytes.copy_from_slice(&bytes[..8]);
        
        let network_id_u64 = u64::from_le_bytes(network_id_bytes);
        let network_id_field = types::field::PrimeField64::new(network_id_u64);
        
        // Verify the field element conversion
        assert_eq!(network_id_field.to_string(), "PrimeField64(1742133188492406885)");
    }
}
