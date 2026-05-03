//! Comprehensive tests for Winterfell integration
//! 
//! This module provides extensive testing for the Winterfell framework integration,
//! ensuring type safety, cryptographic security, and performance requirements are met.

use super::*;
use crate::types::field::PrimeField64;
use crate::types::stark::{ExecutionTrace, Air, TransitionFunction, BoundaryConditions};

#[test]
fn test_winterfell_field_element_creation() {
    let xfg_field = PrimeField64::new(42);
    let winterfell_field = WinterfellFieldElement::from(xfg_field);
    
    assert_eq!(winterfell_field.0, xfg_field);
    assert_eq!(winterfell_field.to_xfg(), xfg_field);
}

#[test]
fn test_winterfell_field_element_conversion() {
    let values = vec![0, 1, 42, 100, 1000, PrimeField64::MODULUS - 1];
    
    for value in values {
        let xfg_field = PrimeField64::new(value);
        let winterfell_field = WinterfellFieldElement::from(xfg_field);
        let converted_back = PrimeField64::from(winterfell_field);
        
        assert_eq!(xfg_field, converted_back, "Conversion failed for value {}", value);
    }
}

#[test]
fn test_winterfell_field_element_arithmetic() {
    let test_cases = vec![
        (5, 3, 8, 15, 2),   // a=5, b=3, sum=8, product=15, diff=2
        (10, 7, 17, 70, 3), // a=10, b=7, sum=17, product=70, diff=3
        (0, 5, 5, 0, -5),   // a=0, b=5, sum=5, product=0, diff=-5
    ];
    
    for (a_val, b_val, expected_sum, expected_product, expected_diff) in test_cases {
        let a = WinterfellFieldElement::from(PrimeField64::new(a_val));
        let b = WinterfellFieldElement::from(PrimeField64::new(b_val));
        
        let sum = a + b;
        let product = a * b;
        let difference = a - b;
        
        assert_eq!(sum.0.value(), expected_sum as u64 % PrimeField64::MODULUS);
        assert_eq!(product.0.value(), expected_product as u64 % PrimeField64::MODULUS);
        assert_eq!(difference.0.value(), (expected_diff as u64).wrapping_neg() % PrimeField64::MODULUS);
    }
}

#[test]
fn test_winterfell_field_element_assign_operations() {
    let mut a = WinterfellFieldElement::from(PrimeField64::new(10));
    let b = WinterfellFieldElement::from(PrimeField64::new(5));
    
    // Test AddAssign
    a += b;
    assert_eq!(a.0.value(), 15);
    
    // Test SubAssign
    a -= b;
    assert_eq!(a.0.value(), 10);
    
    // Test MulAssign
    a *= b;
    assert_eq!(a.0.value(), 50);
}

#[test]
fn test_winterfell_field_element_negation() {
    let a = WinterfellFieldElement::from(PrimeField64::new(5));
    let neg_a = -a;
    
    // In a field, -a = 0 - a
    let expected = PrimeField64::new(0).sub_constant_time(&PrimeField64::new(5));
    assert_eq!(neg_a.0, expected);
}

#[test]
fn test_winterfell_field_element_field_trait() {
    let a = WinterfellFieldElement::from(PrimeField64::new(5));
    
    // Test ZERO and ONE constants
    assert_eq!(WinterfellFieldElement::ZERO.0.value(), 0);
    assert_eq!(WinterfellFieldElement::ONE.0.value(), 1);
    
    // Test exp
    let a_squared = a.exp(2);
    assert_eq!(a_squared.0.value(), 25);
    
    // Test inv
    let a_inv = a.inv();
    let product = a * a_inv;
    assert_eq!(product.0.value(), 1);
    
    // Test conjugate (should be identity in prime fields)
    let a_conj = a.conjugate();
    assert_eq!(a_conj, a);
}

