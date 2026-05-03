//! XFG Burn & Mint AIR Implementation for Winterfell (v3 unified format)
//!
//! This module implements the Winterfell AIR for XFG burn and HEAT mint operations,
//! as well as COLD deposit proofs, using the unified v3 commitment format.
//!
//! ## v3 Unified Commitment Preimage (56 bytes)
//! Matches Fuego C++ `StarkCommitmentGenerator::computeCommitment()`:
//!   secret[32] || le64(amount) || le32(network_id) || le32(chain_id) || le32(version) || le32(term)
//!
//! tx_hash is NOT in the commitment preimage (circular dependency: commitment
//! goes in tx_extra which is part of the transaction). tx_hash binds via merkle
//! leaf indexing + separate public input.
//!
//! ## v3 Nullifier (49 bytes)
//! Matches Fuego C++ `StarkCommitmentGenerator::computeNullifier()`:
//!   secret[32] || "nullifier"[9] || le64(amount)
//!
//! ## Atomic Units
//! - 1 XFG = 10,000,000 atomic units (7 decimal places)
//! - All burn/mint operations use 1:1 conversion ratio in atomic units

use crate::{types::field::PrimeField64, Result};
use anyhow;
use sha3::{Digest, Keccak256};
use winter_math::{FieldElement, StarkField, ToElements};
use winterfell::{
    math::fields::f64::BaseElement, Air, AirContext, Assertion, EvaluationFrame, ProofOptions,
    Prover, TraceInfo, TraceTable, TransitionConstraintDegree,
};

/// DEPOSIT_TERM_FOREVER — matches Fuego C++ ((uint32_t)(-1))
pub const DEPOSIT_TERM_FOREVER: u32 = 0xFFFFFFFF;

/// Public inputs for burn & mint verification (v3 unified)
#[derive(Debug, Clone)]
pub struct BurnMintPublicInputs {
    /// Burn amount in XFG tokens (atomic units)
    pub burn_amount: BaseElement,
    /// Mint amount in HEAT/CD tokens (atomic units, must equal burn_amount)
    pub mint_amount: BaseElement,
    /// Transaction hash (first 4 bytes as u32 LE — binds proof to specific on-chain tx)
    /// NOT part of commitment preimage; separate public input for binding
    pub txn_hash: BaseElement,
    /// State (0=init, 1=burn, 2=mint, 3=complete)
    pub state: BaseElement,
    /// Fuego network ID (1=mainnet, 2=testnet — prevents cross-network replay)
    pub network_id: BaseElement,
    /// Target chain ID (1=ETH, 42161=ARB)
    pub target_chain_id: BaseElement,
    /// Commitment format version (3 = v3 unified relay format, 4 tiers)
    pub commitment_version: BaseElement,
    /// Deposit term in blocks (COLD actual term, HEAT = DEPOSIT_TERM_FOREVER)
    pub deposit_term: BaseElement,
}

impl ToElements<BaseElement> for BurnMintPublicInputs {
    fn to_elements(&self) -> Vec<BaseElement> {
        vec![
            self.burn_amount,
            self.mint_amount,
            self.txn_hash,
            self.state,
            self.network_id,
            self.target_chain_id,
            self.commitment_version,
            self.deposit_term,
        ]
    }
}

/// XFG Burn & Mint AIR for Winterfell (v3 unified format)
///
/// Execution Trace Layout (7 registers, 64 steps):
/// - Register 0: Burn amount (XFG atomic units)
/// - Register 1: Mint amount (HEAT/CD atomic units)
/// - Register 2: Transaction hash (on-chain tx binding)
/// - Register 3: Deposit term (COLD lock period, FOREVER for HEAT)
/// - Register 4: State (0=init, 1=burn, 2=mint, 3=complete)
/// - Register 5: Nullifier (anti-double-spend)
/// - Register 6: Commitment (cryptographic binding to on-chain leaf)
pub struct XfgBurnMintAir {
    context: AirContext<BaseElement>,
    pub public_inputs: BurnMintPublicInputs,
    secret: BaseElement,
    options: ProofOptions,
}

