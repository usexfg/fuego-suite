//! Boundary Conditions for AIR
//! 
//! This module defines boundary conditions that specify initial and final states
//! for computations in AIR (Algebraic Intermediate Representation).

use crate::types::{FieldElement, StarkComponent, TypeError};
use std::fmt::{Display, Formatter};

/// Boundary conditions for AIR
/// 
/// Boundary conditions specify constraints on the initial and final states
/// of a computation, ensuring the computation starts and ends correctly.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BoundaryConditions<F: FieldElement> {
    /// Boundary constraints
    pub constraints: Vec<BoundaryConstraint<F>>,
}

impl<F: FieldElement> BoundaryConditions<F> {
    /// Create new boundary conditions
    pub fn new(constraints: Vec<BoundaryConstraint<F>>) -> Self {
        Self { constraints }
    }

    /// Create empty boundary conditions
    pub fn empty() -> Self {
        Self::new(Vec::new())
    }

    /// Add a boundary constraint
    pub fn add_constraint(&mut self, constraint: BoundaryConstraint<F>) {
        self.constraints.push(constraint);
    }

    /// Check if all boundary conditions are satisfied
    pub fn verify(&self, initial_state: &[F], final_state: &[F]) -> bool {
        self.constraints.iter().all(|constraint| {
            constraint.verify(initial_state, final_state)
        })
    }

    /// Get the number of boundary constraints
    pub fn len(&self) -> usize {
        self.constraints.len()
    }

    /// Check if there are no boundary constraints
    pub fn is_empty(&self) -> bool {
        self.constraints.is_empty()
    }

    /// Validate boundary conditions
    pub fn validate(&self) -> Result<(), BoundaryError> {
        for constraint in &self.constraints {
            constraint.validate()?;
        }
        Ok(())
    }
}

impl<F: FieldElement> Display for BoundaryConditions<F> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "BoundaryConditions(constraints={})", self.constraints.len())
    }
}

impl<F: FieldElement> StarkComponent<F> for BoundaryConditions<F> {
    fn validate(&self) -> std::result::Result<(), TypeError> {
        self.validate().map_err(|e| TypeError::InvalidConversion(e.to_string()))
    }

    fn to_bytes(&self) -> Vec<u8> {
        Vec::new()
    }

    fn from_bytes(_bytes: &[u8]) -> std::result::Result<Self, TypeError> {
        Err(TypeError::InvalidConversion("Not implemented".to_string()))
    }
}

/// Individual boundary constraint
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BoundaryConstraint<F: FieldElement> {
    /// Register index
    pub register: usize,
    /// Step index (0 for initial, trace_length-1 for final)
    pub step: usize,
    /// Expected value
    pub value: F,
    /// Constraint type
    pub constraint_type: BoundaryType,
}

impl<F: FieldElement> BoundaryConstraint<F> {
    /// Create a new boundary constraint
    pub fn new(register: usize, step: usize, value: F, constraint_type: BoundaryType) -> Self {
        Self {
            register,
            step,
            value,
            constraint_type,
        }
    }

    /// Create an initial condition constraint
    pub fn initial(register: usize, value: F) -> Self {
        Self::new(register, 0, value, BoundaryType::Initial)
    }

    /// Create a final condition constraint
    pub fn final_condition(register: usize, value: F) -> Self {
        Self::new(register, usize::MAX, value, BoundaryType::Final)
    }

    /// Verify the boundary constraint
    pub fn verify(&self, initial_state: &[F], final_state: &[F]) -> bool {
        match self.constraint_type {
            BoundaryType::Initial => {
                if self.register < initial_state.len() {
                    initial_state[self.register] == self.value
                } else {
                    false
                }
            }
            BoundaryType::Final => {
                if self.register < final_state.len() {
                    final_state[self.register] == self.value
                } else {
                    false
                }
            }
        }
    }

    /// Validate the boundary constraint
    pub fn validate(&self) -> Result<(), BoundaryError> {
        if self.register == usize::MAX {
            return Err(BoundaryError::InvalidRegister);
        }
        Ok(())
    }
}

impl<F: FieldElement> Display for BoundaryConstraint<F> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "BoundaryConstraint(register={}, step={}, value={:?}, type={:?})",
            self.register, self.step, self.value, self.constraint_type
        )
    }
}

/// Boundary constraint type
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum BoundaryType {
    /// Initial state constraint
    Initial,
    /// Final state constraint
    Final,
}

impl Display for BoundaryType {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            BoundaryType::Initial => write!(f, "Initial"),
            BoundaryType::Final => write!(f, "Final"),
        }
    }
}

/// Boundary error types
#[derive(Debug, thiserror::Error)]
pub enum BoundaryError {
    /// Invalid register index
    #[error("Invalid register index")]
    InvalidRegister,

    /// Invalid step index
    #[error("Invalid step index")]
    InvalidStep,

    /// Invalid constraint
    #[error("Invalid constraint: {0}")]
    InvalidConstraint(String),
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::field::PrimeField64;

    #[test]
    fn test_boundary_conditions_creation() {
        let constraints = vec![
            BoundaryConstraint::initial(0, PrimeField64::new(1)),
            BoundaryConstraint::final_condition(0, PrimeField64::new(10)),
        ];

        let boundary = BoundaryConditions::new(constraints);
        assert_eq!(boundary.len(), 2);
    }

    #[test]
    fn test_boundary_verification() {
        let constraints = vec![
            BoundaryConstraint::initial(0, PrimeField64::new(1)),
            BoundaryConstraint::final_condition(0, PrimeField64::new(10)),
        ];

        let boundary = BoundaryConditions::new(constraints);
        let initial_state = vec![PrimeField64::new(1)];
        let final_state = vec![PrimeField64::new(10)];

        assert!(boundary.verify(&initial_state, &final_state));
    }

    #[test]
    fn test_boundary_constraint_creation() {
        let constraint = BoundaryConstraint::initial(0, PrimeField64::new(1));
        assert_eq!(constraint.register, 0);
        assert_eq!(constraint.step, 0);
        assert_eq!(constraint.value, PrimeField64::new(1));
        assert_eq!(constraint.constraint_type, BoundaryType::Initial);
    }
}