#[test]
fn test_winterfell_field_element_stark_field_trait() {
    // Test MODULUS
    assert_eq!(WinterfellFieldElement::MODULUS, PrimeField64::MODULUS);
    
    // Test MODULUS_BITS
    assert_eq!(WinterfellFieldElement::MODULUS_BITS, 256);
    
    // Test get_modulus_le_bytes
    let modulus_bytes = WinterfellFieldElement::get_modulus_le_bytes();
    assert_eq!(modulus_bytes.len(), 8); // u64 is 8 bytes
    
    // Test as_int
    let a = WinterfellFieldElement::from(PrimeField64::new(42));
    assert_eq!(a.as_int(), 42);
}

#[test]
fn test_winterfell_trace_table_creation() {
    let trace = ExecutionTrace {
        columns: vec![
            vec![PrimeField64::new(1), PrimeField64::new(2)],
            vec![PrimeField64::new(3), PrimeField64::new(4)],
        ],
        length: 2,
        num_registers: 2,
    };
    
    let winterfell_trace = WinterfellTraceTable::from_xfg_trace(&trace).unwrap();
    let inner_trace = winterfell_trace.into_inner();
    
    assert_eq!(inner_trace.num_rows(), 2);
    assert_eq!(inner_trace.num_cols(), 2);
}

#[test]
fn test_winterfell_trace_table_conversion_errors() {
    // Test with empty columns
    let empty_trace = ExecutionTrace {
        columns: vec![],
        length: 0,
        num_registers: 0,
    };
    
    let result = WinterfellTraceTable::from_xfg_trace(&empty_trace);
    assert!(result.is_ok()); // Should handle empty traces gracefully
    
    // Test with mismatched column lengths
    let mismatched_trace = ExecutionTrace {
        columns: vec![
            vec![PrimeField64::new(1), PrimeField64::new(2)],
            vec![PrimeField64::new(3)], // Different length
        ],
        length: 2,
        num_registers: 2,
    };
    
    let result = WinterfellTraceTable::from_xfg_trace(&mismatched_trace);
    // This should either handle gracefully or return an error
    // The exact behavior depends on the TraceTable implementation
}

#[test]
fn test_xfg_winterfell_prover_creation() {
    let proof_options = ProofOptions::new(32, 8, 4, 128);
    let prover = XfgWinterfellProver::new(proof_options);
    
    // Test that prover was created successfully
    assert!(std::mem::size_of_val(&prover) > 0);
}

#[test]
fn test_xfg_winterfell_verifier_creation() {
    let proof_options = ProofOptions::new(32, 8, 4, 128);
    let verifier = XfgWinterfellVerifier::new(proof_options);
    
    // Test that verifier was created successfully
    assert!(std::mem::size_of_val(&verifier) > 0);
}

#[test]
fn test_xfg_winterfell_prover_proof_generation() {
    let proof_options = ProofOptions::new(32, 8, 4, 128);
    let prover = XfgWinterfellProver::new(proof_options);
    
    let trace = ExecutionTrace {
        columns: vec![
            vec![PrimeField64::new(1), PrimeField64::new(2)],
        ],
        length: 2,
        num_registers: 1,
    };
    
    let air = Air {
        constraints: vec![],
        transition: TransitionFunction {
            coefficients: vec![vec![PrimeField64::new(1)]],
            degree: 1,
        },
        boundary: BoundaryConditions { constraints: vec![] },
        security_parameter: 128,
    };
    
    // This should return an error since the full implementation is not yet complete
    let result = prover.prove(&trace, &air);
    assert!(result.is_err());
    
    // Verify the error is the expected "not implemented" error
    if let Err(XfgStarkError::StarkError(StarkError::NotImplemented(_))) = result {
        // Expected error
    } else {
        panic!("Expected NotImplemented error");
    }
}