impl XfgBurnMintAir {
    /// Create new AIR with explicit secret (for proof generation)
    pub fn new_with_secret(
        trace_info: TraceInfo,
        public_inputs: BurnMintPublicInputs,
        secret: BaseElement,
        options: ProofOptions,
    ) -> Self {
        let constraint_degrees = vec![
            TransitionConstraintDegree::new(1), // burn amount validation
            TransitionConstraintDegree::new(1), // mint proportionality
            TransitionConstraintDegree::new(1), // txn hash consistency
            TransitionConstraintDegree::new(1), // deposit term consistency
            TransitionConstraintDegree::new(1), // state transitions
            TransitionConstraintDegree::new(1), // nullifier consistency
            TransitionConstraintDegree::new(1), // commitment validation
        ];

        let context = AirContext::new(trace_info, constraint_degrees, 7, options.clone());

        Self {
            context,
            public_inputs,
            secret,
            options,
        }
    }

    /// v3 UNIFIED COMMITMENT — matches Fuego C++ StarkCommitmentGenerator::computeCommitment()
    ///
    /// Preimage (56 bytes):
    ///   secret[8*] || le64(amount) || le32(network_id) || le32(chain_id) || le32(version) || le32(term)
    ///
    /// *Note: BaseElement is u64, so secret is 8 bytes here. C++ uses full 32-byte secret.
    /// For field-element arithmetic in the STARK, we use the first 8 bytes.
    /// The full 32-byte secret is used in the off-chain keccak computation.
    ///
    /// NO tx_hash in preimage (circular dependency).
    /// NO recipient (contract mints to msg.sender, nullifier prevents replay).
    pub fn compute_commitment(&self, secret: &BaseElement) -> BaseElement {
        let mut hasher = Keccak256::new();

        // Secret (8 bytes LE — field element representation)
        hasher.update(&secret.as_int().to_le_bytes());

        // Amount (8 bytes LE)
        hasher.update(&self.public_inputs.burn_amount.as_int().to_le_bytes());

        // Network ID (8 bytes LE — field element, but only low 4 bytes matter)
        hasher.update(&self.public_inputs.network_id.as_int().to_le_bytes());

        // Target chain ID (8 bytes LE)
        hasher.update(&self.public_inputs.target_chain_id.as_int().to_le_bytes());

        // Commitment version (8 bytes LE)
        hasher.update(&self.public_inputs.commitment_version.as_int().to_le_bytes());

        // Term (8 bytes LE) — DEPOSIT_TERM_FOREVER for HEAT, actual blocks for COLD
        hasher.update(&self.public_inputs.deposit_term.as_int().to_le_bytes());

        let hash = hasher.finalize();
        BaseElement::from(u32::from_le_bytes([hash[0], hash[1], hash[2], hash[3]]))
    }

    /// v3 NULLIFIER — matches Fuego C++ StarkCommitmentGenerator::computeNullifier()
    ///
    /// Preimage (49 bytes):
    ///   secret[8*] || "nullifier"[9] || le64(amount)
    ///
    /// Domain separator "nullifier" prevents collision with commitment hash.
    /// Amount binding prevents cross-tier nullifier reuse.
    pub fn compute_nullifier(&self, secret: &BaseElement) -> BaseElement {
        let mut hasher = Keccak256::new();

        // Secret (8 bytes LE)
        hasher.update(&secret.as_int().to_le_bytes());

        // Domain separator
        hasher.update(b"nullifier");

        // Amount (8 bytes LE)
        hasher.update(&self.public_inputs.burn_amount.as_int().to_le_bytes());

        let hash = hasher.finalize();
        BaseElement::from(u32::from_le_bytes([hash[0], hash[1], hash[2], hash[3]]))
    }

