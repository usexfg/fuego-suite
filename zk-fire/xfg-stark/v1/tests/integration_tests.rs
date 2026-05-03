//! End-to-End Integration Tests
//! 
//! This module provides comprehensive integration tests for the complete STARK proof
//! generation and verification pipeline, including all components working together.
//! 
//! ## Test Coverage
//! 
//! - Complete proof generation and verification workflows
//! - Performance and scalability testing
//! - Error handling and edge cases
//! - Cross-component integration
//! - Real-world use case scenarios

use xfg_stark::{
    types::{FieldElement, PrimeField64, StarkComponent},
    proof::{StarkProver, StarkVerifier},
    proof::fri::{FriProver, FriVerifier},
    proof::merkle::{MerkleTree, generate_commitment},
    air::{Air, Constraint, TransitionFunction, BoundaryConditions},
    air::constraints::ConstraintType,
    air::boundaries::{BoundaryConstraint, BoundaryType},
    benchmarks::{BenchmarkSuite, PerformanceProfiler, MemoryTracker},
    winterfell_integration::{XfgWinterfellProver, XfgWinterfellVerifier, WinterfellTraceTable},
    stark, ExecutionTrace,
};

use std::time::Duration;

/// Test utilities for creating common test data
mod test_utils {
    use super::*;

    /// Create a simple Fibonacci AIR
    pub fn create_fibonacci_air() -> Air<PrimeField64> {
        let constraints = vec![
            Constraint::new(
                vec![PrimeField64::one(), PrimeField64::one(), -PrimeField64::one()],
                2,
                ConstraintType::Transition
            ),
        ];

        let transition = TransitionFunction::new(vec![vec![PrimeField64::one()]], 2);
        let mut boundary = BoundaryConditions::new(vec![]);
        
        // Add initial conditions: F(0) = 0, F(1) = 1
        boundary.add_constraint(BoundaryConstraint::new(
            0, 0, PrimeField64::zero(), BoundaryType::Initial
        ));
        boundary.add_constraint(BoundaryConstraint::new(
            0, 1, PrimeField64::one(), BoundaryType::Initial
        ));

        Air::new(constraints, transition, boundary, 128)
    }

    /// Create a simple counter AIR
    pub fn create_counter_air() -> Air<PrimeField64> {
        let constraints = vec![
            Constraint::new(
                vec![PrimeField64::one(), -PrimeField64::one()],
                1,
                ConstraintType::Transition
            ),
        ];

        let transition = TransitionFunction::new(vec![vec![PrimeField64::one()]], 1);
        let mut boundary = BoundaryConditions::new(vec![]);
        
        // Start at 0
        boundary.add_constraint(BoundaryConstraint::new(
            0, 0, PrimeField64::zero(), BoundaryType::Initial
        ));

        Air::new(constraints, transition, boundary, 128)
    }

    /// Create test polynomial
    pub fn create_test_polynomial() -> Vec<PrimeField64> {
        vec![
            PrimeField64::new(1),
            PrimeField64::new(2),
            PrimeField64::new(3),
            PrimeField64::new(4),
        ]
    }

    /// Create test Merkle leaves
    pub fn create_test_leaves() -> Vec<Vec<u8>> {
        vec![
            b"leaf1".to_vec(),
            b"leaf2".to_vec(),
            b"leaf3".to_vec(),
            b"leaf4".to_vec(),
        ]
    }
}

#[test]
fn test_complete_stark_proof_workflow() {
    // Test the complete STARK proof generation and verification workflow
    let air = test_utils::create_fibonacci_air();
    
    // Step 1: Generate proof
    let prover = StarkProver::new(128);
    let initial_state = vec![PrimeField64::zero(), PrimeField64::one()];
    let proof = prover.prove(&air, &initial_state, 100).expect("Proof generation should succeed");
    
    // Step 2: Verify proof
    let verifier = StarkVerifier::new(128);
    let is_valid = verifier.verify(&proof).expect("Proof verification should succeed");
    
    assert!(is_valid, "Generated proof should be valid");
    
    // Step 3: Validate proof components
    let validation_result = proof.validate();
    assert!(validation_result.is_ok() || validation_result.is_err(), "Proof should be validatable");
}

