//! XFG Burn & Mint Prover (v3 unified relay format)
//!
//! Generates STARK proofs for HEAT burns and COLD deposits using the unified v3
//! commitment format that matches Fuego C++ `StarkCommitmentGenerator`.
//!
//! v3 changes from v1/v2:
//! - No tx_prefix_hash in commitment preimage (circular dependency resolved)
//! - No recipient in commitment (contract mints to msg.sender)
//! - 4 tiers: 0.8, 8, 80, 800 XFG
//! - Unified HEAT + COLD via deposit_term field
//! - tx_hash as separate public input for on-chain binding

use crate::{
    burn_mint_air::{make_public_inputs, BurnMintPublicInputs, XfgBurnMintAir, DEPOSIT_TERM_FOREVER},
    Result,
};
use winterfell::{math::fields::f64::BaseElement, ProofOptions, Prover, StarkProof, TraceInfo};

/// XFG Burn & Mint Prover using Winterfell
pub struct XfgBurnMintProver {
    security_parameter: usize,
    proof_options: ProofOptions,
}

impl XfgBurnMintProver {
    /// Create new prover with default proof options
    pub fn new(security_parameter: usize) -> Self {
        let proof_options = ProofOptions::new(
            42,                               // blowup factor
            8,                                // grinding factor
            4,                                // hash function
            winterfell::FieldExtension::None, // field extension
            8,                                // FRI folding factor
            31,                               // FRI remainder max degree
        );

        Self {
            security_parameter,
            proof_options,
        }
    }

    /// Create prover with custom proof options
    pub fn with_options(security_parameter: usize, proof_options: ProofOptions) -> Self {
        Self {
            security_parameter,
            proof_options,
        }
    }

    /// Prove XFG burn or COLD deposit (v3 unified format)
    ///
    /// Generates a STARK proof that validates:
    /// - Burn/deposit amount is a valid tier (0.8, 8, 80, 800 XFG)
    /// - Mint amount equals burn amount (1:1 atomic units)
    /// - Commitment = keccak256(secret || amount || network || chain || version || term)
    /// - Nullifier = keccak256(secret || "nullifier" || amount)
    /// - State machine: init → burn → mint → complete
    /// - Network/chain IDs prevent cross-chain replay
    pub fn prove_burn_mint(
        &self,
        burn_amount: u64,
        mint_amount: u64,
        txn_hash: u32,           // First 4 bytes of on-chain tx hash (LE)
        secret: &[u8],           // 32-byte secret (user's claim ticket)
        network_id: u32,         // 1=mainnet, 2=testnet
        target_chain_id: u32,    // 1=ETH, 42161=ARB
        commitment_version: u32, // 3 = v3 unified
        deposit_term: u32,       // DEPOSIT_TERM_FOREVER for HEAT, actual blocks for COLD
    ) -> Result<StarkProof> {
        self.validate_inputs(burn_amount, mint_amount, txn_hash, commitment_version)?;

        let secret_element = self.secret_to_field_element(secret)?;

        let public_inputs = make_public_inputs(
            burn_amount as u32,
            txn_hash,
            network_id,
            target_chain_id,
            commitment_version,
            deposit_term,
        );

        let trace_info = TraceInfo::new(7, 64);

        let air = XfgBurnMintAir::new_with_secret(
            trace_info,
            public_inputs,
            secret_element,
            self.proof_options.clone(),
        );

        let trace = air.build_trace();

        let proof = air
            .prove(trace)
            .map_err(|e| crate::XfgStarkError::CryptoError(format!("Prover error: {:?}", e)))?;

        Ok(proof)
    }

    /// Validate input parameters
    /// v1: 2 tiers (0.8, 800), v2: 3 tiers (0.8, 80, 800), v3: 4 tiers (0.8, 8, 80, 800)
    fn validate_inputs(
        &self,
        burn_amount: u64,
        mint_amount: u64,
        txn_hash: u32,
        commitment_version: u32,
    ) -> Result<()> {
        let valid_amounts: Vec<u64> = match commitment_version {
            1 => vec![8_000_000, 8_000_000_000],                              // v1: 0.8, 800
            2 => vec![8_000_000, 800_000_000, 8_000_000_000],                 // v2: 0.8, 80, 800
            3 => vec![8_000_000, 80_000_000, 800_000_000, 8_000_000_000],     // v3: 0.8, 8, 80, 800
            _ => {
                return Err(crate::XfgStarkError::CryptoError(
                    format!("Unsupported commitment version: {}", commitment_version),
                ));
            }
        };

        if !valid_amounts.contains(&burn_amount) {
            return Err(crate::XfgStarkError::CryptoError(
                format!("v{}: Invalid burn amount {}. Valid: {:?}",
                    commitment_version, burn_amount, valid_amounts),
            ));
        }

        if mint_amount != burn_amount {
            return Err(crate::XfgStarkError::CryptoError(
                format!("Mint amount {} must equal burn amount {}", mint_amount, burn_amount),
            ));
        }

        if txn_hash == 0 {
            return Err(crate::XfgStarkError::CryptoError(
                "Transaction hash must be non-zero".to_string(),
            ));
        }

        Ok(())
    }