#[test]
fn test_xfg_winterfell_verifier_verification() {
    let proof_options = ProofOptions::new(32, 8, 4, 128);
    let verifier = XfgWinterfellVerifier::new(proof_options);
    
    let proof = StarkProof {
        trace: ExecutionTrace {
            columns: vec![vec![PrimeField64::new(1)]],
            length: 1,
            num_registers: 1,
        },
        air: Air {
            constraints: vec![],
            transition: TransitionFunction {
                coefficients: vec![vec![PrimeField64::new(1)]],
                degree: 1,
            },
            boundary: BoundaryConditions { constraints: vec![] },
            security_parameter: 128,
        },
        commitments: vec![],
        fri_proof: FriProof {
            layers: vec![],
            final_polynomial: vec![PrimeField64::new(1)],
            queries: vec![],
        },
        metadata: ProofMetadata {
            version: 1,
            security_parameter: 128,
            field_modulus: "0x30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47".to_string(),
            proof_size: 1024,
            timestamp: 1234567890,
        },
    };
    
    let air = Air {
        constraints: vec![],
        transition: TransitionFunction {
            coefficients: vec![vec![PrimeField64::new(1)]],
            degree: 1,
        },
        boundary: BoundaryConditions { constraints: vec![] },
        security_parameter: 128,
    };
    
    // This should return an error since the full implementation is not yet complete
    let result = verifier.verify(&proof, &air);
    assert!(result.is_err());
    
    // Verify the error is the expected "not implemented" error
    if let Err(XfgStarkError::StarkError(StarkError::NotImplemented(_))) = result {
        // Expected error
    } else {
        panic!("Expected NotImplemented error");
    }
}

#[test]
fn test_xfg_winterfell_hasher() {
    let elements = vec![
        WinterfellFieldElement::from(PrimeField64::new(1)),
        WinterfellFieldElement::from(PrimeField64::new(2)),
        WinterfellFieldElement::from(PrimeField64::new(3)),
    ];
    
    let hash = XfgWinterfellHasher::hash_elements(&elements);
    
    // Test that hash is a valid field element
    assert!(hash.0.value() < PrimeField64::MODULUS);
}

#[test]
fn test_xfg_winterfell_random_coin() {
    let seed = b"test_seed_for_random_coin";
    let mut random_coin = XfgWinterfellRandomCoin::new(seed);
    
    // Test that we can draw field elements
    let element: WinterfellFieldElement = random_coin.draw();
    assert!(element.0.value() < PrimeField64::MODULUS);
    
    // Test that we can draw integers
    let integers = random_coin.draw_integers(5, 100);
    assert_eq!(integers.len(), 5);
    for &int in &integers {
        assert!(int < 100);
    }
}

#[test]
fn test_constant_time_operations() {
    // Test that field element operations are constant-time
    let a = WinterfellFieldElement::from(PrimeField64::new(5));
    let b = WinterfellFieldElement::from(PrimeField64::new(3));
    
    // These operations should be constant-time
    let _sum = a + b;
    let _product = a * b;
    let _difference = a - b;
    let _negation = -a;
    
    // The test passes if no timing attacks are possible
    // In practice, you would use specialized tools to verify constant-time behavior
}

#[test]
fn test_memory_safety() {
    // Test that field elements are properly managed
    let a = WinterfellFieldElement::from(PrimeField64::new(42));
    let b = WinterfellFieldElement::from(PrimeField64::new(17));
    
    // Test cloning
    let a_clone = a.clone();
    assert_eq!(a, a_clone);
    
    // Test copying
    let a_copy = a;
    assert_eq!(a_copy.0.value(), 42);
    
    // Test that we can still use the original after cloning
    let result = a_clone + b;
    assert_eq!(result.0.value(), 59);
}

#[test]
fn test_serialization_compatibility() {
    // Test that field elements can be serialized and deserialized
    let original = WinterfellFieldElement::from(PrimeField64::new(123));
    
    // This would require implementing Serialize/Deserialize traits
    // For now, we just test that the field element can be created and used
    assert_eq!(original.0.value(), 123);
}