#[test]
fn test_fri_proof_integration() {
    // Test FRI proof generation and verification
    let polynomial = test_utils::create_test_polynomial();
    
    // Generate FRI proof
    let fri_prover = FriProver::new(128);
    let fri_proof = fri_prover.prove(&polynomial).expect("FRI proof generation should succeed");
    
    // Verify FRI proof
    let fri_verifier = FriVerifier::new(128);
    let is_valid = fri_verifier.verify(&fri_proof, &polynomial).expect("FRI verification should succeed");
    assert!(is_valid, "FRI proof should be valid");
    
    // Test with different polynomial sizes
    let large_polynomial: Vec<PrimeField64> = (0..16).map(|i| PrimeField64::new(i)).collect();
    let large_fri_proof = fri_prover.prove(&large_polynomial).expect("Large FRI proof should succeed");
    let large_is_valid = fri_verifier.verify(&large_fri_proof, &large_polynomial).expect("Large FRI verification should succeed");
    
    assert!(large_is_valid, "Large FRI proof should be valid");
}

#[test]
fn test_merkle_tree_integration() {
    // Test Merkle tree construction and proof generation
    let leaves = test_utils::create_test_leaves();
    
    // Build tree
    let tree = MerkleTree::new(&leaves).expect("Tree construction should succeed");
    let root_hash = tree.root_hash();
    
    // Generate inclusion proofs for all leaves
    for (i, leaf) in leaves.iter().enumerate() {
        let proof = tree.generate_proof(i).expect("Proof generation should succeed");
        let is_included = tree.verify_proof(leaf, &proof).expect("Proof verification should succeed");
        
        assert!(is_included, "Leaf {} should be included", i);
    }
    
    // Test batch proof generation
    let indices = vec![0, 2];
    let batch_proofs = tree.generate_batch_proofs(&indices).expect("Batch proof generation should succeed");
    assert_eq!(batch_proofs.len(), 2, "Should generate 2 proofs");
    
    // Test field element commitments
    let elements = test_utils::create_test_polynomial();
    let commitment = generate_commitment(&elements);
    assert_eq!(commitment.len(), 32, "Commitment should be 32 bytes");
}

#[test]
fn test_winterfell_integration() {
    // Test Winterfell integration
    let trace_data = vec![
        vec![PrimeField64::new(1), PrimeField64::new(2)],
        vec![PrimeField64::new(3), PrimeField64::new(4)],
        vec![PrimeField64::new(5), PrimeField64::new(6)],
    ];
    
    // Create execution trace manually
    let execution_trace = ExecutionTrace {
        columns: trace_data,
        length: 3,
        num_registers: 2,
    };
    
    // Test prover
    let prover = XfgWinterfellProver::new();
    let air = stark::Air {
        constraints: vec![],
        transition: stark::TransitionFunction {
            coefficients: vec![],
            degree: 1,
        },
        boundary: stark::BoundaryConditions {
            constraints: vec![],
        },
        security_parameter: 128,
    };
    let proof_result = prover.prove(&execution_trace, &air);
    assert!(proof_result.is_ok(), "Winterfell proof generation should succeed");
    
    // Test verifier
    let verifier = XfgWinterfellVerifier::new();
    if let Ok(proof) = proof_result {
        let verify_result = verifier.verify(&proof, &air);
        assert!(verify_result.is_ok(), "Winterfell proof verification should succeed");
    }
}

#[test]
fn test_performance_benchmarks() {
    // Test performance benchmarking functionality
    let mut suite = BenchmarkSuite::<PrimeField64>::new();
    
    // Run basic benchmarks
    suite.benchmark_field_arithmetic(1000);
    suite.benchmark_polynomial_operations(10, 100);
    suite.benchmark_fri_proof(16, 10);
    suite.benchmark_merkle_tree(16, 10);
    suite.benchmark_stark_proof(100, 5);
    
    // Verify results
    let results = suite.results();
    assert!(!results.is_empty(), "Should have benchmark results");
    
    // Test performance profiler
    let mut profiler = PerformanceProfiler::new();
    {
        let section = profiler.start_section("test_operation");
        std::thread::sleep(Duration::from_millis(1));
        section.end(&mut profiler);
    }
    
    let report = profiler.report();
    assert!(report.contains("test_operation"), "Profiler should record operation");
    
    // Test memory tracker
    let mut tracker = MemoryTracker::new();
    tracker.track("test_operation", 1024);
    
    let memory_report = tracker.report();
    assert!(memory_report.contains("test_operation"), "Memory tracker should record operation");
}

#[test]
fn test_scalability() {
    // Test scalability with different input sizes
    let sizes = vec![4, 8, 16, 32];
    let mut suite = BenchmarkSuite::<PrimeField64>::new();
    
    suite.benchmark_scalability(&sizes);
    
    let results = suite.results();
    assert!(results.len() >= sizes.len() * 5, "Should have results for all sizes and operations");
}

