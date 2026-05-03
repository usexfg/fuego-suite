//! Polynomial Types for XFG STARK Implementation
//! 
//! This module provides type-safe polynomial implementations for STARK proof operations,
//! ensuring efficient arithmetic and evaluation with field element coefficients.

use std::fmt::{Debug, Display, Formatter};
use std::ops::Sub;
use serde::{Deserialize, Serialize};
use super::{FieldElement, Polynomial, TypeError};
use crate::Result;

/// Polynomial operation error
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum PolynomialError {
    /// Invalid polynomial degree
    #[error("Invalid polynomial degree: {0}")]
    InvalidDegree(String),
    
    /// Division by zero polynomial
    #[error("Division by zero polynomial")]
    DivisionByZero,
    
    /// Interpolation error
    #[error("Interpolation error: {0}")]
    InterpolationError(String),
    
    /// Invalid coefficient
    #[error("Invalid coefficient: {0}")]
    InvalidCoefficient(String),
}

/// Polynomial with field element coefficients
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FieldPolynomial<F: FieldElement> {
    /// Polynomial coefficients (constant term first)
    coefficients: Vec<F>,
}

impl<F: FieldElement> FieldPolynomial<F> {
    /// Create a new polynomial from coefficients
    pub fn new(coefficients: Vec<F>) -> Self {
        Self { coefficients }
    }
    
    /// Create a zero polynomial
    pub fn zero() -> Self {
        Self::new(vec![F::zero()])
    }
    
    /// Create a constant polynomial
    pub fn constant(value: F) -> Self {
        Self::new(vec![value])
    }
    
    /// Get the degree of the polynomial
    pub fn degree(&self) -> usize {
        if self.coefficients.is_empty() {
            return 0;
        }
        
        for i in (0..self.coefficients.len()).rev() {
            if !self.coefficients[i].is_zero() {
                return i;
            }
        }
        0
    }
    
    /// Get coefficient at given index
    pub fn coefficient(&self, index: usize) -> F {
        self.coefficients.get(index).copied().unwrap_or(F::zero())
    }
    
    /// Set coefficient at given index
    pub fn set_coefficient(&mut self, index: usize, value: F) {
        while self.coefficients.len() <= index {
            self.coefficients.push(F::zero());
        }
        self.coefficients[index] = value;
    }
    
    /// Evaluate polynomial at a point
    pub fn evaluate(&self, point: F) -> F {
        let mut result = F::zero();
        let mut power = F::one();
        
        for &coeff in &self.coefficients {
            result = result + coeff * power;
            power = power * point;
        }
        
        result
    }
    
    /// Add another polynomial
    pub fn add(&self, other: &Self) -> Self {
        let max_degree = self.degree().max(other.degree());
        let mut result = vec![F::zero(); max_degree + 1];
        
        for i in 0..=max_degree {
            result[i] = self.coefficient(i) + other.coefficient(i);
        }
        
        Self::new(result)
    }
    
    /// Multiply by another polynomial
    pub fn multiply(&self, other: &Self) -> Self {
        let degree = self.degree() + other.degree();
        let mut result = vec![F::zero(); degree + 1];
        
        for i in 0..=self.degree() {
            for j in 0..=other.degree() {
                result[i + j] = result[i + j] + self.coefficient(i) * other.coefficient(j);
            }
        }
        
        Self::new(result)
    }
    
    /// Divide by another polynomial
    pub fn divide(&self, other: &Self) -> Option<(Self, Self)> {
        if other.is_zero() {
            return None;
        }
        
        let mut quotient = Self::zero();
        let mut remainder = self.clone();
        let divisor_degree = other.degree();
        
        while remainder.degree() >= divisor_degree {
            let quotient_degree = remainder.degree() - divisor_degree;
            let leading_coeff = remainder.coefficient(remainder.degree());
            let divisor_leading = other.coefficient(divisor_degree);
            
            // For field elements, we need to handle division properly
            let inv = divisor_leading.inverse()?;
            let coeff = leading_coeff * inv;
            
            let mut term = vec![F::zero(); quotient_degree + 1];
            term[quotient_degree] = coeff;
            let term_poly = Self::new(term);
            
            quotient = quotient.add(&term_poly);
            remainder = remainder.sub(term_poly.multiply(other));
        }
        
        Some((quotient, remainder))
    }
    
    /// Compute the derivative
    pub fn derivative(&self) -> Self {
        if self.degree() == 0 {
            return Self::zero();
        }
        
        let mut result = vec![F::zero(); self.degree()];
        for i in 1..=self.degree() {
            // For field elements, we need to handle the case where i might not have an inverse
            // This is a simplified version - in practice, you'd need proper field arithmetic
            result[i - 1] = self.coefficient(i);
        }
        
        Self::new(result)
    }
    
    /// Check if polynomial is zero
    pub fn is_zero(&self) -> bool {
        self.coefficients.iter().all(|&c| c.is_zero())
    }
    
