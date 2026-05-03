//! XFG Burn & Mint Verifier (v3 unified relay format)
//!
//! Verifies STARK proofs for HEAT burns and COLD deposits using the unified v3
//! commitment format that matches Fuego C++ `StarkCommitmentGenerator`.

use crate::{
    burn_mint_air::{make_public_inputs, BurnMintPublicInputs, XfgBurnMintAir, DEPOSIT_TERM_FOREVER},
    Result,
};
use std::time::Instant;
use winter_crypto::hashers::Blake3_256;
use winterfell::{
    crypto::{DefaultRandomCoin, MerkleTree},
    math::fields::f64::BaseElement,
    verify, AcceptableOptions, ProofOptions, StarkProof, VerifierError,
};

/// Result of proof verification with detailed information
#[derive(Debug, Clone)]
pub enum VerificationResult {
    Success {
        verification_time: Instant,
        proof_size: usize,
    },
    Failure {
        error: String,
        verification_time: Instant,
        proof_size: usize,
    },
}

impl VerificationResult {
    pub fn is_success(&self) -> bool {
        matches!(self, VerificationResult::Success { .. })
    }

    pub fn verification_time(&self) -> Instant {
        match self {
            VerificationResult::Success { verification_time, .. } => *verification_time,
            VerificationResult::Failure { verification_time, .. } => *verification_time,
        }
    }

    pub fn proof_size(&self) -> usize {
        match self {
            VerificationResult::Success { proof_size, .. } => *proof_size,
            VerificationResult::Failure { proof_size, .. } => *proof_size,
        }
    }

    pub fn error_message(&self) -> Option<&str> {
        match self {
            VerificationResult::Success { .. } => None,
            VerificationResult::Failure { error, .. } => Some(error),
        }
    }
}

/// XFG Burn & Mint Verifier using Winterfell (v3 unified)
pub struct XfgBurnMintVerifier {
    security_parameter: usize,
    proof_options: ProofOptions,
}

impl XfgBurnMintVerifier {
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

    pub fn with_options(security_parameter: usize, proof_options: ProofOptions) -> Self {
        Self {
            security_parameter,
            proof_options,
        }
    }

    /// Verify a burn/deposit proof (v3 unified)
    ///
    /// No recipient address needed — contract mints to msg.sender,
    /// nullifier prevents replay.
    pub fn verify_burn_mint(
        &self,
        proof: &StarkProof,
        burn_amount: u64,
        mint_amount: u64,
        txn_hash: u32,
        network_id: u32,
        target_chain_id: u32,
        commitment_version: u32,
        deposit_term: u32,
    ) -> Result<bool> {
        self.validate_inputs(burn_amount, mint_amount, txn_hash, commitment_version)?;

        let public_inputs = make_public_inputs(
            burn_amount as u32,
            txn_hash,
            network_id,
            target_chain_id,
            commitment_version,
            deposit_term,
        );

        match self.verify_with_winterfell(proof, &public_inputs) {
            Ok(_) => Ok(true),
            Err(e) => {
                eprintln!("Proof verification failed: {:?}", e);
                Ok(false)
            }
        }
    }

    /// Verify proof with pre-built public inputs
    pub fn verify_with_public_inputs(
        &self,
        proof: &StarkProof,
        public_inputs: &BurnMintPublicInputs,
    ) -> Result<bool> {
        self.validate_public_inputs(public_inputs)?;

        match self.verify_with_winterfell(proof, public_inputs) {
            Ok(_) => Ok(true),
            Err(e) => {
                eprintln!("Proof verification failed: {:?}", e);
                Ok(false)
            }
        }
    }