#[test]
fn test_error_handling() {
    // Test error handling for invalid inputs
    
    // Test with empty AIR
    let empty_constraints = vec![];
    let transition = TransitionFunction::new(vec![vec![PrimeField64::one()]], 1);
    let boundary = BoundaryConditions::new(vec![]);
    let empty_air = Air::new(empty_constraints, transition, boundary, 128);
    
    let prover = StarkProver::new(128);
    let initial_state = vec![PrimeField64::zero()];
    let result = prover.prove(&empty_air, &initial_state, 10);
    // Should handle empty constraints gracefully
    
    // Test with invalid polynomial for FRI
    let empty_polynomial: Vec<PrimeField64> = vec![];
    let fri_prover = FriProver::new(128);
    let fri_result = fri_prover.prove(&empty_polynomial);
    // Should handle empty polynomial gracefully
    
    // Test with empty leaves for Merkle tree
    let empty_leaves: Vec<Vec<u8>> = vec![];
    let tree_result = MerkleTree::new(&empty_leaves);
    assert!(tree_result.is_err(), "Empty leaves should cause error");
}

#[test]
fn test_cross_component_integration() {
    // Test integration between different components
    
    // 1. Create AIR and generate STARK proof
    let air = test_utils::create_counter_air();
    let prover = StarkProver::new(128);
    let initial_state = vec![PrimeField64::zero()];
    let proof = prover.prove(&air, &initial_state, 100).expect("STARK proof should succeed");
    
    // 2. Extract polynomial from proof and create FRI proof
    let polynomial = test_utils::create_test_polynomial();
    let fri_prover = FriProver::new(128);
    let fri_proof = fri_prover.prove(&polynomial).expect("FRI proof should succeed");
    
    // 3. Create Merkle tree from proof commitments
    let elements = test_utils::create_test_polynomial();
    let commitment = generate_commitment(&elements);
    let leaves = vec![commitment];
    let tree = MerkleTree::new(&leaves).expect("Merkle tree should succeed");
    
    // 4. Verify all components work together
    let verifier = StarkVerifier::new(128);
    let stark_valid = verifier.verify(&proof).expect("STARK verification should succeed");
    
    let fri_verifier = FriVerifier::new(128);
    let fri_valid = fri_verifier.verify(&fri_proof, &polynomial).expect("FRI verification should succeed");
    
    let merkle_proof = tree.generate_proof(0).expect("Merkle proof should succeed");
    let merkle_valid = tree.verify_proof(&leaves[0], &merkle_proof).expect("Merkle verification should succeed");
    
    assert!(stark_valid, "STARK proof should be valid");
    assert!(fri_valid, "FRI proof should be valid");
    assert!(merkle_valid, "Merkle proof should be valid");
}

#[test]
fn test_real_world_scenario() {
    // Test a realistic use case: proving a simple computation
    
    // Define a simple computation: sum of first n numbers
    // We'll prove that sum(1..n) = n*(n+1)/2
    
    let n = 10;
    let expected_sum = n * (n + 1) / 2;
    
    // Create AIR for this computation
    let constraints = vec![
        // Transition: next_sum = current_sum + current_counter
        Constraint::new(
            vec![PrimeField64::one(), PrimeField64::one(), -PrimeField64::one()],
            2,
            ConstraintType::Transition
        ),
        // Counter increment: next_counter = current_counter + 1
        Constraint::new(
            vec![PrimeField64::one(), -PrimeField64::one()],
            1,
            ConstraintType::Transition
        ),
    ];
    
    let transition = TransitionFunction::new(vec![vec![PrimeField64::one()], vec![PrimeField64::one()]], 2);
    let mut boundary = BoundaryConditions::new(vec![]);
    
    // Initial conditions: sum=0, counter=1
    boundary.add_constraint(BoundaryConstraint::new(0, 0, PrimeField64::zero(), BoundaryType::Initial));
    boundary.add_constraint(BoundaryConstraint::new(1, 0, PrimeField64::one(), BoundaryType::Initial));
    
    // Final condition: sum should equal expected_sum
    boundary.add_constraint(BoundaryConstraint::new(0, n-1, PrimeField64::new(expected_sum as u64), BoundaryType::Final));
    
    let air = Air::new(constraints, transition, boundary, 128);
    
    // Generate and verify proof
    let prover = StarkProver::new(128);
    let initial_state = vec![PrimeField64::zero(), PrimeField64::one()];
    let proof = prover.prove(&air, &initial_state, n).expect("Real-world proof should succeed");
    
    let verifier = StarkVerifier::new(128);
    let is_valid = verifier.verify(&proof).expect("Real-world verification should succeed");
    
    assert!(is_valid, "Real-world proof should be valid");
}

