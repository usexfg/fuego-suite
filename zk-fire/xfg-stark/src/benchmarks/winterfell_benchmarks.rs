//! Winterfell STARK Performance Benchmarks
//! 
//! This module provides comprehensive benchmarking for Winterfell STARK proof generation
//! and verification, including performance analysis and optimization recommendations.

use crate::{
    burn_mint_air::{XfgBurnMintAir, BurnMintPublicInputs},
    burn_mint_prover::XfgBurnMintProver,
    burn_mint_verifier::{XfgBurnMintVerifier, VerificationResult},
};
use winterfell::{
    math::fields::f64::BaseElement, ProofOptions, StarkProof, TraceInfo,
    crypto::{DefaultRandomCoin, ElementHasher, MerkleTree},
    verify, AcceptableOptions, VerifierError,
};
use winter_crypto::hashers::Blake3_256;
use std::time::{Duration, Instant};
use std::collections::HashMap;

/// Winterfell STARK benchmark results
#[derive(Debug, Clone)]
pub struct WinterfellBenchmarkResult {
    /// Operation name
    pub operation: String,
    /// Execution time
    pub duration: Duration,
    /// Memory usage in bytes (estimated)
    pub memory_usage: usize,
    /// Number of iterations
    pub iterations: usize,
    /// Input size (trace length)
    pub trace_length: usize,
    /// Proof size in bytes
    pub proof_size: usize,
    /// Additional metrics
    pub metrics: HashMap<String, f64>,
}

impl WinterfellBenchmarkResult {
    /// Create a new benchmark result
    pub fn new(operation: String, duration: Duration, trace_length: usize) -> Self {
        Self {
            operation,
            duration,
            memory_usage: 0,
            iterations: 1,
            trace_length,
            proof_size: 0,
            metrics: HashMap::new(),
        }
    }

    /// Add a metric
    pub fn add_metric(&mut self, key: String, value: f64) {
        self.metrics.insert(key, value);
    }

    /// Get operations per second
    pub fn ops_per_second(&self) -> f64 {
        if self.duration.as_secs_f64() > 0.0 {
            self.iterations as f64 / self.duration.as_secs_f64()
        } else {
            0.0
        }
    }

    /// Get throughput (operations per second)
    pub fn throughput(&self) -> f64 {
        self.ops_per_second()
    }

    /// Get average time per operation
    pub fn avg_time_per_op(&self) -> Duration {
        if self.iterations > 0 {
            Duration::from_nanos(self.duration.as_nanos() as u64 / self.iterations as u64)
        } else {
            Duration::ZERO
        }
    }

    /// Get proof generation rate (proofs per second)
    pub fn proofs_per_second(&self) -> f64 {
        self.ops_per_second()
    }

    /// Get verification rate (verifications per second)
    pub fn verifications_per_second(&self) -> f64 {
        self.ops_per_second()
    }
}

impl std::fmt::Display for WinterfellBenchmarkResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}: {} ops/sec, {} avg, {} bytes, {} trace length, {} iterations",
            self.operation,
            self.ops_per_second(),
            format_duration(self.avg_time_per_op()),
            self.memory_usage,
            self.trace_length,
            self.iterations
        )
    }
}

/// Winterfell STARK benchmark suite
pub struct WinterfellBenchmarkSuite {
    /// Security parameter
    security_parameter: usize,
    /// Proof options
    proof_options: ProofOptions,
}

impl WinterfellBenchmarkSuite {
    /// Create a new benchmark suite
    pub fn new(security_parameter: usize) -> Self {
        let proof_options = ProofOptions::new(
            42, // blowup factor
            8,  // grinding factor
            4,  // hash function
            winterfell::FieldExtension::None, // field extension
            8,  // FRI folding factor
            31, // FRI remainder max degree
        );
        
        Self {
            security_parameter,
            proof_options,
        }
    }

    /// Benchmark proof generation performance
    pub fn benchmark_proof_generation(
        &self,
        trace_length: usize,
        iterations: usize,
    ) -> WinterfellBenchmarkResult {
        let mut total_duration = Duration::ZERO;
        let mut total_proof_size = 0;
        let mut successful_proofs = 0;

        for _ in 0..iterations {
            let start = Instant::now();
            
            // Create prover
            let prover = XfgBurnMintProver::new(self.security_parameter);
            
            // Generate test data (v3 unified format)
            let secret = [42u8; 32];
            let burn_amount = 8_000_000u64;    // 0.8 XFG
            let mint_amount = 8_000_000u64;
            let txn_hash = 0xDEADu32;

            // Generate proof
            match prover.prove_burn_mint(burn_amount, mint_amount, txn_hash, &secret, 2, 42161, 3, 0xFFFFFFFF) {
                Ok(proof) => {
                    total_proof_size += proof.to_bytes().len();
                    successful_proofs += 1;
                }
                Err(_) => {
                    // Continue with benchmark even if some proofs fail
                }
            }
            
            total_duration += start.elapsed();
        }

        let mut result = WinterfellBenchmarkResult::new(
            "Proof Generation".to_string(),
            total_duration,
            trace_length,
        );
        result.iterations = iterations;
        result.proof_size = if successful_proofs > 0 {
            total_proof_size / successful_proofs
        } else {
            0
        };
        result.memory_usage = self.estimate_memory_usage(trace_length);
        result.add_metric("success_rate".to_string(), successful_proofs as f64 / iterations as f64);
        result.add_metric("avg_proof_size".to_string(), result.proof_size as f64);

        result
    }