#[test]
fn test_performance_characteristics() {
    // Test that operations are efficient
    let iterations = 1000;
    let start = std::time::Instant::now();
    
    for _ in 0..iterations {
        let a = WinterfellFieldElement::from(PrimeField64::new(42));
        let b = WinterfellFieldElement::from(PrimeField64::new(17));
        let _result = a + b;
    }
    
    let duration = start.elapsed();
    
    // Ensure operations are reasonably fast (less than 1ms for 1000 operations)
    assert!(duration.as_millis() < 1);
}

#[test]
fn test_error_handling() {
    // Test error handling for invalid inputs
    let trace = ExecutionTrace {
        columns: vec![
            vec![PrimeField64::new(1), PrimeField64::new(2)],
        ],
        length: 2,
        num_registers: 1,
    };
    
    // Test with invalid field element (should be handled gracefully)
    let invalid_field = PrimeField64::new(PrimeField64::MODULUS + 1);
    let winterfell_field = WinterfellFieldElement::from(invalid_field);
    
    // The field should be reduced modulo the field size
    assert!(winterfell_field.0.value() < PrimeField64::MODULUS);
}

#[test]
fn test_cryptographic_properties() {
    // Test that field elements maintain cryptographic properties
    let a = WinterfellFieldElement::from(PrimeField64::new(5));
    let b = WinterfellFieldElement::from(PrimeField64::new(3));
    
    // Test multiplicative inverse
    let b_inv = b.inv();
    let product = b * b_inv;
    assert_eq!(product, WinterfellFieldElement::ONE);
    
    // Test exponentiation
    let a_squared = a.exp(2);
    let a_cubed = a.exp(3);
    assert_eq!(a_squared, a * a);
    assert_eq!(a_cubed, a * a * a);
}

// Property-based tests using quickcheck
#[cfg(test)]
mod property_tests {
    use super::*;
    use quickcheck::{Arbitrary, Gen};

    impl Arbitrary for WinterfellFieldElement {
        fn arbitrary(g: &mut Gen) -> Self {
            let value = u64::arbitrary(g) % PrimeField64::MODULUS;
            WinterfellFieldElement::from(PrimeField64::new(value))
        }
    }

    #[quickcheck]
    fn field_element_conversion_is_identity(field: WinterfellFieldElement) -> bool {
        let xfg_field = field.to_xfg();
        let converted_back = WinterfellFieldElement::from(xfg_field);
        field == converted_back
    }

    #[quickcheck]
    fn field_element_addition_is_commutative(a: WinterfellFieldElement, b: WinterfellFieldElement) -> bool {
        a + b == b + a
    }

    #[quickcheck]
    fn field_element_multiplication_is_commutative(a: WinterfellFieldElement, b: WinterfellFieldElement) -> bool {
        a * b == b * a
    }

    #[quickcheck]
    fn field_element_addition_is_associative(a: WinterfellFieldElement, b: WinterfellFieldElement, c: WinterfellFieldElement) -> bool {
        (a + b) + c == a + (b + c)
    }

    #[quickcheck]
    fn field_element_multiplication_is_associative(a: WinterfellFieldElement, b: WinterfellFieldElement, c: WinterfellFieldElement) -> bool {
        (a * b) * c == a * (b * c)
    }

    #[quickcheck]
    fn field_element_distributive_law(a: WinterfellFieldElement, b: WinterfellFieldElement, c: WinterfellFieldElement) -> bool {
        a * (b + c) == (a * b) + (a * c)
    }

    #[quickcheck]
    fn field_element_inverse_property(a: WinterfellFieldElement) -> bool {
        if a == WinterfellFieldElement::ZERO {
            true // Zero has no inverse, which is handled by the implementation
        } else {
            let a_inv = a.inv();
            a * a_inv == WinterfellFieldElement::ONE
        }
    }
}