    /// Validate input parameters
    /// v1: 2 tiers, v2: 3 tiers, v3: 4 tiers (0.8, 8, 80, 800)
    fn validate_inputs(
        &self,
        burn_amount: u64,
        mint_amount: u64,
        txn_hash: u32,
        commitment_version: u32,
    ) -> Result<()> {
        let valid_amounts: Vec<u64> = match commitment_version {
            1 => vec![8_000_000, 8_000_000_000],
            2 => vec![8_000_000, 800_000_000, 8_000_000_000],
            3 => vec![8_000_000, 80_000_000, 800_000_000, 8_000_000_000],
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

    fn validate_public_inputs(&self, public_inputs: &BurnMintPublicInputs) -> Result<()> {
        let burn_amount = public_inputs.burn_amount.as_int() as u64;
        let mint_amount = public_inputs.mint_amount.as_int() as u64;
        let txn_hash = public_inputs.txn_hash.as_int() as u32;

        if burn_amount == 0 {
            return Err(crate::XfgStarkError::CryptoError(
                "Burn amount must be greater than 0".to_string(),
            ));
        }
        if mint_amount == 0 {
            return Err(crate::XfgStarkError::CryptoError(
                "Mint amount must be greater than 0".to_string(),
            ));
        }
        if txn_hash == 0 {
            return Err(crate::XfgStarkError::CryptoError(
                "Transaction hash must be non-zero".to_string(),
            ));
        }

        Ok(())
    }

    fn verify_with_winterfell(
        &self,
        proof: &StarkProof,
        public_inputs: &BurnMintPublicInputs,
    ) -> std::result::Result<(), VerifierError> {
        let acceptable_options = AcceptableOptions::OptionSet(vec![self.proof_options.clone()]);

        verify::<XfgBurnMintAir, Blake3_256<BaseElement>, DefaultRandomCoin<Blake3_256<BaseElement>>>(
            proof.clone(),
            public_inputs.clone(),
            &acceptable_options,
        )
    }

    /// Verify proof with detailed error information
    pub fn verify_with_details(
        &self,
        proof: &StarkProof,
        public_inputs: &BurnMintPublicInputs,
    ) -> Result<VerificationResult> {
        self.validate_public_inputs(public_inputs)?;

        match self.verify_with_winterfell(proof, public_inputs) {
            Ok(_) => Ok(VerificationResult::Success {
                verification_time: Instant::now(),
                proof_size: proof.to_bytes().len(),
            }),
            Err(e) => Ok(VerificationResult::Failure {
                error: e.to_string(),
                verification_time: Instant::now(),
                proof_size: proof.to_bytes().len(),
            }),
        }
    }

    pub fn estimate_verification_time(&self, proof_size: usize) -> std::time::Duration {
        std::time::Duration::from_millis((proof_size / 1024) as u64)
    }

    pub fn security_parameter(&self) -> usize {
        self.security_parameter
    }

    pub fn proof_options(&self) -> &ProofOptions {
        &self.proof_options
    }

    pub fn is_valid_proof_format(&self, proof: &StarkProof) -> bool {
        !proof.to_bytes().is_empty()
    }

    /// Batch verify multiple proofs
    pub fn batch_verify(
        &self,
        proofs_and_inputs: &[(StarkProof, BurnMintPublicInputs)],
    ) -> Result<Vec<bool>> {
        let mut results = Vec::with_capacity(proofs_and_inputs.len());
        for (proof, public_inputs) in proofs_and_inputs {
            let result = self.verify_with_public_inputs(proof, public_inputs)?;
            results.push(result);
        }
        Ok(results)
    }
}

impl Default for XfgBurnMintVerifier {
    fn default() -> Self {
        Self::new(128)
    }
}

/// Batch verifier for multiple burn & mint proofs
pub struct BatchBurnMintVerifier {
    verifier: XfgBurnMintVerifier,
}

impl BatchBurnMintVerifier {
    pub fn new(security_parameter: usize) -> Self {
        Self {
            verifier: XfgBurnMintVerifier::new(security_parameter),
        }
    }

    pub fn verify_batch(
        &self,
        proofs_and_inputs: &[(&StarkProof, &BurnMintPublicInputs)],
    ) -> Result<Vec<bool>> {
        let mut results = Vec::new();
        for (proof, public_inputs) in proofs_and_inputs {
            let result = self.verifier.verify_with_public_inputs(proof, public_inputs)?;
            results.push(result);
        }
        Ok(results)
    }

    pub fn verify_all(
        &self,
        proofs_and_inputs: &[(&StarkProof, &BurnMintPublicInputs)],
    ) -> Result<bool> {
        let results = self.verify_batch(proofs_and_inputs)?;
        Ok(results.iter().all(|&valid| valid))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_verifier_creation() {
        let verifier = XfgBurnMintVerifier::new(128);
        assert_eq!(verifier.security_parameter(), 128);
    }

    #[test]
    fn test_v3_input_validation() {
        let verifier = XfgBurnMintVerifier::new(128);

        // All 4 v3 tiers
        assert!(verifier.validate_inputs(8_000_000, 8_000_000, 0xDEAD, 3).is_ok());
        assert!(verifier.validate_inputs(80_000_000, 80_000_000, 0xDEAD, 3).is_ok());
        assert!(verifier.validate_inputs(800_000_000, 800_000_000, 0xDEAD, 3).is_ok());
        assert!(verifier.validate_inputs(8_000_000_000, 8_000_000_000, 0xDEAD, 3).is_ok());

        // Invalid
        assert!(verifier.validate_inputs(1_000_000, 1_000_000, 0xDEAD, 3).is_err());
        assert!(verifier.validate_inputs(8_000_000, 16_000_000, 0xDEAD, 3).is_err());
        assert!(verifier.validate_inputs(8_000_000, 8_000_000, 0, 3).is_err());
        assert!(verifier.validate_inputs(8_000_000, 8_000_000, 0xDEAD, 99).is_err());
    }

    #[test]
    fn test_public_inputs_validation() {
        let verifier = XfgBurnMintVerifier::new(128);

        let valid = make_public_inputs(8_000_000, 0xDEAD, 1, 1, 3, DEPOSIT_TERM_FOREVER);
        assert!(verifier.validate_public_inputs(&valid).is_ok());

        let invalid = make_public_inputs(0, 0xDEAD, 1, 1, 3, DEPOSIT_TERM_FOREVER);
        assert!(verifier.validate_public_inputs(&invalid).is_err());
    }

    #[test]
    fn test_verification_time_estimation() {
        let verifier = XfgBurnMintVerifier::new(128);

        let small = verifier.estimate_verification_time(1024);
        let large = verifier.estimate_verification_time(102400);
        assert!(small < large);
    }
}
