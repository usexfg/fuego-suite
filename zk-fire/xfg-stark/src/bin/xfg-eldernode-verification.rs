use clap::{App, Arg};
use xfg_stark::{
    proof_data_schema::{StarkProofDataPackage, CompleteProofPackage, StarkProof, EldernodeVerification, ProofDataTemplate},
    burn_mint_prover::XfgBurnMintProver,
    burn_mint_verifier::{XfgBurnMintVerifier, VerificationResult},
    XfgStarkError,
    Result,
};
use std::time::{Instant, Duration};
use std::sync::{Arc, Mutex};
use std::thread;
use std::sync::mpsc;
use tokio;

// Progress tracking structures
#[derive(Debug, Clone)]
enum VerificationStatus {
    NotStarted,
    SendingToEldernodes,
    AwaitingConsensus,
    EldernodeResponse(u32, u32), // (responded, total)
    ConsensusReached,
    Failed(String),
}

#[derive(Debug, Clone)]
struct ProgressTracker {
    stark_status: String,
    eldernode_status: VerificationStatus,
    start_time: Instant,
    last_update: Instant,
}

impl ProgressTracker {
    fn new() -> Self {
        Self {
            stark_status: "Initializing...".to_string(),
            eldernode_status: VerificationStatus::NotStarted,
            start_time: Instant::now(),
            last_update: Instant::now(),
        }
    }

    fn update_stark_status(&mut self, status: String) {
        self.stark_status = status;
        self.last_update = Instant::now();
        self.display_progress();
    }

    fn update_eldernode_status(&mut self, status: VerificationStatus) {
        self.eldernode_status = status;
        self.last_update = Instant::now();
        self.display_progress();
    }

    fn display_progress(&self) {
        let elapsed = self.start_time.elapsed();
        let since_update = self.last_update.elapsed();
        
        // Clear previous lines (approximate)
        print!("\r\x1B[K"); // Clear line
        
        match &self.eldernode_status {
            VerificationStatus::NotStarted => {
                println!("🔐 STARK Generation: {}", self.stark_status);
                println!("⏳ Eldernode Verification: Not started");
            }
            VerificationStatus::SendingToEldernodes => {
                println!("🔐 STARK Generation: {}", self.stark_status);
                println!("📤 Eldernode Verification: Sending commitment & burn amount verification...");
            }
            VerificationStatus::AwaitingConsensus => {
                println!("🔐 STARK Generation: {}", self.stark_status);
                println!("⏳ Eldernode Verification: Awaiting consensus...");
            }
            VerificationStatus::EldernodeResponse(responded, total) => {
                println!("🔐 STARK Generation: {}", self.stark_status);
                println!("📊 Eldernode Verification: {}/{} Eldernodes responded", responded, total);
            }
            VerificationStatus::ConsensusReached => {
                println!("🔐 STARK Generation: {}", self.stark_status);
                println!("✅ Eldernode Verification: Consensus reached!");
            }
            VerificationStatus::Failed(error) => {
                println!("🔐 STARK Generation: {}", self.stark_status);
                println!("❌ Eldernode Verification: Failed - {}", error);
            }
        }
        
        println!("⏱️  Total Time: {:?}", elapsed);
        if since_update < Duration::from_secs(5) {
            println!("🔄 Last Update: {:?} ago", since_update);
        }
    }
}

// STARK generation inputs structure (v3 unified relay format)
#[derive(Debug, Clone)]
struct StarkGenerationInputs {
    secret: Vec<u8>,
    burn_amount: u64,
    mint_amount: u64,
    txn_hash: u32,
    network_id: u32,
    target_chain_id: u32,
    commitment_version: u32,
    deposit_term: u32,
}

// Eldernode verification inputs (commitment + burn amount)
#[derive(Debug, Clone)]
struct EldernodeVerificationInputs {
    tx_hash: String,
    commitment: String,  // The commitment as a whole (32-byte hex string)
    burn_amount: u64,    // Burn amount (amount with undefined output key)
}

