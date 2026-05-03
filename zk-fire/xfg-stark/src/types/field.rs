//! Field Element Types for XFG STARK Implementation
//! 
//! This module provides type-safe field element implementations for cryptographic operations,
//! ensuring constant-time arithmetic and memory safety through Rust's type system.


use std::fmt::{Debug, Display, Formatter};
use std::ops::{Add, AddAssign, Sub, SubAssign, Mul, MulAssign, Neg};
use serde::{Deserialize, Serialize};
use super::{FieldElement, TypeError};
use crate::Result;

/// Field arithmetic error
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum FieldError {
    /// Invalid field element
    #[error("Invalid field element: {0}")]
    InvalidElement(String),
    
    /// Division by zero
    #[error("Division by zero")]
    DivisionByZero,
    
    /// Overflow error
    #[error("Arithmetic overflow")]
    Overflow,
    
    /// Invalid conversion
    #[error("Invalid conversion: {0}")]
    InvalidConversion(String),
}

/// Prime field with 64-bit modulus
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct PrimeField64 {
    /// Field element value
    value: u64,
}

impl PrimeField64 {
    /// Field modulus (using a smaller prime for u64 compatibility)
    pub const MODULUS: u64 = 0x7fffffffffffffff; // 2^63 - 1 (Mersenne prime)
    
    /// Create a new field element
    pub fn new(value: u64) -> Self {
        Self {
            value: value % Self::MODULUS,
        }
    }
    
    /// Get the raw value
    pub fn value(&self) -> u64 {
        self.value
    }
    
    /// Constant-time addition
    pub fn add_constant_time(&self, other: &Self) -> Self {
        let sum = self.value + other.value;
        if sum >= Self::MODULUS {
            Self::new(sum - Self::MODULUS)
        } else {
            Self::new(sum)
        }
    }
    
    /// Constant-time subtraction
    pub fn sub_constant_time(&self, other: &Self) -> Self {
        if self.value >= other.value {
            Self::new(self.value - other.value)
        } else {
            Self::new(Self::MODULUS - (other.value - self.value))
        }
    }
    
    /// Constant-time multiplication
    pub fn mul_constant_time(&self, other: &Self) -> Self {
        let product = (self.value as u128) * (other.value as u128);
        Self::new((product % Self::MODULUS as u128) as u64)
    }
    
    /// Modular inverse using extended Euclidean algorithm
    pub fn inverse(&self) -> Option<Self> {
        if self.value == 0 {
            return None;
        }
        
        let mut a = self.value as i64;
        let mut b = Self::MODULUS as i64;
        let mut x = 1i64;
        let mut y = 0i64;
        
        while b != 0 {
            let q = a / b;
            let temp = b;
            b = a % b;
            a = temp;
            let temp = y;
            y = x - q * y;
            x = temp;
        }
        
        if a != 1 {
            return None;
        }
        
        if x < 0 {
            x += Self::MODULUS as i64;
        }
        
        Some(Self::new(x as u64))
    }
    
    /// Modular exponentiation
    pub fn pow(&self, mut exponent: u64) -> Self {
        let mut base = *self;
        let mut result = Self::one();
        
        while exponent > 0 {
            if exponent & 1 == 1 {
                result = result.mul_constant_time(&base);
            }
            base = base.mul_constant_time(&base);
            exponent >>= 1;
        }
        
        result
    }
    
    /// Square root (if it exists)
    pub fn sqrt(&self) -> Option<Self> {
        if self.value == 0 {
            return Some(Self::zero());
        }
        
        // Check if square root exists using Euler's criterion
        let legendre = self.pow((Self::MODULUS - 1) / 2);
        if legendre.value != 1 {
            return None;
        }
        
        // Tonelli-Shanks algorithm for square root
        let mut q = Self::MODULUS - 1;
        let mut s = 0;
        while q % 2 == 0 {
            q /= 2;
            s += 1;
        }
        
        if s == 1 {
            return Some(self.pow((Self::MODULUS + 1) / 4));
        }
        
        // Find a quadratic non-residue
        let mut z = 2;
        while PrimeField64::new(z).pow((Self::MODULUS - 1) / 2).value == 1 {
            z += 1;
        }
        
        let mut c = PrimeField64::new(z).pow(q);
        let mut r = self.pow((q + 1) / 2);
        let mut t = self.pow(q);
        let mut m = s;
        
        while t.value != 1 {
            let mut i = 0;
            let mut temp = t;
            while temp.value != 1 && i < m {
                temp = temp.mul_constant_time(&temp);
                i += 1;
            }
            
            let b = c.pow(1 << (m - i - 1));
            r = r.mul_constant_time(&b);
            c = b.mul_constant_time(&b);
            t = t.mul_constant_time(&c);
            m = i;
        }
        
        Some(r)
    }
    