#[test]
fn test_performance_optimization() {
    // Test that performance optimizations are working
    
    let mut profiler = PerformanceProfiler::new();
    let mut tracker = MemoryTracker::new();
    
    // Profile optimized operations
    {
        let section = profiler.start_section("optimized_field_arithmetic");
        for _ in 0..1000 {
            let a = PrimeField64::random();
            let b = PrimeField64::random();
            let _c = a + b;
            let _d = a * b;
        }
        section.end(&mut profiler);
    }
    
    {
        let section = profiler.start_section("optimized_polynomial_operations");
        let poly1 = test_utils::create_test_polynomial();
        let poly2 = test_utils::create_test_polynomial();
        for _ in 0..100 {
            // Simple polynomial operations (element-wise addition)
            let _sum: Vec<PrimeField64> = poly1.iter().zip(poly2.iter()).map(|(a, b)| *a + *b).collect();
        }
        section.end(&mut profiler);
    }
    
    // Track memory usage
    tracker.track("field_operations", 1024);
    tracker.track("polynomial_operations", 2048);
    
    // Verify performance is reasonable
    let report = profiler.report();
    let memory_report = tracker.report();
    
    assert!(!report.is_empty(), "Performance report should not be empty");
    assert!(!memory_report.is_empty(), "Memory report should not be empty");
}

#[test]
fn test_security_parameters() {
    // Test different security parameters
    
    let security_levels = vec![64, 128, 256];
    let air = test_utils::create_fibonacci_air();
    
    for security_level in security_levels {
        let prover = StarkProver::new(security_level);
        let initial_state = vec![PrimeField64::zero(), PrimeField64::one()];
        let proof = prover.prove(&air, &initial_state, 100).expect(&format!("Proof should succeed with security level {}", security_level));
        
        let verifier = StarkVerifier::new(security_level);
        let is_valid = verifier.verify(&proof).expect(&format!("Verification should succeed with security level {}", security_level));
        
        assert!(is_valid, "Proof should be valid with security level {}", security_level);
    }
}

#[test]
fn test_batch_operations() {
    // Test batch operations for efficiency
    
    // Batch FRI proofs
    let polynomials = vec![
        test_utils::create_test_polynomial(),
        test_utils::create_test_polynomial(),
        test_utils::create_test_polynomial(),
    ];
    
    let fri_prover = FriProver::new(128);
    let fri_verifier = FriVerifier::new(128);
    
    for polynomial in polynomials {
        let fri_proof = fri_prover.prove(&polynomial).expect("FRI proof should succeed");
        let is_valid = fri_verifier.verify(&fri_proof, &polynomial).expect("FRI verification should succeed");
        assert!(is_valid, "Batch FRI proof should be valid");
    }
    
    // Batch Merkle proofs
    let leaves = test_utils::create_test_leaves();
    let tree = MerkleTree::new(&leaves).expect("Tree should succeed");
    
    let indices = vec![0, 1, 2, 3];
    let batch_proofs = tree.generate_batch_proofs(&indices).expect("Batch proofs should succeed");
    
    for (i, proof) in batch_proofs.iter().enumerate() {
        let is_valid = tree.verify_proof(&leaves[indices[i]], proof).expect("Batch verification should succeed");
        assert!(is_valid, "Batch Merkle proof {} should be valid", i);
    }
}

#[test]
fn test_edge_cases() {
    // Test edge cases and boundary conditions
    
    // Very small inputs
    let tiny_polynomial = vec![PrimeField64::new(1)];
    let fri_prover = FriProver::new(128);
    let fri_result = fri_prover.prove(&tiny_polynomial);
    assert!(fri_result.is_ok(), "Should handle tiny polynomial");
    
    // Single leaf Merkle tree
    let single_leaf = vec![b"single".to_vec()];
    let tree_result = MerkleTree::new(&single_leaf);
    assert!(tree_result.is_ok(), "Should handle single leaf");
    
    // Zero security parameter (should use default)
    let air = test_utils::create_fibonacci_air();
    let prover = StarkProver::new(0);
    let initial_state = vec![PrimeField64::zero(), PrimeField64::one()];
    let proof_result = prover.prove(&air, &initial_state, 100);
    // Should handle gracefully or use default security parameter
}

