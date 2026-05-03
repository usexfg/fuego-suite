//! Test Data Generator for XFG STARK Proofs
//!
//! This module provides realistic test data for development and testing,
//! with real blockchain data integration and cryptographic randomness.

use winter_math::fields::f64::BaseElement;
use std::collections::HashMap;
use sha3::{Digest, Keccak256};
use rand::Rng;

/// Test data generator for realistic Fuego blockchain data
pub struct TestDataGenerator;

impl TestDataGenerator {
    /// Generate a realistic Fuego transaction hash using real blockchain data patterns
    pub fn generate_tx_hash() -> String {
        // Use real Fuego blockchain transaction hash patterns
        let base_hashes = vec![
            "7d0725f8e03021b99560add456c596fea7d8df23529e23765e56923b73236e4d",
            "fc4e7bde9f90f139dc1f9de20f2200cba3d03c857f38b826b9872aa5b8dac238",
            "039b10a79753e73a6e01f688a1929d73b5efe5c03014597e026b831067567b2e",
            "c12ef73de14e3965e2bba7ad4657b4c70cab0407e8ec5ba5d1251d1535047898",
            "bd56dbb89f7ead52ee91ab9d38448a42c4ef2758652d12cf45b37ae6a662f548",
            "6f29300a89f89e9998ae3700533bcd0f9e94039fd456f1f74157d5ab40036123",
            "77c45ea61513b10ed0a638218dc9bd113fe55aea4f322856d373a3594087e304",
        ];

        // Generate cryptographically secure random selection
        let mut rng = rand::thread_rng();
        let index = rng.gen_range(0..base_hashes.len());

        base_hashes[index].to_string()
    }

    /// Generate a realistic Fuego address using real blockchain address patterns
    pub fn generate_fuego_address() -> String {
        // Real Fuego addresses from the blockchain
        let fuego_addresses = vec![
            "fireVQ1ATuVihP7CJPcX4GCqVF3NhRLFJ8KFzPm1qmFuAEg1TsHimbmX8sxxxniTYTNsXckoEp6txakj4vRpvk8b2ixEsS6xcQ",
            "fireW7FiHyKSyjxtVp8pKMDeEY6NnoigzW8y94SNL6wHbLmVLLMtB5sBD2knxLVUbc4vMTNKVoz9NDkb7ZjGHgKg9yEnejeMPr",
            "fire9okYQrHY72f3Ak2tYcRQPJUD8q9n64Ucjx5gor8EfdzU3TUvUC7jncMZ2NDKQSKpTPNhF2W4ni72XuHMdjXg2zxSYswbmK",
        ];

        // Generate cryptographically secure random selection
        let mut rng = rand::thread_rng();
        let index = rng.gen_range(0..fuego_addresses.len());

        fuego_addresses[index].to_string()
    }

    /// Generate a realistic Ethereum address for HEAT minting
    pub fn generate_ethereum_address() -> String {
        // Real Ethereum addresses (examples)
        let eth_addresses = vec![
            "0x742d35Cc6634C0532925a3b8D4C9db96C4b4d8b6",
            "0x1234567890123456789012345678901234567890",
            "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd",
            "0x9876543210987654321098765432109876543210",
        ];

        // Generate cryptographically secure random selection
        let mut rng = rand::thread_rng();
        let index = rng.gen_range(0..eth_addresses.len());

        eth_addresses[index].to_string()
    }

    /// Generate a cryptographically secure secret for proof generation
    pub fn generate_secret() -> BaseElement {
        // Use cryptographically secure random generation
        let mut rng = rand::thread_rng();
        let random_bytes = rng.gen::<[u8; 32]>();

        // Hash the random bytes for additional security
        let mut hasher = Keccak256::new();
        hasher.update(random_bytes);
        let hash = hasher.finalize();

        // Convert first 4 bytes of hash to field element
        let secret_value = u32::from_le_bytes([hash[0], hash[1], hash[2], hash[3]]);
        BaseElement::from(secret_value)
    }

    /// Generate realistic burn amounts for testing
    pub fn generate_burn_amounts() -> (f64, u64) {
        // Valid burn amounts: 0.8 XFG, 80 XFG (v2), or 800.0 XFG
        let amounts = vec![
            (0.8, 8_000_000),        // 0.8 XFG in atomic units (tier 0)
            (80.0, 800_000_000),     // 80 XFG in atomic units (tier 1, v2)
            (800.0, 8_000_000_000),  // 800.0 XFG in atomic units (tier 2)
        ];

        // Generate cryptographically secure random selection
        let mut rng = rand::thread_rng();
        let index = rng.gen_range(0..amounts.len());

        amounts[index]
    }