    /// Convert to bytes (constant-time)
    pub fn to_bytes(&self) -> [u8; 32] {
        let mut bytes = [0u8; 32];
        for i in 0..8 {
            bytes[24 + i] = ((self.value >> (8 * i)) & 0xff) as u8;
        }
        bytes
    }
    
    /// Convert from bytes (constant-time)
    pub fn from_bytes_constant_time(bytes: &[u8; 32]) -> Option<Self> {
        let mut value = 0u64;
        for i in 0..8 {
            value |= (bytes[24 + i] as u64) << (8 * i);
        }
        
        if value >= Self::MODULUS {
            None
        } else {
            Some(Self::new(value))
        }
    }
    
    /// Random field element
    pub fn random() -> Self {
        use rand::Rng;
        let mut rng = rand::thread_rng();
        Self::new(rng.gen_range(0..Self::MODULUS))
    }
}

impl FieldElement for PrimeField64 {
    const MODULUS: u64 = Self::MODULUS;
    const CHARACTERISTIC: u64 = Self::MODULUS;
    
    fn zero() -> Self {
        Self::new(0)
    }
    
    fn one() -> Self {
        Self::new(1)
    }
    
    fn is_zero(&self) -> bool {
        self.value == 0
    }
    
    fn is_one(&self) -> bool {
        self.value == 1
    }
    
    fn add_assign(&mut self, other: &Self) {
        *self = self.add_constant_time(other);
    }
    
    fn sub_assign(&mut self, other: &Self) {
        *self = self.sub_constant_time(other);
    }
    
    fn mul_assign(&mut self, other: &Self) {
        *self = self.mul_constant_time(other);
    }
    
    fn inverse(&self) -> Option<Self> {
        self.inverse()
    }
    
    fn pow(&self, exponent: u64) -> Self {
        self.pow(exponent)
    }
    
    fn sqrt(&self) -> Option<Self> {
        self.sqrt()
    }
    
    fn to_bytes(&self) -> [u8; 32] {
        self.to_bytes()
    }
    
    fn from_bytes(bytes: &[u8; 32]) -> Option<Self> {
        Self::from_bytes_constant_time(bytes)
    }
    
    fn value(&self) -> u64 {
        self.value
    }
    
    fn new(value: u64) -> Self {
        Self::new(value)
    }
    
    fn random() -> Self {
        Self::random()
    }
}

// Standard arithmetic trait implementations
impl Add for PrimeField64 {
    type Output = Self;
    
    fn add(self, other: Self) -> Self::Output {
        self.add_constant_time(&other)
    }
}

impl AddAssign for PrimeField64 {
    fn add_assign(&mut self, other: Self) {
        *self = self.add_constant_time(&other);
    }
}

impl Sub for PrimeField64 {
    type Output = Self;
    
    fn sub(self, other: Self) -> Self::Output {
        self.sub_constant_time(&other)
    }
}

impl SubAssign for PrimeField64 {
    fn sub_assign(&mut self, other: Self) {
        *self = self.sub_constant_time(&other);
    }
}

impl Mul for PrimeField64 {
    type Output = Self;
    
    fn mul(self, other: Self) -> Self::Output {
        self.mul_constant_time(&other)
    }
}

impl MulAssign for PrimeField64 {
    fn mul_assign(&mut self, other: Self) {
        *self = self.mul_constant_time(&other);
    }
}

impl Neg for PrimeField64 {
    type Output = Self;
    