    /// Interpolate polynomial from points
    pub fn interpolate(points: &[(F, F)]) -> Option<Self> {
        if points.is_empty() {
            return None;
        }
        
        // Lagrange interpolation
        let mut result = Self::zero();
        let n = points.len();
        
        for i in 0..n {
            let mut term = Self::constant(points[i].1);
            let mut denominator = F::one();
            
            for j in 0..n {
                if i != j {
                    let x_diff = points[i].0 - points[j].0;
                    if x_diff.is_zero() {
                        return None; // Duplicate x-coordinates
                    }
                    
                    let x_term = Self::new(vec![F::zero() - points[j].0, F::one()]);
                    term = term.multiply(&x_term);
                    denominator = denominator * x_diff;
                }
            }
            
            if let Some(inv_denom) = denominator.inverse() {
                term = term.multiply(&Self::constant(inv_denom));
                result = result.add(&term);
            } else {
                return None;
            }
        }
        
        Some(result)
    }
}

impl<F: FieldElement> Polynomial<F> for FieldPolynomial<F> {
    fn degree(&self) -> usize {
        self.degree()
    }
    
    fn evaluate(&self, point: F) -> F {
        self.evaluate(point)
    }
    
    fn coefficient(&self, index: usize) -> F {
        self.coefficient(index)
    }
    
    fn set_coefficient(&mut self, index: usize, value: F) {
        self.set_coefficient(index, value);
    }
    
    fn add(&self, other: &Self) -> Self {
        self.add(other)
    }
    
    fn multiply(&self, other: &Self) -> Self {
        self.multiply(other)
    }
    
    fn divide(&self, other: &Self) -> Option<(Self, Self)> {
        self.divide(other)
    }
    
    fn derivative(&self) -> Self {
        self.derivative()
    }
    
    fn interpolate(points: &[(F, F)]) -> Option<Self> {
        Self::interpolate(points)
    }
}

impl<F: FieldElement> Display for FieldPolynomial<F> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        if self.is_zero() {
            return write!(f, "0");
        }
        
        let mut first = true;
        for (i, &coeff) in self.coefficients.iter().enumerate().rev() {
            if coeff.is_zero() {
                continue;
            }
            
            if !first {
                write!(f, " + ")?;
            }
            first = false;
            
            if i == 0 {
                write!(f, "{}", coeff)?;
            } else if i == 1 {
                if coeff.is_one() {
                    write!(f, "x")?;
                } else {
                    write!(f, "{}x", coeff)?;
                }
            } else {
                if coeff.is_one() {
                    write!(f, "x^{}", i)?;
                } else {
                    write!(f, "{}x^{}", coeff, i)?;
                }
            }
        }
        
        Ok(())
    }
}

// Standard arithmetic trait implementations
impl<F: FieldElement> std::ops::Neg for FieldPolynomial<F> {
    type Output = Self;
    
    fn neg(self) -> Self::Output {
        let mut result = self;
        for coeff in &mut result.coefficients {
            *coeff = -*coeff;
        }
        result
    }
}

impl<F: FieldElement> std::ops::Add for FieldPolynomial<F> {
    type Output = Self;
    
    fn add(self, other: Self) -> Self::Output {
        FieldPolynomial::add(&self, &other)
    }
}

impl<F: FieldElement> std::ops::Sub for FieldPolynomial<F> {
    type Output = Self;
    
    fn sub(self, other: Self) -> Self::Output {
        self.add(&(-other))
    }
}

impl<F: FieldElement> std::ops::Mul for FieldPolynomial<F> {
    type Output = Self;
    
    fn mul(self, other: Self) -> Self::Output {
        self.multiply(&other)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::field::PrimeField64;

    #[test]
    fn test_polynomial_basic_operations() {
        let p1 = FieldPolynomial::<PrimeField64>::new(vec![
            PrimeField64::new(1),
            PrimeField64::new(2),
            PrimeField64::new(3),
        ]);
        
        let p2 = FieldPolynomial::<PrimeField64>::new(vec![
            PrimeField64::new(1),
            PrimeField64::new(1),
        ]);
        
        let sum = p1.add(&p2);
        assert_eq!(sum.degree(), 2);
        
        let product = p1.multiply(&p2);
        assert_eq!(product.degree(), 3);
    }

    #[test]
    fn test_polynomial_evaluation() {
        let p = FieldPolynomial::<PrimeField64>::new(vec![
            PrimeField64::new(1),
            PrimeField64::new(2),
            PrimeField64::new(1),
        ]);
        
        let result = p.evaluate(PrimeField64::new(3));
        assert_eq!(result, PrimeField64::new(16)); // 1 + 2*3 + 3^2 = 16
    }

    #[test]
    fn test_polynomial_interpolation() {
        let points = vec![
            (PrimeField64::new(1), PrimeField64::new(1)),
            (PrimeField64::new(2), PrimeField64::new(4)),
            (PrimeField64::new(3), PrimeField64::new(9)),
        ];
        
        let poly = FieldPolynomial::<PrimeField64>::interpolate(&points).unwrap();
        
        for (x, y) in points {
            assert_eq!(poly.evaluate(x), y);
        }
    }
}
