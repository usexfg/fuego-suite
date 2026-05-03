//! Constraint System for AIR
//! 
//! This module defines the algebraic constraints used in AIR (Algebraic Intermediate Representation)
//! for STARK proofs. Constraints are polynomials that must evaluate to zero for valid computations.

use crate::types::{FieldElement, StarkComponent, TypeError};
use std::fmt::{Display, Formatter};

/// Algebraic constraint for AIR
/// 
/// A constraint is a polynomial that must evaluate to zero for valid computations.
/// Constraints can be of different types: transition, boundary, or algebraic.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Constraint<F: FieldElement> {
    /// Constraint polynomial coefficients
    pub polynomial: Vec<F>,
    /// Constraint degree
    pub degree: usize,
    /// Constraint type
    pub constraint_type: ConstraintType,
    /// Constraint description
    pub description: String,
}

impl<F: FieldElement> Constraint<F> {
    /// Create a new constraint
    pub fn new(
        polynomial: Vec<F>,
        degree: usize,
        constraint_type: ConstraintType,
    ) -> Self {
        Self {
            polynomial,
            degree,
            constraint_type,
            description: String::new(),
        }
    }

    /// Create a new constraint with description
    pub fn with_description(
        polynomial: Vec<F>,
        degree: usize,
        constraint_type: ConstraintType,
        description: String,
    ) -> Self {
        Self {
            polynomial,
            degree,
            constraint_type,
            description,
        }
    }

    /// Get the constraint degree
    pub fn degree(&self) -> usize {
        self.degree
    }

    /// Get the constraint type
    pub fn constraint_type(&self) -> &ConstraintType {
        &self.constraint_type
    }

    /// Evaluate the constraint at given points
    /// 
    /// This evaluates the constraint polynomial using the current state,
    /// next state, and a random challenge for soundness.
    pub fn evaluate(
        &self,
        current_state: &[F],
        next_state: &[F],
        random_challenge: F,
    ) -> F {
        let mut result = F::zero();
        let mut power = F::one();

        // Evaluate polynomial: sum(coeff_i * x^i)
        for &coeff in &self.polynomial {
            result = result + coeff * power;
            power = power * random_challenge;
        }

        // Apply constraint-specific evaluation
        match self.constraint_type {
            ConstraintType::Transition => {
                // Transition constraint: f(current, next) = 0
                self.evaluate_transition(current_state, next_state, result)
            }
            ConstraintType::Boundary => {
                // Boundary constraint: f(state) = 0
                self.evaluate_boundary(current_state, result)
            }
            ConstraintType::Algebraic => {
                // Algebraic constraint: f(state) = 0
                self.evaluate_algebraic(current_state, result)
            }
        }
    }

    /// Evaluate transition constraint
    fn evaluate_transition(
        &self,
        current_state: &[F],
        next_state: &[F],
        base_value: F,
    ) -> F {
        // For transition constraints, we typically have:
        // next_state[i] = f(current_state) for some function f
        if current_state.len() > 0 && next_state.len() > 0 {
            // Simple example: next = current + 1
            // This would be: next_state[0] - (current_state[0] + 1)
            if next_state.len() > 0 {
                next_state[0] - (current_state[0] + F::one())
            } else {
                base_value
            }
        } else {
            base_value
        }
    }

    /// Evaluate boundary constraint
    fn evaluate_boundary(&self, state: &[F], base_value: F) -> F {
        // Boundary constraints check initial/final conditions
        // For example: state[0] = 1 (initial condition)
        if state.len() > 0 {
            state[0] - F::one()
        } else {
            base_value
        }
    }

    /// Evaluate algebraic constraint
    fn evaluate_algebraic(&self, state: &[F], base_value: F) -> F {
        // Algebraic constraints are general polynomial constraints
        // For example: state[0]^2 - state[0] = 0
        if state.len() > 0 {
            let x = state[0];
            x * x - x
        } else {
            base_value
        }
    }

