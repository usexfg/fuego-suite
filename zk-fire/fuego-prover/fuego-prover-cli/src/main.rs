use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand};
use fuego_prover_core::{
    compute_checkpoint_hash, compute_merkle_root, parse_heat_commitments, CircuitWitness,
    ProofPublicValues, RpcBlock,
};
use serde::{Deserialize, Serialize};
use sp1_sdk::{ProverClient, SP1Stdin};
use tiny_keccak::{Hasher, Keccak};

use std::path::PathBuf;

/// Load the circuit ELF binary at runtime.
/// Looks for CIRCUIT_ELF_PATH env var, falling back to the default build path.
fn load_circuit_elf() -> Result<Vec<u8>> {
    let path = std::env::var("CIRCUIT_ELF_PATH")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../fuego-circuit/elf/riscv32im-succinct-zkvm-elf")
        });
    std::fs::read(&path)
        .with_context(|| format!("Failed to load circuit ELF from {}", path.display()))
}

// ---------------------------------------------------------------------------
// CLI definition
// ---------------------------------------------------------------------------

#[derive(Parser)]
#[command(name = "fuego-prover", about = "Fuego ZK prover CLI")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Generate a ZK proof for a range of Fuego blocks
    Prove(ProveArgs),
    /// Produce claim calldata for a HEAT commitment preimage
    Claim(ClaimArgs),
}

#[derive(Parser)]
struct ProveArgs {
    /// RPC base URL (e.g. http://localhost:8080)
    #[arg(long)]
    rpc: String,

    /// First block height in the range (inclusive)
    #[arg(long)]
    from_height: u32,

    /// Last block height in the range (inclusive)
    #[arg(long)]
    to_height: u32,

    /// Previous checkpoint hash as a 64-char hex string (32 bytes)
    #[arg(long)]
    checkpoint: String,

    /// Difficulty target for PoW verification (e.g. 0x00000FFF)
    #[arg(long)]
    difficulty_target: Option<u32>,

    /// Output file path for the serialised proof bytes
    #[arg(long)]
    out: String,
}

#[derive(Parser)]
struct ClaimArgs {
    /// RPC base URL (e.g. http://localhost:8080)
    #[arg(long)]
    rpc: String,

    /// Commitment hash as a 64-char hex string (32 bytes, keccak256 of preimage)
    #[arg(long)]
    commitment: String,

    /// Preimage as a 112-char hex string (56 bytes):
    /// secret[32] || amount_le64[8] || network_id_le32[4] || chain_id_le32[4]
    /// || version_le32[4]=3 || term_le32[4]=0xFFFFFFFF
    #[arg(long)]
    preimage: String,

    /// Recipient Ethereum address (0x-prefixed)
    #[arg(long)]
    recipient: String,

    /// Output file path for the JSON claim data
    #[arg(long)]
    out: String,
}

// ---------------------------------------------------------------------------
// RPC response shapes
// ---------------------------------------------------------------------------

#[derive(Deserialize)]
struct BlockRangeResponse {
    blocks: Vec<RpcBlock>,
    #[allow(dead_code)]
    status: String,
}

#[derive(Deserialize)]
struct CommitmentLeavesResponse {
    leaves: Vec<String>,
    #[allow(dead_code)]
    status: String,
}