    /// Benchmark proof verification performance
    pub fn benchmark_proof_verification(
        &self,
        trace_length: usize,
        iterations: usize,
    ) -> WinterfellBenchmarkResult {
        let mut total_duration = Duration::ZERO;
        let mut successful_verifications = 0;

        // Create a sample proof for verification (v3 unified format)
        let prover = XfgBurnMintProver::new(self.security_parameter);
        let secret = [42u8; 32];
        let burn_amount = 8_000_000u64;
        let mint_amount = 8_000_000u64;
        let txn_hash = 0xDEADu32;

        let sample_proof = match prover.prove_burn_mint(burn_amount, mint_amount, txn_hash, &secret, 2, 42161, 3, 0xFFFFFFFF) {
            Ok(proof) => proof,
            Err(_) => {
                // Return empty result if proof generation fails
                return WinterfellBenchmarkResult::new(
                    "Proof Verification".to_string(),
                    Duration::ZERO,
                    trace_length,
                );
            }
        };

        let verifier = XfgBurnMintVerifier::new(self.security_parameter);

        for _ in 0..iterations {
            let start = Instant::now();
            
            // Verify proof
            match verifier.verify_burn_mint(&sample_proof, burn_amount, mint_amount, txn_hash, 2, 42161, 3, 0xFFFFFFFF) {
                Ok(is_valid) => {
                    if is_valid {
                        successful_verifications += 1;
                    }
                }
                Err(_) => {
                    // Continue with benchmark even if some verifications fail
                }
            }
            
            total_duration += start.elapsed();
        }

        let mut result = WinterfellBenchmarkResult::new(
            "Proof Verification".to_string(),
            total_duration,
            trace_length,
        );
        result.iterations = iterations;
        result.proof_size = sample_proof.to_bytes().len();
        result.memory_usage = self.estimate_memory_usage(trace_length);
        result.add_metric("success_rate".to_string(), successful_verifications as f64 / iterations as f64);

        result
    }

    /// Benchmark end-to-end performance (generation + verification)
    pub fn benchmark_end_to_end(
        &self,
        trace_length: usize,
        iterations: usize,
    ) -> WinterfellBenchmarkResult {
        let mut total_duration = Duration::ZERO;
        let mut successful_operations = 0;

        for _ in 0..iterations {
            let start = Instant::now();
            
            // Generate proof (v3 unified format)
            let prover = XfgBurnMintProver::new(self.security_parameter);
            let secret = [42u8; 32];
            let burn_amount = 8_000_000u64;
            let mint_amount = 8_000_000u64;
            let txn_hash = 0xDEADu32;

            let proof = match prover.prove_burn_mint(burn_amount, mint_amount, txn_hash, &secret, 2, 42161, 3, 0xFFFFFFFF) {
                Ok(proof) => proof,
                Err(_) => continue,
            };

            // Verify proof
            let verifier = XfgBurnMintVerifier::new(self.security_parameter);
            match verifier.verify_burn_mint(&proof, burn_amount, mint_amount, txn_hash, 2, 42161, 3, 0xFFFFFFFF) {
                Ok(is_valid) => {
                    if is_valid {
                        successful_operations += 1;
                    }
                }
                Err(_) => continue,
            }
            
            total_duration += start.elapsed();
        }

        let mut result = WinterfellBenchmarkResult::new(
            "End-to-End".to_string(),
            total_duration,
            trace_length,
        );
        result.iterations = iterations;
        result.memory_usage = self.estimate_memory_usage(trace_length);
        result.add_metric("success_rate".to_string(), successful_operations as f64 / iterations as f64);

        result
    }