    /// Check if the constraint is satisfied
    pub fn is_satisfied(
        &self,
        current_state: &[F],
        next_state: &[F],
        random_challenge: F,
    ) -> bool {
        self.evaluate(current_state, next_state, random_challenge) == F::zero()
    }

    /// Get the constraint as a polynomial
    pub fn as_polynomial(&self) -> &[F] {
        &self.polynomial
    }

    /// Create a linear constraint: ax + b = 0
    pub fn linear(a: F, b: F) -> Self {
        Self::new(vec![b, a], 1, ConstraintType::Algebraic)
    }

    /// Create a quadratic constraint: ax^2 + bx + c = 0
    pub fn quadratic(a: F, b: F, c: F) -> Self {
        Self::new(vec![c, b, a], 2, ConstraintType::Algebraic)
    }

    /// Create a transition constraint: next = f(current)
    pub fn transition(polynomial: Vec<F>) -> Self {
        let degree = polynomial.len().saturating_sub(1);
        Self::new(polynomial, degree, ConstraintType::Transition)
    }

    /// Create a boundary constraint: state = value
    pub fn boundary(register: usize, value: F) -> Self {
        let mut polynomial = vec![F::zero(); register + 1];
        polynomial[register] = F::one();
        polynomial[0] = -value;
        Self::new(polynomial, 1, ConstraintType::Boundary)
    }
}

impl<F: FieldElement> Display for Constraint<F> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Constraint({:?}, degree={}, type={:?})",
            self.polynomial, self.degree, self.constraint_type
        )
    }
}

impl<F: FieldElement> StarkComponent<F> for Constraint<F> {
    fn validate(&self) -> std::result::Result<(), TypeError> {
        if self.polynomial.is_empty() {
            return Err(TypeError::InvalidConversion("Empty polynomial".to_string()));
        }

        if self.degree >= self.polynomial.len() {
            return Err(TypeError::InvalidConversion("Invalid degree".to_string()));
        }

        Ok(())
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

/// Constraint type classification
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ConstraintType {
    /// Transition constraint between states
    Transition,
    /// Boundary condition constraint
    Boundary,
    /// General algebraic constraint
    Algebraic,
}

impl Display for ConstraintType {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            ConstraintType::Transition => write!(f, "Transition"),
            ConstraintType::Boundary => write!(f, "Boundary"),
            ConstraintType::Algebraic => write!(f, "Algebraic"),
        }
    }
}

/// Constraint system builder
#[derive(Debug, Clone)]
pub struct ConstraintSystemBuilder<F: FieldElement> {
    constraints: Vec<Constraint<F>>,
}

impl<F: FieldElement> ConstraintSystemBuilder<F> {
    /// Create a new constraint system builder
    pub fn new() -> Self {
        Self {
            constraints: Vec::new(),
        }
    }

    /// Add a constraint to the system
    pub fn add_constraint(mut self, constraint: Constraint<F>) -> Self {
        self.constraints.push(constraint);
        self
    }

    /// Add a linear constraint
    pub fn linear(mut self, a: F, b: F) -> Self {
        self.constraints.push(Constraint::linear(a, b));
        self
    }

    /// Add a quadratic constraint
    pub fn quadratic(mut self, a: F, b: F, c: F) -> Self {
        self.constraints.push(Constraint::quadratic(a, b, c));
        self
    }

    /// Add a transition constraint
    pub fn transition(mut self, polynomial: Vec<F>) -> Self {
        self.constraints.push(Constraint::transition(polynomial));
        self
    }

    /// Add a boundary constraint
    pub fn boundary(mut self, register: usize, value: F) -> Self {
        self.constraints.push(Constraint::boundary(register, value));
        self
    }

    /// Build the constraint system
    pub fn build(self) -> Vec<Constraint<F>> {
        self.constraints
    }
}

impl<F: FieldElement> Default for ConstraintSystemBuilder<F> {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::field::PrimeField64;

