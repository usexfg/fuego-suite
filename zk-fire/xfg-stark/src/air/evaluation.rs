//! Constraint Evaluation for AIR
//! 
//! This module provides efficient evaluation and verification of AIR constraints.

use crate::types::FieldElement;

/// Evaluate all constraints in an AIR system
pub fn evaluate_all_constraints<F: FieldElement>(
    constraints: &[crate::air::constraints::Constraint<F>],
    current_state: &[F],
    next_state: &[F],
    random_challenge: F,
) -> Vec<F> {
    constraints
        .iter()
        .map(|constraint| constraint.evaluate(current_state, next_state, random_challenge))
        .collect()
}

/// Verify that all constraints are satisfied
pub fn verify_all_constraints<F: FieldElement>(
    constraints: &[crate::air::constraints::Constraint<F>],
    current_state: &[F],
    next_state: &[F],
    random_challenge: F,
) -> bool {
    evaluate_all_constraints(constraints, current_state, next_state, random_challenge)
        .iter()
        .all(|&value| value == F::zero())
}