// Eldernode consensus structure
#[derive(Debug, Clone)]
struct EldernodeConsensus {
    eldernode_ids: Vec<String>,
    signatures: Vec<String>,
    message_hash: String,
    timestamp: String,
    consensus_threshold: u32,
    total_eldernodes: u32,
    verified_inputs: EldernodeVerificationInputs,
    tx_extra_commitment: String,  // Commitment extracted from tx_extra
    tx_burn_amount: u64,          // Burn amount from transaction (undefined output key)
    commitment_match: bool,        // Whether commitments match
    burn_amount_match: bool,      // Whether burn amounts match
}

// Eldernode verification client (mock implementation)
struct EldernodeClient {
    progress_tx: std::sync::mpsc::Sender<VerificationStatus>,
}

impl EldernodeClient {
    fn new(progress_tx: std::sync::mpsc::Sender<VerificationStatus>) -> Self {
        Self { progress_tx }
    }

    async fn verify_commitment_and_burn_amount_with_eldernodes(&self, verification_inputs: &EldernodeVerificationInputs) -> Result<EldernodeConsensus> {
        // Send initial status
        self.progress_tx.send(VerificationStatus::SendingToEldernodes)?;
        
        // Simulate network delay
        tokio::time::sleep(tokio::time::Duration::from_millis(500)).await;
        
        // Send awaiting consensus status
        self.progress_tx.send(VerificationStatus::AwaitingConsensus)?;
        
        // Simulate Eldernode responses
        let total_eldernodes = 5;
        for i in 1..=total_eldernodes {
            tokio::time::sleep(tokio::time::Duration::from_millis(800)).await;
            self.progress_tx.send(VerificationStatus::EldernodeResponse(i, total_eldernodes))?;
        }
        
        // Simulate consensus reached
        tokio::time::sleep(tokio::time::Duration::from_millis(300)).await;
        self.progress_tx.send(VerificationStatus::ConsensusReached)?;
        
        // Extract commitment from tx_extra (this would be the actual extraction)
        let tx_extra_commitment = extract_commitment_from_tx_extra(&verification_inputs.tx_hash)?;
        
        // Extract burn amount from transaction (undefined output key amount)
        let tx_burn_amount = extract_burn_amount_from_transaction(&verification_inputs.tx_hash)?;
        
        // Check if commitments match
        let commitment_match = verification_inputs.commitment == tx_extra_commitment;
        
        // Check if burn amounts match
        let burn_amount_match = verification_inputs.burn_amount == tx_burn_amount;
        
        // Return mock consensus with verified inputs
        Ok(EldernodeConsensus {
            eldernode_ids: vec!["elder1".to_string(), "elder2".to_string(), "elder3".to_string()],
            signatures: vec!["sig1".to_string(), "sig2".to_string(), "sig3".to_string()],
            message_hash: "consensus_hash".to_string(),
            timestamp: chrono::Utc::now().to_rfc3339(),
            consensus_threshold: 3,
            total_eldernodes: 5,
            verified_inputs: verification_inputs.clone(),
            tx_extra_commitment,
            tx_burn_amount,
            commitment_match,
            burn_amount_match,
        })
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let matches = App::new("xfg-eldernode-verification")
        .version("1.0")
        .about("CLI tool for STARK proof generation with Eldernode verification (commitment + burn amount)")
        .subcommand(
            App::new("prove-and-verify")
                .about("Generate STARK proof and verify with Eldernodes")
                .arg(
                    Arg::new("input")
                        .short('i')
                        .long("input")
                        .value_name("FILE")
                        .help("Input data package file")
                        .required(true)
                        .takes_value(true)
                )
                .arg(
                    Arg::new("output")
                        .short('o')
                        .long("output")
                        .value_name("FILE")
                        .help("Output complete proof package file")
                        .required(true)
                        .takes_value(true)
                )
                .arg(
                    Arg::new("eldernode-endpoint")
                        .short('e')
                        .long("eldernode-endpoint")
                        .value_name("URL")
                        .help("Eldernode verification endpoint")
                        .takes_value(true)
                        .default_value("https://eldernodes.fuego.network/api/v1/verify")
                )
        )
        .get_matches();

    match matches.subcommand() {
        Some(("prove-and-verify", args)) => {
            let input_file = args.get_one::<String>("input").unwrap();
            let output_file = args.get_one::<String>("output").unwrap();
            let eldernode_endpoint = args.get_one::<String>("eldernode-endpoint").unwrap();
            prove_and_verify_with_eldernodes(input_file, output_file, eldernode_endpoint).await?;
        }
        _ => {
            eprintln!("Unknown subcommand. Use --help for usage information.");
            std::process::exit(1);
        }
    }

    Ok(())
}

/// Generate STARK proof and verify with Eldernodes
async fn prove_and_verify_with_eldernodes(input_file: &str, output_file: &str, eldernode_endpoint: &str) -> Result<()> {
    println!("🚀 XFG Burn & HEAT Mint with STARK + Eldernode Verification");
    println!("============================================================");
    
    // Load and validate data package
    println!("🔍 Loading data package from: {}", input_file);
    let package = StarkProofDataPackage::load_from_file(input_file)
        .map_err(|e| XfgStarkError::ParseError(e.to_string()))?;

    let validation = package.validate();
    if !validation.is_valid {
        eprintln!("❌ Data package validation failed:");
        for error in &validation.errors {
            eprintln!("   - {}", error);
        }
        std::process::exit(1);
    }

    println!("✅ Data package validated successfully");
    println!("📊 Burn amount: {} XFG ({} atomic units)",
             package.burn_transaction.burn_amount_xfg,
             package.burn_transaction.burn_amount_atomic);
    println!("🎯 Mint amount: {} HEAT", package.get_mint_amount_heat());

    // Prepare STARK inputs (full inputs for STARK generation)
    println!("\n🔧 Preparing STARK generation inputs...");
    let stark_inputs = prepare_stark_inputs(&package)?;
    
    // Prepare Eldernode verification inputs (commitment + burn amount)
    println!("🔧 Preparing Eldernode verification inputs...");
    let eldernode_inputs = prepare_eldernode_inputs(&package)?;
    
    println!("📋 Eldernode verification inputs:");
    println!("   Transaction Hash: {}", eldernode_inputs.tx_hash);
    println!("   Commitment: {}", eldernode_inputs.commitment);
    println!("   Burn Amount: {} atomic units", eldernode_inputs.burn_amount);
    println!("   Note: Eldernodes verify commitment matches tx_extra AND burn amount matches undefined output key");
    
    // Set up progress tracking
    let (progress_tx, progress_rx) = std::sync::mpsc::channel();
    let progress_tracker = Arc::new(Mutex::new(ProgressTracker::new()));
    
    // Start progress display thread
    let progress_display = progress_tracker.clone();
    let progress_thread = thread::spawn(move || {
        while let Ok(status) = progress_rx.recv() {
            let mut tracker = progress_display.lock().unwrap();
            tracker.update_eldernode_status(status);
        }
    });

    // Create Eldernode client
    let eldernode_client = EldernodeClient::new(progress_tx.clone());
    
    // Start both processes in parallel
    println!("\n⚡ Starting parallel STARK generation and Eldernode verification...");
    
    let stark_start = Instant::now();
    let mut tracker = progress_tracker.lock().unwrap();
    tracker.update_stark_status("Initializing prover...".to_string());
    drop(tracker);

    // Start STARK generation in a separate thread
    let stark_inputs_clone = stark_inputs.clone();
    let stark_handle = thread::spawn(move || {
        generate_stark_proof(&stark_inputs_clone, progress_tracker.clone())
    });

    // Start Eldernode verification (commitment + burn amount)
    let eldernode_handle = tokio::spawn(async move {
        eldernode_client.verify_commitment_and_burn_amount_with_eldernodes(&eldernode_inputs).await
    });

    // Wait for both to complete
    let stark_result = stark_handle.join().unwrap()?;
    let eldernode_result = eldernode_handle.await?;

    // Verify consistency
    println!("\n🔒 Verifying Eldernode verification consistency...");
    verify_eldernode_consistency(&eldernode_inputs, &stark_result, &eldernode_result)?;
    println!("✅ Eldernode verification successful - commitment and burn amount match on-chain data");

    // Create complete proof package using the proper API
    let mut complete_package = CompleteProofPackage::new(package);
    complete_package.add_stark_proof(stark_result);
    complete_package.add_eldernode_verification(eldernode_result);

    // Save complete package
    let json = serde_json::to_string_pretty(&complete_package)
        .map_err(|e| XfgStarkError::JsonError(e))?;

    std::fs::write(output_file, json)
        .map_err(|e| XfgStarkError::IoError(e))?;

    let total_time = stark_start.elapsed();
    println!("\n🎉 Complete verification successful!");
    println!("📁 Complete proof package saved to: {}", output_file);
    println!("⏱️  Total time: {:?}", total_time);
    println!("🚀 Ready for submission to HEAT mint contract!");

    // Clean up progress thread
    drop(progress_tx);
    progress_thread.join().unwrap();

    Ok(())
}

/// Prepare full inputs for STARK generation (v3 unified relay format)
fn prepare_stark_inputs(package: &StarkProofDataPackage) -> Result<StarkGenerationInputs> {
    // Extract txn_hash as u32 (first 4 bytes of tx hash, LE)
    let tx_hash_bytes = hex_to_bytes(&package.burn_transaction.transaction_hash)
        .map_err(|e| XfgStarkError::ParseError(format!("Invalid transaction hash: {}", e)))?;
    let txn_hash = if tx_hash_bytes.len() >= 4 {
        u32::from_le_bytes([tx_hash_bytes[0], tx_hash_bytes[1], tx_hash_bytes[2], tx_hash_bytes[3]])
    } else {
        return Err(XfgStarkError::ParseError("Transaction hash too short".to_string()));
    };

    // Convert secret to bytes
    let secret_bytes = package.secret.secret_key.as_bytes();

    // v3 unified relay format
    let network_id = package.burn_transaction.network_id.parse::<u32>().unwrap_or(1);
    let target_chain_id = package.burn_transaction.target_chain_id.unwrap_or(42161);
    let commitment_version = 3; // v3 unified
    let deposit_term = package.burn_transaction.deposit_term.unwrap_or(0xFFFFFFFF); // HEAT = FOREVER

    Ok(StarkGenerationInputs {
        secret: secret_bytes.to_vec(),
        burn_amount: package.burn_transaction.burn_amount_atomic,
        mint_amount: package.get_mint_amount_atomic(),
        txn_hash,
        network_id,
        target_chain_id,
        commitment_version,
        deposit_term,
    })
}

/// Prepare inputs for Eldernode verification (commitment + burn amount)
fn prepare_eldernode_inputs(package: &StarkProofDataPackage) -> Result<EldernodeVerificationInputs> {
    // Compute the commitment that should match what's in tx_extra
    let commitment = compute_commitment_from_inputs(&package)?;
    
    Ok(EldernodeVerificationInputs {
        tx_hash: package.burn_transaction.transaction_hash.clone(),
        commitment: commitment,
        burn_amount: package.burn_transaction.burn_amount_atomic,
    })
}

/// Compute commitment from inputs (v3 unified: keccak256 of 56-byte preimage)
///
/// Preimage: secret[32] || le64(amount) || le32(networkId) || le32(chainId) || le32(version) || le32(term)
/// Matches C++ StarkCommitmentGenerator::computeCommitment
fn compute_commitment_from_inputs(package: &StarkProofDataPackage) -> Result<String> {
    use sha3::{Digest, Keccak256};

    let secret_bytes = package.secret.secret_key.as_bytes();
    let mut secret_array = [0u8; 32];
    let copy_len = secret_bytes.len().min(32);
    secret_array[..copy_len].copy_from_slice(&secret_bytes[..copy_len]);

    let amount = package.burn_transaction.burn_amount_atomic;
    let network_id = package.burn_transaction.network_id.parse::<u32>().unwrap_or(1);
    let chain_id = package.burn_transaction.target_chain_id.unwrap_or(42161);
    let version = 3u32; // v3 unified
    let term = package.burn_transaction.deposit_term.unwrap_or(0xFFFFFFFF);

    // 56-byte preimage
    let mut preimage = Vec::with_capacity(56);
    preimage.extend_from_slice(&secret_array);
    preimage.extend_from_slice(&amount.to_le_bytes());
    preimage.extend_from_slice(&network_id.to_le_bytes());
    preimage.extend_from_slice(&chain_id.to_le_bytes());
    preimage.extend_from_slice(&version.to_le_bytes());
    preimage.extend_from_slice(&term.to_le_bytes());

    let commitment_hash = Keccak256::digest(&preimage);
    Ok(hex::encode(commitment_hash))
}

/// Extract commitment from tx_extra of the Fuego transaction
fn extract_commitment_from_tx_extra(tx_hash: &str) -> Result<String> {
    // This would be the actual implementation that:
    // 1. Looks up the transaction on the Fuego blockchain
    // 2. Extracts the tx_extra field
    // 3. Parses the 0x08 HEAT commitment tag
    // 4. Returns the 32-byte commitment as hex string
    
    // For now, simulate the extraction
    // In real implementation, this would query the Fuego RPC
    println!("🔍 Extracting commitment from tx_extra for transaction: {}", tx_hash);
    
    // Mock extraction - in reality this would be:
    // let tx = get_transaction_from_fuego_rpc(tx_hash)?;
    // let tx_extra = parse_tx_extra(tx.extra)?;
    // let commitment = extract_heat_commitment(tx_extra)?;
    
    // For demo purposes, return a mock commitment
    Ok("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef".to_string())
}

/// Extract burn amount from transaction (undefined output key amount)
fn extract_burn_amount_from_transaction(tx_hash: &str) -> Result<u64> {
    // This would be the actual implementation that:
    // 1. Looks up the transaction on the Fuego blockchain
    // 2. Finds the output with undefined key (burn output)
    // 3. Returns the amount of that output
    
    // For now, simulate the extraction
    // In real implementation, this would query the Fuego RPC
    println!("🔍 Extracting burn amount from transaction: {}", tx_hash);
    
    // Mock extraction - in reality this would be:
    // let tx = get_transaction_from_fuego_rpc(tx_hash)?;
    // let burn_output = find_undefined_output_key(tx.outputs)?;
    // let burn_amount = burn_output.amount;
    
    // For demo purposes, return a mock burn amount
    Ok(8_000_000_000) // 800 XFG in atomic units
}

/// Generate STARK proof with progress tracking
fn generate_stark_proof(
    inputs: &StarkGenerationInputs,
    progress_tracker: Arc<Mutex<ProgressTracker>>
) -> Result<StarkProof> {
    let mut tracker = progress_tracker.lock().unwrap();
    tracker.update_stark_status("Creating prover...".to_string());
    drop(tracker);

    // Create prover
    let prover = XfgBurnMintProver::new(128);

    let mut tracker = progress_tracker.lock().unwrap();
    tracker.update_stark_status("Generating proof...".to_string());
    drop(tracker);

    // Convert secret to 32-byte array
    let mut secret_array = [0u8; 32];
    let copy_len = inputs.secret.len().min(32);
    secret_array[..copy_len].copy_from_slice(&inputs.secret[..copy_len]);

    // Generate STARK proof (v3 unified relay format)
    let winterfell_proof = prover.prove_burn_mint(
        inputs.burn_amount,
        inputs.mint_amount,
        inputs.txn_hash,
        &secret_array,
        inputs.network_id,
        inputs.target_chain_id,
        inputs.commitment_version,
        inputs.deposit_term,
    ).map_err(|e| XfgStarkError::CryptoError(format!("Proof generation failed: {}", e)))?;

    let mut tracker = progress_tracker.lock().unwrap();
    tracker.update_stark_status("Proof generated successfully!".to_string());
    drop(tracker);

    // Convert to our format
    let proof_data = winterfell_proof.to_bytes();

    Ok(StarkProof {
        proof_data: proof_data.clone(),
        public_inputs: xfg_stark::proof_data_schema::StarkPublicInputs {
            burn_amount: inputs.burn_amount,
            mint_amount: inputs.mint_amount,
            txn_hash: format!("{:08x}", inputs.txn_hash),
            state: 0,
            deposit_term: inputs.deposit_term,
            network_id: inputs.network_id,
            target_chain_id: inputs.target_chain_id,
            commitment_version: inputs.commitment_version,
        },
        metadata: xfg_stark::proof_data_schema::ProofMetadata {
            version: "3.0.0".to_string(),
            created_at: chrono::Utc::now().to_rfc3339(),
            description: format!("STARK proof for {} XFG burn (v3 unified)", inputs.burn_amount),
            network: "fuego-testnet".to_string(),
        },
    })
}

/// Verify Eldernode consistency between STARK proof and Eldernode consensus
fn verify_eldernode_consistency(
    eldernode_inputs: &EldernodeVerificationInputs,
    stark_proof: &StarkProof,
    eldernode_consensus: &EldernodeConsensus
) -> Result<()> {
    // **Key Verification: Commitment Matching**
    if !eldernode_consensus.commitment_match {
        return Err(XfgStarkError::CryptoError(
            "Commitment mismatch: provided commitment does not match tx_extra commitment".to_string()
        ));
    }
    
    // **Key Verification: Burn Amount Matching**
    if !eldernode_consensus.burn_amount_match {
        return Err(XfgStarkError::CryptoError(
            "Burn amount mismatch: provided burn amount does not match undefined output key amount".to_string()
        ));
    }
    
    println!("✅ Eldernode verification successful:");
    println!("   Commitment verification:");
    println!("     Provided: {}", eldernode_inputs.commitment);
    println!("     tx_extra:  {}", eldernode_consensus.tx_extra_commitment);
    println!("     Match:     {}", eldernode_consensus.commitment_match);
    println!("   Burn amount verification:");
    println!("     Provided: {} atomic units", eldernode_inputs.burn_amount);
    println!("     On-chain: {} atomic units", eldernode_consensus.tx_burn_amount);
    println!("     Match:     {}", eldernode_consensus.burn_amount_match);
    
    Ok(())
}

// Helper functions for hex conversion
fn hex_to_bytes(hex: &str) -> Result<Vec<u8>, hex::FromHexError> {
    // Remove 0x prefix if present
    let hex_clean = if hex.starts_with("0x") {
        &hex[2..]
    } else {
        hex
    };
    hex::decode(hex_clean)
}

fn hex_to_u64(hex: &str) -> Result<u64, XfgStarkError> {
    let bytes = hex_to_bytes(hex)
        .map_err(|e| XfgStarkError::ParseError(format!("Invalid hex string: {}", e)))?;
    
    if bytes.len() < 8 {
        return Err(XfgStarkError::ParseError("Hex string too short for u64".to_string()));
    }
    
    let mut u64_bytes = [0u8; 8];
    u64_bytes.copy_from_slice(&bytes[0..8]);
    Ok(u64::from_le_bytes(u64_bytes))
}