    /// Benchmark trace generation performance
    pub fn benchmark_trace_generation(
        &self,
        trace_length: usize,
        iterations: usize,
    ) -> WinterfellBenchmarkResult {
        let mut total_duration = Duration::ZERO;

        for _ in 0..iterations {
            let start = Instant::now();
            
            // Create AIR and generate trace
            let trace_info = TraceInfo::new(7, trace_length);
            let public_inputs = BurnMintPublicInputs {
                burn_amount: BaseElement::from(1000u32),
                mint_amount: BaseElement::from(1000u32),
                txn_hash: BaseElement::from(12345u32),
                state: BaseElement::from(0u32),
                network_id: BaseElement::from(2u32),          // Fuego testnet
                target_chain_id: BaseElement::from(42161u32), // Arbitrum One
                commitment_version: BaseElement::from(3u32),  // v3 unified
                deposit_term: BaseElement::from(0xFFFFFFFFu32), // HEAT = FOREVER
            };
            let secret = BaseElement::from(67305985u32);
            
            let air = XfgBurnMintAir::new_with_secret(
                trace_info,
                public_inputs,
                secret,
                self.proof_options.clone(),
            );
            
            let _trace = air.build_trace();
            
            total_duration += start.elapsed();
        }

        let mut result = WinterfellBenchmarkResult::new(
            "Trace Generation".to_string(),
            total_duration,
            trace_length,
        );
        result.iterations = iterations;
        result.memory_usage = self.estimate_memory_usage(trace_length);

        result
    }

    /// Run comprehensive benchmark suite
    pub fn run_comprehensive_benchmark(&self) -> Vec<WinterfellBenchmarkResult> {
        let mut results = Vec::new();
        
        // Test different trace lengths
        let trace_lengths = vec![64, 128, 256, 512];
        let iterations = 10; // Reduced for faster testing
        
        for trace_length in trace_lengths {
            println!("Benchmarking with trace length: {}", trace_length);
            
            // Trace generation
            results.push(self.benchmark_trace_generation(trace_length, iterations));
            
            // Proof generation
            results.push(self.benchmark_proof_generation(trace_length, iterations));
            
            // Proof verification
            results.push(self.benchmark_proof_verification(trace_length, iterations));
            
            // End-to-end
            results.push(self.benchmark_end_to_end(trace_length, iterations));
        }
        
        results
    }

    /// Estimate memory usage for given trace length
    fn estimate_memory_usage(&self, trace_length: usize) -> usize {
        // Rough estimation: 6 registers * trace_length * 8 bytes per field element
        // Plus overhead for proof data structures
        let base_memory = 6 * trace_length * 8;
        let proof_overhead = trace_length * 100; // Conservative estimate
        base_memory + proof_overhead
    }

    /// Generate performance report
    pub fn generate_report(&self, results: &[WinterfellBenchmarkResult]) -> String {
        let mut report = String::new();
        report.push_str("=== Winterfell STARK Performance Report ===\n\n");
        
        for result in results {
            report.push_str(&format!("{}\n", result));
        }
        
        // Add summary statistics
        report.push_str("\n=== Summary ===\n");
        
        let proof_gen_results: Vec<_> = results.iter()
            .filter(|r| r.operation == "Proof Generation")
            .collect();
        
        let verification_results: Vec<_> = results.iter()
            .filter(|r| r.operation == "Proof Verification")
            .collect();
        
        if !proof_gen_results.is_empty() {
            let avg_proof_time: Duration = proof_gen_results.iter()
                .map(|r| r.avg_time_per_op())
                .sum::<Duration>() / proof_gen_results.len() as u32;
            report.push_str(&format!("Average proof generation time: {}\n", format_duration(avg_proof_time)));
        }
        
        if !verification_results.is_empty() {
            let avg_verification_time: Duration = verification_results.iter()
                .map(|r| r.avg_time_per_op())
                .sum::<Duration>() / verification_results.len() as u32;
            report.push_str(&format!("Average verification time: {}\n", format_duration(avg_verification_time)));
        }
        
        report
    }
}

/// Format duration for display
fn format_duration(duration: Duration) -> String {
    if duration.as_secs() > 0 {
        format!("{:.2}s", duration.as_secs_f64())
    } else if duration.as_millis() > 0 {
        format!("{}ms", duration.as_millis())
    } else if duration.as_micros() > 0 {
        format!("{}μs", duration.as_micros())
    } else {
        format!("{}ns", duration.as_nanos())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_benchmark_suite_creation() {
        let suite = WinterfellBenchmarkSuite::new(128);
        assert_eq!(suite.security_parameter, 128);
    }

    #[test]
    fn test_trace_generation_benchmark() {
        let suite = WinterfellBenchmarkSuite::new(128);
        let result = suite.benchmark_trace_generation(64, 5);
        
        assert_eq!(result.operation, "Trace Generation");
        assert_eq!(result.trace_length, 64);
        assert_eq!(result.iterations, 5);
        assert!(result.duration > Duration::ZERO);
    }

    #[test]
    fn test_memory_usage_estimation() {
        let suite = WinterfellBenchmarkSuite::new(128);
        let memory = suite.estimate_memory_usage(64);
        
        // Should be reasonable (at least 1KB for 64-length trace)
        assert!(memory > 1024);
    }
}