#[derive(Deserialize)]
struct MerkleProofResponse {
    proof: Vec<String>,
    leaf_index: usize,
    #[allow(dead_code)]
    status: String,
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/// Decode a hex string (with or without 0x prefix) into a fixed-size byte array.
fn hex_to_array<const N: usize>(hex: &str) -> Result<[u8; N]> {
    let stripped = hex.strip_prefix("0x").unwrap_or(hex);
    let bytes = hex::decode(stripped).with_context(|| format!("invalid hex string: {hex}"))?;
    if bytes.len() != N {
        bail!(
            "expected {} bytes from hex but got {} bytes (input: {})",
            N,
            bytes.len(),
            hex
        );
    }
    let mut out = [0u8; N];
    out.copy_from_slice(&bytes);
    Ok(out)
}

/// Decode a hex string into a variable-length byte vec.
fn hex_to_vec(hex: &str) -> Result<Vec<u8>> {
    let stripped = hex.strip_prefix("0x").unwrap_or(hex);
    hex::decode(stripped).with_context(|| format!("invalid hex string: {hex}"))
}

/// keccak256 of an arbitrary byte slice.
fn keccak256(data: &[u8]) -> [u8; 32] {
    let mut k = Keccak::v256();
    k.update(data);
    let mut out = [0u8; 32];
    k.finalize(&mut out);
    out
}

/// Format a 32-byte array as a 0x-prefixed lowercase hex string.
fn fmt_hex32(bytes: &[u8; 32]) -> String {
    format!("0x{}", hex::encode(bytes))
}

/// Format an arbitrary byte slice as a 0x-prefixed lowercase hex string.
fn fmt_hex(bytes: &[u8]) -> String {
    format!("0x{}", hex::encode(bytes))
}

// ---------------------------------------------------------------------------
// `prove` subcommand
// ---------------------------------------------------------------------------

fn run_prove(args: ProveArgs) -> Result<()> {
    let circuit_elf = load_circuit_elf()?;

    let client = reqwest::blocking::Client::new();

    // 1. Fetch block range from RPC.
    let block_range_url = format!("{}/get_block_range", args.rpc);
    let block_range_body = serde_json::json!({
        "start_height": args.from_height,
        "end_height": args.to_height,
    });
    let block_range_resp: BlockRangeResponse = client
        .post(&block_range_url)
        .json(&block_range_body)
        .send()
        .with_context(|| format!("POST {block_range_url} failed"))?
        .error_for_status()
        .with_context(|| format!("POST {block_range_url} returned error status"))?
        .json()
        .with_context(|| format!("failed to parse response from {block_range_url}"))?;

    let blocks = block_range_resp.blocks;

    // 2. Fetch prior commitment leaves.
    let leaves_url = format!("{}/get_commitment_leaves", args.rpc);
    let leaves_resp: CommitmentLeavesResponse = client
        .get(&leaves_url)
        .send()
        .with_context(|| format!("GET {leaves_url} failed"))?
        .error_for_status()
        .with_context(|| format!("GET {leaves_url} returned error status"))?
        .json()
        .with_context(|| format!("failed to parse response from {leaves_url}"))?;

    let prev_leaves: Vec<[u8; 32]> = leaves_resp
        .leaves
        .iter()
        .map(|h| hex_to_array::<32>(h))
        .collect::<Result<_>>()
        .context("failed to decode commitment leaves")?;

    // 3. Build ProofPublicValues.
    let prev_checkpoint_hash: [u8; 32] =
        hex_to_array::<32>(&args.checkpoint).context("failed to parse --checkpoint")?;

    // Collect new commitment hashes from the fetched blocks.
    let mut new_commitment_hashes: Vec<[u8; 32]> = Vec::new();
    for block in &blocks {
        for tx_extra in &block.tx_extras {
            let hashes = parse_heat_commitments(tx_extra);
            new_commitment_hashes.extend(hashes);
        }
    }

    // Extend prev_leaves with the new commitments to form the full leaf set.
    let mut all_leaves = prev_leaves.clone();
    all_leaves.extend_from_slice(&new_commitment_hashes);

    let new_merkle_root = compute_merkle_root(&all_leaves);
    let new_checkpoint_hash =
        compute_checkpoint_hash(&new_merkle_root, args.to_height, &all_leaves);

    let public = ProofPublicValues {
        prev_checkpoint_hash,
        new_checkpoint_hash,
        new_merkle_root,
        height_start: args.from_height,
        height_end: args.to_height,
        difficulty_target: args.difficulty_target.unwrap_or(0),
    };

    // 4. Build CircuitWitness.
    let witness = CircuitWitness {
        blocks,
        prev_leaves,
        public,
    };

    // 5. Run SP1 proof.
    let prover = ProverClient::network();

    let mut stdin = SP1Stdin::new();
    stdin.write(&witness);

    let (_, _report) = prover
        .execute(&circuit_elf, stdin.clone())
        .run()
        .context("SP1 circuit execution failed")?;

    // Generate the actual proof.
    let (pk, _vk) = prover.setup(&circuit_elf);
    let proof = prover
        .prove(&pk, stdin)
        .run()
        .context("SP1 proof generation failed")?;

    // 6. Serialise proof to output file.
    let proof_bytes = bincode_encode(&proof)?;
    std::fs::write(&args.out, &proof_bytes)
        .with_context(|| format!("failed to write proof to {}", args.out))?;

    println!(
        "Proof written to {} ({} bytes)",
        args.out,
        proof_bytes.len()
    );
    println!(
        "new_merkle_root:      {}",
        fmt_hex32(&witness.public.new_merkle_root)
    );
    println!(
        "new_checkpoint_hash:  {}",
        fmt_hex32(&witness.public.new_checkpoint_hash)
    );

    Ok(())
}

/// Encode an SP1 proof to bytes using serde_json as a portable format.
/// SP1 proofs implement serde::Serialize.
fn bincode_encode<T: Serialize>(value: &T) -> Result<Vec<u8>> {
    serde_json::to_vec(value).context("failed to serialise proof")
}

// ---------------------------------------------------------------------------
// `claim` subcommand
// ---------------------------------------------------------------------------

/// Output JSON written to --out.
#[derive(Serialize)]
struct ClaimOutput {
    preimage: String,
    commitment: String,
    merkle_proof: Vec<String>,
    leaf_index: usize,
    recipient: String,
    calldata_hint: String,
}

fn run_claim(args: ClaimArgs) -> Result<()> {
    // 1. Decode preimage (56 bytes).
    let preimage_bytes = hex_to_vec(&args.preimage).context("failed to decode --preimage")?;
    if preimage_bytes.len() != 56 {
        bail!(
            "--preimage must be 56 bytes (112 hex chars), got {} bytes",
            preimage_bytes.len()
        );
    }

    // 2. Compute commitment = keccak256(preimage).
    let computed_commitment = keccak256(&preimage_bytes);
    let computed_commitment_hex = fmt_hex32(&computed_commitment);

    // Cross-check against the provided --commitment flag if non-empty.
    let provided_commitment_stripped = args
        .commitment
        .strip_prefix("0x")
        .unwrap_or(&args.commitment);
    if !provided_commitment_stripped.is_empty() {
        let provided_bytes =
            hex_to_array::<32>(&args.commitment).context("failed to parse --commitment")?;
        if computed_commitment != provided_bytes {
            bail!(
                "computed commitment {} does not match provided --commitment {}",
                computed_commitment_hex,
                args.commitment,
            );
        }
    }

    // 3. Fetch merkle proof from RPC.
    let client = reqwest::blocking::Client::new();
    let proof_url = format!("{}/get_commitment_merkle_proof", args.rpc);
    let proof_body = serde_json::json!({
        "commitment": computed_commitment_hex,
    });
    let proof_resp: MerkleProofResponse = client
        .post(&proof_url)
        .json(&proof_body)
        .send()
        .with_context(|| format!("POST {proof_url} failed"))?
        .error_for_status()
        .with_context(|| format!("POST {proof_url} returned error status"))?
        .json()
        .with_context(|| format!("failed to parse response from {proof_url}"))?;

    let merkle_proof_hex: Vec<String> = proof_resp
        .proof
        .iter()
        .map(|h| {
            // Normalise to 0x-prefixed form.
            let stripped = h.strip_prefix("0x").unwrap_or(h);
            format!("0x{}", stripped)
        })
        .collect();

    // 4. Write output JSON.
    let recipient = if args.recipient.starts_with("0x") {
        args.recipient.clone()
    } else {
        format!("0x{}", args.recipient)
    };

    let output = ClaimOutput {
        preimage: fmt_hex(&preimage_bytes),
        commitment: computed_commitment_hex,
        merkle_proof: merkle_proof_hex,
        leaf_index: proof_resp.leaf_index,
        recipient,
        calldata_hint: "call HEATClaimer.claimHEAT(preimage, merkleProof, leafIndex, recipient)"
            .to_string(),
    };

    let json = serde_json::to_string_pretty(&output).context("failed to serialise claim output")?;
    std::fs::write(&args.out, &json)
        .with_context(|| format!("failed to write claim output to {}", args.out))?;

    println!("Claim data written to {}", args.out);
    println!("commitment:  {}", output.commitment);
    println!("leaf_index:  {}", output.leaf_index);

    Ok(())
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Commands::Prove(args) => run_prove(args),
        Commands::Claim(args) => run_claim(args),
    }
}