    /// Convert secret bytes to field element (uses first 4 bytes as u32 LE)
    fn secret_to_field_element(&self, secret: &[u8]) -> Result<BaseElement> {
        if secret.len() < 4 {
            return Err(crate::XfgStarkError::CryptoError(
                "Secret must be at least 4 bytes".to_string(),
            ));
        }
        let value = u32::from_le_bytes([secret[0], secret[1], secret[2], secret[3]]);
        Ok(BaseElement::from(value))
    }

    pub fn xfg_to_atomic_units(xfg_amount: f64) -> u64 {
        (xfg_amount * 10_000_000.0) as u64
    }

    pub fn atomic_units_to_xfg(atomic_units: u64) -> f64 {
        atomic_units as f64 / 10_000_000.0
    }

    pub fn get_proof_size(&self, proof: &StarkProof) -> usize {
        proof.to_bytes().len()
    }

    pub fn security_parameter(&self) -> usize {
        self.security_parameter
    }

    pub fn proof_options(&self) -> &ProofOptions {
        &self.proof_options
    }
}

impl Default for XfgBurnMintProver {
    fn default() -> Self {
        Self::new(128)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_prover_creation() {
        let prover = XfgBurnMintProver::new(128);
        assert_eq!(prover.security_parameter(), 128);
    }

    #[test]
    fn test_v3_input_validation() {
        let prover = XfgBurnMintProver::new(128);

        // All 4 v3 tiers valid
        assert!(prover.validate_inputs(8_000_000, 8_000_000, 0xDEAD, 3).is_ok());       // 0.8 XFG
        assert!(prover.validate_inputs(80_000_000, 80_000_000, 0xDEAD, 3).is_ok());      // 8 XFG
        assert!(prover.validate_inputs(800_000_000, 800_000_000, 0xDEAD, 3).is_ok());    // 80 XFG
        assert!(prover.validate_inputs(8_000_000_000, 8_000_000_000, 0xDEAD, 3).is_ok()); // 800 XFG

        // Invalid amount
        assert!(prover.validate_inputs(1_000_000, 1_000_000, 0xDEAD, 3).is_err());

        // Mint != Burn
        assert!(prover.validate_inputs(8_000_000, 16_000_000, 0xDEAD, 3).is_err());

        // Zero tx hash
        assert!(prover.validate_inputs(8_000_000, 8_000_000, 0, 3).is_err());

        // Unsupported version
        assert!(prover.validate_inputs(8_000_000, 8_000_000, 0xDEAD, 99).is_err());
    }

    #[test]
    fn test_v1_v2_backward_compat() {
        let prover = XfgBurnMintProver::new(128);

        // v1: only 0.8 and 800
        assert!(prover.validate_inputs(8_000_000, 8_000_000, 0xAA, 1).is_ok());
        assert!(prover.validate_inputs(8_000_000_000, 8_000_000_000, 0xAA, 1).is_ok());
        assert!(prover.validate_inputs(80_000_000, 80_000_000, 0xAA, 1).is_err()); // 8 XFG not in v1

        // v2: 0.8, 80, 800 (no 8)
        assert!(prover.validate_inputs(8_000_000, 8_000_000, 0xAA, 2).is_ok());
        assert!(prover.validate_inputs(800_000_000, 800_000_000, 0xAA, 2).is_ok());
        assert!(prover.validate_inputs(80_000_000, 80_000_000, 0xAA, 2).is_err()); // 8 XFG not in v2
    }

    #[test]
    fn test_secret_conversion() {
        let prover = XfgBurnMintProver::new(128);

        let secret = [1, 2, 3, 4, 5, 6, 7, 8];
        let element = prover.secret_to_field_element(&secret).unwrap();
        assert_eq!(element, BaseElement::from(0x04030201u32));

        let short_secret = [1, 2, 3];
        assert!(prover.secret_to_field_element(&short_secret).is_err());
    }

    #[test]
    fn test_proof_generation_heat() {
        let prover = XfgBurnMintProver::new(128);
        let secret = [42u8; 32];

        let result = prover.prove_burn_mint(
            8_000_000,           // 0.8 XFG
            8_000_000,           // 1:1 mint
            0xDEADBEEF,          // tx hash
            &secret,
            1,                   // mainnet
            1,                   // ETH
            3,                   // v3
            DEPOSIT_TERM_FOREVER,
        );

        assert!(result.is_ok(), "HEAT proof generation failed: {:?}", result);
    }

    #[test]
    fn test_proof_generation_cold() {
        let prover = XfgBurnMintProver::new(128);
        let secret = [42u8; 32];

        let result = prover.prove_burn_mint(
            800_000_000,         // 80 XFG
            800_000_000,         // 1:1 mint
            0xCAFEBABE,          // tx hash
            &secret,
            1,                   // mainnet
            42161,               // Arbitrum
            3,                   // v3
            16000,               // ~3 months
        );

        assert!(result.is_ok(), "COLD proof generation failed: {:?}", result);
    }
}