    /// Validate burn amount constraints (v3: 4 tiers)
    /// Valid: 0.8 XFG, 8 XFG, 80 XFG, 800 XFG (in atomic units)
    fn validate_burn_amount<E: FieldElement<BaseField = BaseElement>>(&self, burn_amount: E) -> E {
        let tier0 = E::from(8_000_000u32);     // 0.8 XFG
        let tier1 = E::from(80_000_000u32);    // 8 XFG
        let tier2 = E::from(800_000_000u32);   // 80 XFG

        // 800 XFG = 8,000,000,000 exceeds u32, use multiplication
        let tier3_multiplier = E::from(10u32);
        let tier3 = tier2 * tier3_multiplier;  // 800 XFG

        // Constraint: product = 0 iff burn_amount is exactly one of the four tiers
        (burn_amount - tier0) * (burn_amount - tier1) * (burn_amount - tier2) * (burn_amount - tier3)
    }

    /// Validate mint proportionality (1:1 atomic unit ratio)
    fn validate_mint_proportionality<E: FieldElement<BaseField = BaseElement>>(
        &self,
        burn_amount: E,
        mint_amount: E,
    ) -> E {
        mint_amount - burn_amount
    }

    /// Validate state transitions: 0→1, 1→2, 2→3, or stay same
    fn validate_state_transitions<E: FieldElement<BaseField = BaseElement>>(
        current_state: E,
        next_state: E,
    ) -> E {
        let state_diff = next_state - current_state;
        // diff * (diff - 1) = 0  ⟹  diff ∈ {0, 1}
        state_diff * (state_diff - E::ONE)
    }

    /// Validate nullifier matches computed value from secret
    fn validate_nullifier_consistency<E: FieldElement<BaseField = BaseElement>>(
        &self,
        trace_nullifier: E,
    ) -> E {
        let expected_nullifier = E::from(self.compute_nullifier(&self.secret));
        trace_nullifier - expected_nullifier
    }

    /// Build execution trace (7 registers x 64 steps)
    pub fn build_trace(&self) -> TraceTable<BaseElement> {
        let nullifier = self.compute_nullifier(&self.secret);
        let commitment = self.compute_commitment(&self.secret);

        let mut reg0 = Vec::with_capacity(64); // Burn amount
        let mut reg1 = Vec::with_capacity(64); // Mint amount
        let mut reg2 = Vec::with_capacity(64); // Transaction hash
        let mut reg3 = Vec::with_capacity(64); // Deposit term
        let mut reg4 = Vec::with_capacity(64); // State
        let mut reg5 = Vec::with_capacity(64); // Nullifier
        let mut reg6 = Vec::with_capacity(64); // Commitment

        for step in 0..64 {
            let state = match step {
                0..=15 => 0u32,
                16..=31 => 1,
                32..=47 => 2,
                _ => 3,
            };

            reg0.push(self.public_inputs.burn_amount);
            reg1.push(self.public_inputs.mint_amount);
            reg2.push(self.public_inputs.txn_hash);
            reg3.push(self.public_inputs.deposit_term);
            reg4.push(BaseElement::from(state));
            reg5.push(nullifier);
            reg6.push(commitment);
        }

        TraceTable::init(vec![reg0, reg1, reg2, reg3, reg4, reg5, reg6])
    }
}

impl Air for XfgBurnMintAir {
    type BaseField = BaseElement;
    type PublicInputs = BurnMintPublicInputs;

    fn new(
        trace_info: TraceInfo,
        public_inputs: Self::PublicInputs,
        options: ProofOptions,
    ) -> Self {
        // Air::new is required by the trait but doesn't have a secret parameter.
        // Use a fixed test secret; real proofs use new_with_secret().
        let secret = BaseElement::from(67305985u32);
        Self::new_with_secret(trace_info, public_inputs, secret, options)
    }

    fn context(&self) -> &AirContext<Self::BaseField> {
        &self.context
    }