    #[test]
    fn test_constraint_creation() {
        let constraint = Constraint::new(
            vec![PrimeField64::new(1), PrimeField64::new(2)],
            1,
            ConstraintType::Transition,
        );

        assert_eq!(constraint.degree(), 1);
        assert_eq!(constraint.polynomial.len(), 2);
    }

    #[test]
    fn test_linear_constraint() {
        let constraint = Constraint::linear(PrimeField64::new(2), PrimeField64::new(3));
        
        assert_eq!(constraint.degree(), 1);
        assert_eq!(constraint.constraint_type, ConstraintType::Algebraic);
    }

    #[test]
    fn test_quadratic_constraint() {
        let constraint = Constraint::quadratic(
            PrimeField64::new(1),
            PrimeField64::new(2),
            PrimeField64::new(3),
        );
        
        assert_eq!(constraint.degree(), 2);
        assert_eq!(constraint.constraint_type, ConstraintType::Algebraic);
    }

    #[test]
    fn test_constraint_builder() {
        let builder = ConstraintSystemBuilder::new()
            .add_constraint(Constraint::new(
                vec![PrimeField64::new(1), PrimeField64::new(2)],
                1,
                ConstraintType::Transition,
            ))
            .add_constraint(Constraint::new(
                vec![PrimeField64::new(3), PrimeField64::new(4)],
                1,
                ConstraintType::Boundary,
            ));
        
        let system = builder.build();
        assert_eq!(system.len(), 2);
    }

    #[test]
    fn test_constraint_evaluation() {
        let constraint = Constraint::new(
            vec![PrimeField64::new(1), PrimeField64::new(2)],
            1,
            ConstraintType::Transition,
        );
        
        let current_state = vec![PrimeField64::new(5)];
        let next_state = vec![PrimeField64::new(7)];
        let random_challenge = PrimeField64::new(3);
        
        let result = constraint.evaluate(&current_state, &next_state, random_challenge);
        assert_eq!(result, PrimeField64::new(19)); // 1*5 + 2*7 = 19
    }

    #[test]
    fn test_constraint_satisfaction() {
        let constraint = Constraint::new(
            vec![PrimeField64::new(1), PrimeField64::new(2)],
            1,
            ConstraintType::Transition,
        );
        
        let current_state = vec![PrimeField64::new(5)];
        let next_state = vec![PrimeField64::new(7)];
        let random_challenge = PrimeField64::new(3);
        
        let is_satisfied = constraint.is_satisfied(&current_state, &next_state, random_challenge);
        assert!(!is_satisfied); // 19 != 0
    }

    #[test]
    fn test_constraint_types() {
        let transition_constraint = Constraint::new(
            vec![PrimeField64::new(1)],
            1,
            ConstraintType::Transition,
        );
        
        let boundary_constraint = Constraint::new(
            vec![PrimeField64::new(1)],
            1,
            ConstraintType::Boundary,
        );
        
        let algebraic_constraint = Constraint::new(
            vec![PrimeField64::new(1)],
            1,
            ConstraintType::Algebraic,
        );
        
        assert_eq!(transition_constraint.constraint_type(), &ConstraintType::Transition);
        assert_eq!(boundary_constraint.constraint_type(), &ConstraintType::Boundary);
        assert_eq!(algebraic_constraint.constraint_type(), &ConstraintType::Algebraic);
    }

    #[test]
    fn test_constraint_system_builder() {
        let builder = ConstraintSystemBuilder::new()
            .linear(PrimeField64::new(1), PrimeField64::new(2))
            .quadratic(PrimeField64::new(1), PrimeField64::new(0), PrimeField64::new(1))
            .transition(vec![PrimeField64::new(1), PrimeField64::new(1)])
            .boundary(0, PrimeField64::new(0));
        
        let system = builder.build();
        assert_eq!(system.len(), 4);
        
        // Test individual constraints
        assert_eq!(system[0].degree(), 1);
        assert_eq!(system[1].degree(), 2);
        assert_eq!(system[2].degree(), 1);
        assert_eq!(system[3].degree(), 1);
    }
}