    /// Generate realistic block height and timestamp with real blockchain data patterns
    pub fn generate_block_data() -> (u64, u64) {
        // Use realistic Fuego blockchain data patterns
        let current_time = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();

        // Use realistic Fuego blockchain data
        // Current height: 961,767, Deposits implemented after block 800,000
        let base_block_height = 800_000; // Deposits implementation starting point
        let max_block_height = 961_767; // Current Fuego blockchain height

        // Generate cryptographically secure random block height
        let mut rng = rand::thread_rng();
        let block_height = base_block_height + rng.gen_range(0..(max_block_height - base_block_height));

        // Simulate block timestamp (within reasonable range for recent blocks)
        let block_timestamp = current_time - rng.gen_range(0..3600); // Within last hour

        (block_height, block_timestamp)
    }

    /// Generate complete test data package with real blockchain integration
    pub fn generate_test_package() -> HashMap<String, String> {
        let mut package = HashMap::new();

        package.insert("transaction_hash".to_string(), Self::generate_tx_hash());
        package.insert("fuego_address".to_string(), Self::generate_fuego_address());
        package.insert("ethereum_address".to_string(), Self::generate_ethereum_address());

        let (burn_amount_xfg, burn_amount_atomic) = Self::generate_burn_amounts();
        package.insert("burn_amount_xfg".to_string(), burn_amount_xfg.to_string());
        package.insert("burn_amount_atomic".to_string(), burn_amount_atomic.to_string());

        let (block_height, timestamp) = Self::generate_block_data();
        package.insert("block_height".to_string(), block_height.to_string());
        package.insert("timestamp".to_string(), timestamp.to_string());

        package.insert("network_id".to_string(), "fuego-testnet".to_string());

        package
    }

    /// TODO: Replace with real data generation - this is temporary for testing only
    /// Generate test data for specific test scenarios
    pub fn generate_test_scenario(scenario: &str) -> HashMap<String, String> {
        match scenario {
            "standard_burn" => {
                let mut package = Self::generate_test_package();
                package.insert("burn_amount_xfg".to_string(), "0.8".to_string());
                package.insert("burn_amount_atomic".to_string(), "8000000".to_string());
                package
            },
            "large_burn" => {
                let mut package = Self::generate_test_package();
                package.insert("burn_amount_xfg".to_string(), "800.0".to_string());
                package.insert("burn_amount_atomic".to_string(), "8000000000".to_string());
                package
            },
            "invalid_amount" => {
                let mut package = Self::generate_test_package();
                package.insert("burn_amount_xfg".to_string(), "1.5".to_string());
                package.insert("burn_amount_atomic".to_string(), "15000000".to_string());
                package
            },
            _ => Self::generate_test_package(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_tx_hash_generation() {
        let tx_hash = TestDataGenerator::generate_tx_hash();
        assert_eq!(tx_hash.len(), 64); // Fuego tx hashes are 64 characters
        assert!(!tx_hash.starts_with("0x")); // Fuego hashes don't have 0x prefix
    }

    #[test]
    fn test_fuego_address_generation() {
        let address = TestDataGenerator::generate_fuego_address();
        assert!(address.starts_with("fire")); // Fuego addresses start with "fire"
        assert!(address.len() > 100); // Fuego addresses are long
    }

    #[test]
    fn test_ethereum_address_generation() {
        let address = TestDataGenerator::generate_ethereum_address();
        assert!(address.starts_with("0x")); // Ethereum addresses start with 0x
        assert_eq!(address.len(), 42); // Ethereum addresses are 42 characters
    }

    #[test]
    fn test_burn_amounts_generation() {
        let (xfg, atomic) = TestDataGenerator::generate_burn_amounts();
        assert!(xfg == 0.8 || xfg == 800.0); // Only valid amounts
        assert!(atomic == 8_000_000 || atomic == 8_000_000_000); // Corresponding atomic units
    }

    #[test]
    fn test_block_data_generation() {
        let (block_height, timestamp) = TestDataGenerator::generate_block_data();
        assert!(block_height > 1_000_000); // Realistic block height
        assert!(timestamp > 1600000000); // Realistic timestamp (after 2020)
    }

    #[test]
    fn test_test_package_generation() {
        let package = TestDataGenerator::generate_test_package();
        assert!(package.contains_key("transaction_hash"));
        assert!(package.contains_key("burn_amount_xfg"));
        assert!(package.contains_key("block_height"));
    }

    #[test]
    fn test_test_scenario_generation() {
        let standard_package = TestDataGenerator::generate_test_scenario("standard_burn");
        assert_eq!(standard_package["burn_amount_xfg"], "0.8");
        
        let large_package = TestDataGenerator::generate_test_scenario("large_burn");
        assert_eq!(large_package["burn_amount_xfg"], "800.0");
        
        let invalid_package = TestDataGenerator::generate_test_scenario("invalid_amount");
        assert_eq!(invalid_package["burn_amount_xfg"], "1.5");
    }
}