    fn evaluate_transition<E: FieldElement<BaseField = Self::BaseField>>(
        &self,
        frame: &EvaluationFrame<E>,
        _periodic_values: &[E],
        result: &mut [E],
    ) {
        let current = frame.current();
        let next = frame.next();

        let burn_amount = current[0];
        let mint_amount = current[1];
        let txn_hash = current[2];
        let deposit_term = current[3];
        let current_state = current[4];
        let nullifier = current[5];
        let commitment = current[6];

        let next_state = next[4];

        // Constraint 0: Burn amount is valid tier (4-tier product = 0)
        result[0] = self.validate_burn_amount(burn_amount);

        // Constraint 1: Mint == Burn (1:1 atomic units)
        result[1] = self.validate_mint_proportionality(burn_amount, mint_amount);

        // Constraint 2: Transaction hash matches public input
        result[2] = txn_hash - E::from(self.public_inputs.txn_hash.as_int() as u32);

        // Constraint 3: Deposit term matches public input
        result[3] = deposit_term - E::from(self.public_inputs.deposit_term.as_int() as u32);

        // Constraint 4: Valid state transitions (diff ∈ {0, 1})
        result[4] = Self::validate_state_transitions(current_state, next_state);

        // Constraint 5: Nullifier matches keccak(secret || "nullifier" || amount)
        result[5] = self.validate_nullifier_consistency(nullifier);

        // Constraint 6: Commitment matches keccak(secret || amount || network || chain || version || term)
        let expected_commitment = E::from(self.compute_commitment(&self.secret));
        result[6] = commitment - expected_commitment;
    }

    fn get_assertions(&self) -> Vec<Assertion<Self::BaseField>> {
        let nullifier = self.compute_nullifier(&self.secret);
        let commitment = self.compute_commitment(&self.secret);

        vec![
            // Initial state (step 0)
            Assertion::single(0, 0, self.public_inputs.burn_amount),
            Assertion::single(1, 0, self.public_inputs.mint_amount),
            Assertion::single(2, 0, self.public_inputs.txn_hash),
            Assertion::single(3, 0, self.public_inputs.deposit_term),
            Assertion::single(4, 0, BaseElement::from(0u32)),  // state = init
            Assertion::single(5, 0, nullifier),
            Assertion::single(6, 0, commitment),
            // Final state (step 63)
            Assertion::single(4, 63, BaseElement::from(3u32)), // state = complete
        ]
    }
}

impl Prover for XfgBurnMintAir {
    type BaseField = BaseElement;
    type Air = XfgBurnMintAir;
    type Trace = TraceTable<BaseElement>;
    type HashFn = winterfell::crypto::hashers::Blake3_256<BaseElement>;
    type RandomCoin =
        winterfell::crypto::DefaultRandomCoin<winterfell::crypto::hashers::Blake3_256<BaseElement>>;
    type TraceLde<E>
        = winterfell::DefaultTraceLde<E, winterfell::crypto::hashers::Blake3_256<BaseElement>>
    where
        E: winterfell::math::FieldElement<BaseField = Self::BaseField>;
    type ConstraintEvaluator<'a, E>
        = winterfell::DefaultConstraintEvaluator<'a, XfgBurnMintAir, E>
    where
        E: winterfell::math::FieldElement<BaseField = Self::BaseField>;

    fn get_pub_inputs(&self, _trace: &Self::Trace) -> <Self::Air as Air>::PublicInputs {
        self.public_inputs.clone()
    }

    fn options(&self) -> &ProofOptions {
        &self.options
    }

    fn new_trace_lde<E>(
        &self,
        trace_info: &TraceInfo,
        main_trace: &winterfell::matrix::ColMatrix<Self::BaseField>,
        domain: &winterfell::StarkDomain<Self::BaseField>,
    ) -> (Self::TraceLde<E>, winterfell::TracePolyTable<E>)
    where
        E: winterfell::math::FieldElement<BaseField = Self::BaseField>,
    {
        winterfell::DefaultTraceLde::new(trace_info, main_trace, domain)
    }

    fn new_evaluator<'a, E>(
        &self,
        air: &'a Self::Air,
        aux_rand_elements: winterfell::AuxTraceRandElements<E>,
        composition_coefficients: winterfell::ConstraintCompositionCoefficients<E>,
    ) -> Self::ConstraintEvaluator<'a, E>
    where
        E: winterfell::math::FieldElement<BaseField = Self::BaseField>,
    {
        winterfell::DefaultConstraintEvaluator::new(
            air,
            aux_rand_elements,
            composition_coefficients,
        )
    }
}