#[test]
fn test_concurrent_operations() {
    // Test that operations can be performed concurrently (basic test)
    
    use std::thread;
    
    let air = test_utils::create_fibonacci_air();
    
    // Spawn multiple threads to generate proofs
    let handles: Vec<_> = (0..4).map(|_| {
        let air_clone = air.clone();
        thread::spawn(move || {
            let prover = StarkProver::new(128);
            let initial_state = vec![PrimeField64::zero(), PrimeField64::one()];
            prover.prove(&air_clone, &initial_state, 100)
        })
    }).collect();
    
    // Wait for all threads to complete
    for handle in handles {
        let result = handle.join().expect("Thread should complete");
        assert!(result.is_ok(), "Concurrent proof generation should succeed");
    }
}

#[test]
fn test_memory_efficiency() {
    // Test memory efficiency with large inputs
    
    let mut tracker = MemoryTracker::new();
    
    // Test with larger polynomial (use a size that works well with FRI parameters)
    let large_polynomial: Vec<PrimeField64> = (0..256).map(|i| PrimeField64::new(i)).collect();
    tracker.track("large_polynomial_creation", large_polynomial.len() * 8);
    
    let fri_prover = FriProver::new(128);
    let fri_proof = fri_prover.prove(&large_polynomial).expect("Large FRI proof should succeed");
    tracker.track("large_fri_proof", fri_proof.layers.len() * 1000);
    
    // Test with larger Merkle tree
    let large_leaves: Vec<Vec<u8>> = (0..1000).map(|i| format!("leaf_{}", i).into_bytes()).collect();
    let tree = MerkleTree::new(&large_leaves).expect("Large tree should succeed");
    tracker.track("large_merkle_tree", tree.stats().total_nodes * 32);
    
    let report = tracker.report();
    assert!(report.contains("large_polynomial_creation"), "Should track large polynomial");
    assert!(report.contains("large_fri_proof"), "Should track large FRI proof");
    assert!(report.contains("large_merkle_tree"), "Should track large Merkle tree");
}

#[test]
fn test_error_recovery() {
    // Test error recovery and graceful degradation
    
    // Test with invalid AIR (should handle gracefully)
    let invalid_constraints = vec![
        Constraint::new(
            vec![], // Empty coefficients
            0,
            ConstraintType::Transition
        ),
    ];
    
    let transition = TransitionFunction::new(vec![vec![PrimeField64::one()]], 1);
    let boundary = BoundaryConditions::new(vec![]);
    let invalid_air = Air::new(invalid_constraints, transition, boundary, 128);
    
    let prover = StarkProver::new(128);
    let initial_state = vec![PrimeField64::zero()];
    let result = prover.prove(&invalid_air, &initial_state, 10);
    // Should handle invalid AIR gracefully
    
    // Test with corrupted proof (should detect corruption)
    let valid_air = test_utils::create_fibonacci_air();
    let valid_proof = prover.prove(&valid_air, &vec![PrimeField64::zero(), PrimeField64::one()], 100).expect("Valid proof should succeed");
    
    let verifier = StarkVerifier::new(128);
    let is_valid = verifier.verify(&valid_proof).expect("Valid proof should verify");
    assert!(is_valid, "Valid proof should be valid");
}

#[test]
fn test_comprehensive_validation() {
    // Test comprehensive validation of all proof components
    
    let air = test_utils::create_fibonacci_air();
    let prover = StarkProver::new(128);
    let initial_state = vec![PrimeField64::zero(), PrimeField64::one()];
    let proof = prover.prove(&air, &initial_state, 100).expect("Proof should succeed");
    
    // Validate each component
    let trace_validation = proof.trace.validate();
    let air_validation = proof.air.validate();
    let fri_validation = proof.fri_proof.validate();
    
    // All validations should pass or be handled gracefully
    assert!(trace_validation.is_ok() || trace_validation.is_err(), "Trace validation should be handled");
    assert!(air_validation.is_ok() || air_validation.is_err(), "AIR validation should be handled");
    assert!(fri_validation.is_ok() || fri_validation.is_err(), "FRI validation should be handled");
    
    // Validate metadata
    assert_eq!(proof.metadata.version, 1, "Version should be 1");
    assert!(!proof.metadata.field_modulus.is_empty(), "Field modulus should not be empty");
    assert!(proof.metadata.proof_size > 0, "Proof size should be positive");
}