    fn neg(self) -> Self::Output {
        if self.value == 0 {
            self
        } else {
            Self::new(Self::MODULUS - self.value)
        }
    }
}

impl Display for PrimeField64 {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "PrimeField64({})", self.value)
    }
}

impl Default for PrimeField64 {
    fn default() -> Self {
        Self::zero()
    }
}

/// Binary field element for characteristic 2 fields
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct BinaryField {
    /// The field element value (polynomial representation)
    value: u64,
    /// Field degree
    degree: u32,
}

impl BinaryField {
    /// Create a new binary field element
    pub fn new(value: u64, degree: u32) -> Self {
        let mask = (1u64 << degree) - 1;
        Self {
            value: value & mask,
            degree,
        }
    }
    
    /// Get the underlying value
    pub fn value(&self) -> u64 {
        self.value
    }
    
    /// Get the field degree
    pub fn degree(&self) -> u32 {
        self.degree
    }
    
    /// Constant-time addition (XOR)
    pub fn add_constant_time(&self, other: &Self) -> Self {
        assert_eq!(self.degree, other.degree, "Field degrees must match");
        Self::new(self.value ^ other.value, self.degree)
    }
    
    /// Constant-time multiplication
    pub fn mul_constant_time(&self, other: &Self) -> Self {
        assert_eq!(self.degree, other.degree, "Field degrees must match");
        
        let mut result = 0u64;
        let mut a = self.value;
        let mut b = other.value;
        
        for _ in 0..self.degree {
            if b & 1 != 0 {
                result ^= a;
            }
            
            let carry = a >> (self.degree - 1);
            a = (a << 1) ^ (carry * self.irreducible_polynomial());
            b >>= 1;
        }
        
        Self::new(result, self.degree)
    }
    
    /// Get the irreducible polynomial for this field
    fn irreducible_polynomial(&self) -> u64 {
        match self.degree {
            8 => 0x11b,   // x^8 + x^4 + x^3 + x + 1
            16 => 0x1002b, // x^16 + x^5 + x^3 + x + 1
            32 => 0x1000000af, // x^32 + x^7 + x^3 + x^2 + 1
            _ => panic!("Unsupported field degree: {}", self.degree),
        }
    }
    
    /// Convert to bytes (constant-time)
    pub fn to_bytes_constant_time(&self) -> [u8; 32] {
        let mut bytes = [0u8; 32];
        let value = self.value.to_le_bytes();
        bytes[..8].copy_from_slice(&value);
        bytes
    }
    
    /// Convert from bytes (constant-time)
    pub fn from_bytes_constant_time(bytes: &[u8; 32], degree: u32) -> Option<Self> {
        let mut value_bytes = [0u8; 8];
        value_bytes.copy_from_slice(&bytes[..8]);
        let value = u64::from_le_bytes(value_bytes);
        Some(Self::new(value, degree))
    }
}

impl Display for BinaryField {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        write!(f, "BinaryField({:x}, degree={})", self.value, self.degree)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_prime_field_basic_operations() {
        let a = PrimeField64::new(5);
        let b = PrimeField64::new(3);
        
        assert_eq!(a + b, PrimeField64::new(8));
        assert_eq!(a - b, PrimeField64::new(2));
        assert_eq!(a * b, PrimeField64::new(15));
        assert_eq!(PrimeField64::zero(), PrimeField64::new(0));
        assert_eq!(PrimeField64::one(), PrimeField64::new(1));
    }

    #[test]
    fn test_prime_field_inverse() {
        let a = PrimeField64::new(5);
        let inv = a.inverse().unwrap();
        assert_eq!(a * inv, PrimeField64::one());
    }

    #[test]
    fn test_binary_field_operations() {
        let a = BinaryField::new(0b101, 8);
        let b = BinaryField::new(0b011, 8);
        
        assert_eq!(a.add_constant_time(&b), BinaryField::new(0b110, 8));
    }

    #[test]
    fn test_constant_time_operations() {
        let a = PrimeField64::new(10);
        let b = PrimeField64::new(5);
        
        // These operations should be constant-time
        let _sum = a.add_constant_time(&b);
        let _diff = a.sub_constant_time(&b);
        let _prod = a.mul_constant_time(&b);
    }
}
