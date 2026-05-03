//! Transition Functions for AIR
//! 
//! This module defines transition functions that describe how states evolve
//! between computation steps in AIR (Algebraic Intermediate Representation).

use crate::types::{FieldElement, StarkComponent, TypeError};
use std::fmt::{Display, Formatter};

/// Transition function for AIR
/// 
/// A transition function defines how the state changes from one step to the next
/// in a computation. It's represented as a matrix of coefficients.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TransitionFunction<F: FieldElement> {
    /// Transition matrix coefficients
    /// Each row represents a register, each column represents a coefficient
    pub coefficients: Vec<Vec<F>>,
    /// Function degree
    pub degree: usize,
    /// Number of input registers
    pub num_inputs: usize,
    /// Number of output registers
    pub num_outputs: usize,
}

impl<F: FieldElement> TransitionFunction<F> {
    /// Create a new transition function
    pub fn new(coefficients: Vec<Vec<F>>, degree: usize) -> Self {
        let num_outputs = coefficients.len();
        let num_inputs = coefficients.first().map(|row| row.len()).unwrap_or(0);

        Self {
            coefficients,
            degree,
            num_inputs,
            num_outputs,
        }
    }

    /// Create a simple linear transition function
    pub fn linear(coefficients: Vec<Vec<F>>) -> Self {
        Self::new(coefficients, 1)
    }

    /// Create a quadratic transition function
    pub fn quadratic(coefficients: Vec<Vec<F>>) -> Self {
        Self::new(coefficients, 2)
    }

    /// Get the number of registers
    pub fn num_registers(&self) -> usize {
        self.num_outputs
    }

    /// Get the function degree
    pub fn degree(&self) -> usize {
        self.degree
    }

    /// Apply the transition function to a state
    pub fn apply(&self, current_state: &[F]) -> Vec<F> {
        let mut next_state = vec![F::zero(); self.num_outputs];

        for (i, row) in self.coefficients.iter().enumerate() {
            for (j, &coeff) in row.iter().enumerate() {
                if j < current_state.len() {
                    next_state[i] = next_state[i] + coeff * current_state[j];
                }
            }
        }

        next_state
    }

    /// Apply the transition function with degree > 1
    pub fn apply_degree(&self, current_state: &[F], degree: usize) -> Vec<F> {
        if degree == 1 {
            return self.apply(current_state);
        }

        let mut next_state = vec![F::zero(); self.num_outputs];

        for (i, row) in self.coefficients.iter().enumerate() {
            for (j, &coeff) in row.iter().enumerate() {
                if j < current_state.len() {
                    let power = current_state[j].pow(degree as u64);
                    next_state[i] = next_state[i] + coeff * power;
                }
            }
        }

        next_state
    }

    /// Check if the transition is valid
    pub fn is_valid_transition(&self, current_state: &[F], next_state: &[F]) -> bool {
        let computed_next = self.apply(current_state);
        computed_next == next_state
    }

    /// Get the coefficient matrix
    pub fn coefficients(&self) -> &[Vec<F>] {
        &self.coefficients
    }

    /// Set a coefficient value
    pub fn set_coefficient(&mut self, row: usize, col: usize, value: F) -> Result<(), TransitionError> {
        if row >= self.coefficients.len() {
            return Err(TransitionError::InvalidRow(row));
        }

        if col >= self.coefficients[row].len() {
            return Err(TransitionError::InvalidColumn(col));
        }

        self.coefficients[row][col] = value;
        Ok(())
    }

    /// Get a coefficient value
    pub fn get_coefficient(&self, row: usize, col: usize) -> Result<F, TransitionError> {
        if row >= self.coefficients.len() {
            return Err(TransitionError::InvalidRow(row));
        }

        if col >= self.coefficients[row].len() {
            return Err(TransitionError::InvalidColumn(col));
        }

        Ok(self.coefficients[row][col])
    }

    /// Create an identity transition function
    pub fn identity(num_registers: usize) -> Self {
        let mut coefficients = vec![vec![F::zero(); num_registers]; num_registers];
        
        for i in 0..num_registers {
            coefficients[i][i] = F::one();
        }

        Self::new(coefficients, 1)
    }

    /// Create a Fibonacci transition function
    pub fn fibonacci() -> Self {
        let coefficients = vec![
            vec![F::zero(), F::one()],     // next_a = b
            vec![F::one(), F::one()],      // next_b = a + b
        ];

        Self::new(coefficients, 1)
    }

    /// Create a counter transition function
    pub fn counter() -> Self {
        let coefficients = vec![
            vec![F::one(), F::one()],      // next = current + 1
        ];

        Self::new(coefficients, 1)
    }

    /// Validate the transition function
    pub fn validate(&self) -> Result<(), TransitionError> {
        if self.coefficients.is_empty() {
            return Err(TransitionError::EmptyCoefficients);
        }

        let first_row_len = self.coefficients[0].len();
        for (i, row) in self.coefficients.iter().enumerate() {
            if row.len() != first_row_len {
                return Err(TransitionError::InconsistentRowLengths(i));
            }
        }

        if self.degree == 0 {
            return Err(TransitionError::ZeroDegree);
        }

        Ok(())
    }
}