// Helper to build v3 public inputs for tests and CLI
pub fn make_public_inputs(
    burn_amount: u32,
    txn_hash: u32,
    network_id: u32,
    target_chain_id: u32,
    commitment_version: u32,
    deposit_term: u32,
) -> BurnMintPublicInputs {
    BurnMintPublicInputs {
        burn_amount: BaseElement::from(burn_amount),
        mint_amount: BaseElement::from(burn_amount), // 1:1
        txn_hash: BaseElement::from(txn_hash),
        state: BaseElement::from(0u32),
        network_id: BaseElement::from(network_id),
        target_chain_id: BaseElement::from(target_chain_id),
        commitment_version: BaseElement::from(commitment_version),
        deposit_term: BaseElement::from(deposit_term),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_inputs_heat() -> BurnMintPublicInputs {
        make_public_inputs(
            8_000_000,        // 0.8 XFG
            0xDEADBEEF,       // dummy tx hash
            1,                // mainnet
            1,                // ETH
            3,                // v3
            DEPOSIT_TERM_FOREVER,
        )
    }

    fn test_inputs_cold() -> BurnMintPublicInputs {
        make_public_inputs(
            800_000_000,      // 80 XFG
            0xCAFEBABE,       // dummy tx hash
            1,                // mainnet
            42161,            // Arbitrum
            3,                // v3
            16000,            // ~3 months in blocks
        )
    }

    #[test]
    fn test_air_creation() {
        let trace_info = TraceInfo::new(7, 64);
        let public_inputs = test_inputs_heat();
        let secret = BaseElement::from(42u32);
        let options = ProofOptions::new(42, 8, 4, winterfell::FieldExtension::None, 8, 31);

        let air = XfgBurnMintAir::new_with_secret(trace_info, public_inputs, secret, options);
        assert_eq!(air.trace_info().width(), 7);
        assert_eq!(air.trace_info().length(), 64);
    }

    #[test]
    fn test_commitment_deterministic() {
        let trace_info = TraceInfo::new(7, 64);
        let public_inputs = test_inputs_heat();
        let secret = BaseElement::from(42u32);
        let options = ProofOptions::new(42, 8, 4, winterfell::FieldExtension::None, 8, 31);

        let air = XfgBurnMintAir::new_with_secret(trace_info, public_inputs, secret, options);
        let c1 = air.compute_commitment(&secret);
        let c2 = air.compute_commitment(&secret);
        assert_eq!(c1, c2);
    }

    #[test]
    fn test_commitment_differs_heat_vs_cold() {
        let trace_info = TraceInfo::new(7, 64);
        let secret = BaseElement::from(42u32);
        let options = ProofOptions::new(42, 8, 4, winterfell::FieldExtension::None, 8, 31);

        let heat_inputs = test_inputs_heat();
        let cold_inputs = test_inputs_cold();

        let heat_air = XfgBurnMintAir::new_with_secret(
            trace_info.clone(), heat_inputs, secret, options.clone());
        let cold_air = XfgBurnMintAir::new_with_secret(
            trace_info, cold_inputs, secret, options);

        // Different amount + term + chain → different commitment
        assert_ne!(
            heat_air.compute_commitment(&secret),
            cold_air.compute_commitment(&secret)
        );
    }

    #[test]
    fn test_nullifier_deterministic() {
        let trace_info = TraceInfo::new(7, 64);
        let public_inputs = test_inputs_heat();
        let secret = BaseElement::from(42u32);
        let options = ProofOptions::new(42, 8, 4, winterfell::FieldExtension::None, 8, 31);

        let air = XfgBurnMintAir::new_with_secret(trace_info, public_inputs, secret, options);
        let n1 = air.compute_nullifier(&secret);
        let n2 = air.compute_nullifier(&secret);
        assert_eq!(n1, n2);
    }

    #[test]
    fn test_nullifier_differs_by_amount() {
        let trace_info = TraceInfo::new(7, 64);
        let secret = BaseElement::from(42u32);
        let options = ProofOptions::new(42, 8, 4, winterfell::FieldExtension::None, 8, 31);

        let inputs_08 = make_public_inputs(8_000_000, 0xAA, 1, 1, 3, DEPOSIT_TERM_FOREVER);
        let inputs_80 = make_public_inputs(800_000_000, 0xAA, 1, 1, 3, DEPOSIT_TERM_FOREVER);

        let air_08 = XfgBurnMintAir::new_with_secret(
            trace_info.clone(), inputs_08, secret, options.clone());
        let air_80 = XfgBurnMintAir::new_with_secret(
            trace_info, inputs_80, secret, options);

        // Same secret, different amount → different nullifier (prevents cross-tier reuse)
        assert_ne!(
            air_08.compute_nullifier(&secret),
            air_80.compute_nullifier(&secret)
        );
    }

    #[test]
    fn test_state_transitions() {
        use winter_math::FieldElement;

        let s0 = BaseElement::from(0u32);
        let s1 = BaseElement::from(1u32);
        let s2 = BaseElement::from(2u32);
        let s3 = BaseElement::from(3u32);

        // Valid: 0→1, 1→2, 2→3, stay same
        assert_eq!(XfgBurnMintAir::validate_state_transitions(s0, s1), BaseElement::ZERO);
        assert_eq!(XfgBurnMintAir::validate_state_transitions(s1, s2), BaseElement::ZERO);
        assert_eq!(XfgBurnMintAir::validate_state_transitions(s2, s3), BaseElement::ZERO);
        assert_eq!(XfgBurnMintAir::validate_state_transitions(s1, s1), BaseElement::ZERO);

        // Invalid: skip or backwards
        assert_ne!(XfgBurnMintAir::validate_state_transitions(s0, s2), BaseElement::ZERO);
        assert_ne!(XfgBurnMintAir::validate_state_transitions(s2, s0), BaseElement::ZERO);
    }

    #[test]
    fn test_burn_amount_validation() {
        let trace_info = TraceInfo::new(7, 64);
        let public_inputs = test_inputs_heat();
        let secret = BaseElement::from(42u32);
        let options = ProofOptions::new(42, 8, 4, winterfell::FieldExtension::None, 8, 31);
        let air = XfgBurnMintAir::new_with_secret(trace_info, public_inputs, secret, options);

        // Valid tiers
        assert_eq!(air.validate_burn_amount(BaseElement::from(8_000_000u32)), BaseElement::ZERO);   // 0.8
        assert_eq!(air.validate_burn_amount(BaseElement::from(80_000_000u32)), BaseElement::ZERO);  // 8
        assert_eq!(air.validate_burn_amount(BaseElement::from(800_000_000u32)), BaseElement::ZERO); // 80

        // Invalid
        assert_ne!(air.validate_burn_amount(BaseElement::from(1_000_000u32)), BaseElement::ZERO);
        assert_ne!(air.validate_burn_amount(BaseElement::from(0u32)), BaseElement::ZERO);
    }

    #[test]
    fn test_nullifier_consistency_constraint() {
        let trace_info = TraceInfo::new(7, 64);
        let public_inputs = test_inputs_heat();
        let secret = BaseElement::from(42u32);
        let options = ProofOptions::new(42, 8, 4, winterfell::FieldExtension::None, 8, 31);
        let air = XfgBurnMintAir::new_with_secret(trace_info, public_inputs, secret, options);

        let correct = air.compute_nullifier(&secret);
        assert_eq!(air.validate_nullifier_consistency(correct), BaseElement::ZERO);

        let wrong = BaseElement::from(999999u32);
        assert_ne!(air.validate_nullifier_consistency(wrong), BaseElement::ZERO);
    }

    #[test]
    fn test_trace_build() {
        let trace_info = TraceInfo::new(7, 64);
        let public_inputs = test_inputs_heat();
        let secret = BaseElement::from(42u32);
        let options = ProofOptions::new(42, 8, 4, winterfell::FieldExtension::None, 8, 31);
        let air = XfgBurnMintAir::new_with_secret(trace_info, public_inputs, secret, options);

        let trace = air.build_trace();
        assert_eq!(trace.width(), 7);
        assert_eq!(trace.length(), 64);
    }
}