impl<F: FieldElement> Display for TransitionFunction<F> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "TransitionFunction(degree={}, inputs={}, outputs={})",
            self.degree, self.num_inputs, self.num_outputs
        )
    }
}

impl<F: FieldElement> StarkComponent<F> for TransitionFunction<F> {
    fn validate(&self) -> std::result::Result<(), TypeError> {
        self.validate().map_err(|e| TypeError::InvalidConversion(e.to_string()))
    }

    fn to_bytes(&self) -> Vec<u8> {
        // Placeholder implementation
        Vec::new()
    }

    fn from_bytes(_bytes: &[u8]) -> std::result::Result<Self, TypeError> {
        // Placeholder implementation
        Err(TypeError::InvalidConversion("Not implemented".to_string()))
    }
}

/// Transition function error types
#[derive(Debug, thiserror::Error)]
pub enum TransitionError {
    /// Empty coefficient matrix
    #[error("Empty coefficient matrix")]
    EmptyCoefficients,

    /// Inconsistent row lengths
    #[error("Inconsistent row lengths at row {0}")]
    InconsistentRowLengths(usize),

    /// Invalid row index
    #[error("Invalid row index: {0}")]
    InvalidRow(usize),

    /// Invalid column index
    #[error("Invalid column index: {0}")]
    InvalidColumn(usize),

    /// Zero degree function
    #[error("Zero degree function")]
    ZeroDegree,

    /// Invalid transition
    #[error("Invalid transition: {0}")]
    InvalidTransition(String),
}

/// Transition function builder
pub struct TransitionFunctionBuilder<F: FieldElement> {
    coefficients: Vec<Vec<F>>,
    degree: usize,
}

impl<F: FieldElement> TransitionFunctionBuilder<F> {
    /// Create a new transition function builder
    pub fn new(degree: usize) -> Self {
        Self {
            coefficients: Vec::new(),
            degree,
        }
    }

    /// Add a row to the coefficient matrix
    pub fn add_row(mut self, row: Vec<F>) -> Self {
        self.coefficients.push(row);
        self
    }

    /// Set a specific coefficient
    pub fn set_coefficient(mut self, row: usize, col: usize, value: F) -> Self {
        while self.coefficients.len() <= row {
            self.coefficients.push(Vec::new());
        }

        while self.coefficients[row].len() <= col {
            self.coefficients[row].push(F::zero());
        }

        self.coefficients[row][col] = value;
        self
    }

    /// Build the transition function
    pub fn build(self) -> TransitionFunction<F> {
        TransitionFunction::new(self.coefficients, self.degree)
    }
}

impl<F: FieldElement> Default for TransitionFunctionBuilder<F> {
    fn default() -> Self {
        Self::new(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::field::PrimeField64;

    #[test]
    fn test_transition_function_creation() {
        let coefficients = vec![
            vec![PrimeField64::new(1), PrimeField64::new(0)],
            vec![PrimeField64::new(0), PrimeField64::new(1)],
        ];

        let transition = TransitionFunction::new(coefficients, 1);

        assert_eq!(transition.degree(), 1);
        assert_eq!(transition.num_registers(), 2);
    }

    #[test]
    fn test_identity_transition() {
        let identity = TransitionFunction::identity(2);
        let state = vec![PrimeField64::new(1), PrimeField64::new(2)];

        let next_state = identity.apply(&state);
        assert_eq!(next_state, state);
    }

    #[test]
    fn test_fibonacci_transition() {
        let fibonacci = TransitionFunction::fibonacci();
        let state = vec![PrimeField64::new(1), PrimeField64::new(1)];

        let next_state = fibonacci.apply(&state);
        assert_eq!(next_state, vec![PrimeField64::new(1), PrimeField64::new(2)]);
    }

    #[test]
    fn test_counter_transition() {
        let counter = TransitionFunction::counter();
        let state = vec![PrimeField64::new(5)];

        let next_state = counter.apply(&state);
        assert_eq!(next_state, vec![PrimeField64::new(6)]);
    }

    #[test]
    fn test_transition_function_validation() {
        let valid_transition: TransitionFunction<PrimeField64> = TransitionFunction::identity(2);
        assert!(valid_transition.validate().is_ok());
        
        let invalid_transition: TransitionFunction<PrimeField64> = TransitionFunction::new(vec![], 0);
        assert!(invalid_transition.validate().is_err());
    }

    #[test]
    fn test_transition_function_builder() {
        let transition = TransitionFunctionBuilder::new(1)
            .add_row(vec![PrimeField64::new(1), PrimeField64::new(0)])
            .add_row(vec![PrimeField64::new(0), PrimeField64::new(1)])
            .build();

        assert_eq!(transition.degree(), 1);
        assert_eq!(transition.num_registers(), 2);
    }
}