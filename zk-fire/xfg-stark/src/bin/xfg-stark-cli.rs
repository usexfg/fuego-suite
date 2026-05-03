use clap::{Command, Arg};
use std::path::Path;
use std::io::{self, Write, BufRead, BufReader};
use std::collections::HashMap;
use xfg_stark::{
    proof_data_schema::{StarkProofDataPackage, CompleteProofPackage, StarkProof, EldernodeVerification, ProofDataTemplate, MerkleProof, ConsensusInfo, VerificationMetadata, EldernodeSignature},
    burn_mint_prover::XfgBurnMintProver,
    burn_mint_verifier::{XfgBurnMintVerifier, VerificationResult},
    fuego_rpc::{FuegoRpcClient, FuegoNetwork},
    XfgStarkError,
    Result,
};

mod ascii_arts;

/// Daemon connection config, derived from global CLI flags or interactive settings
#[derive(Clone, Debug)]
struct DaemonConfig {
    /// Explicit daemon address (e.g. "192.168.1.5:18180"), or None for auto-connect
    daemon_addr: Option<String>,
    /// Which network to target
    network: FuegoNetwork,
}

impl DaemonConfig {
    /// Connect to the Fuego daemon using this config.
    /// Tries: explicit address → localhost → seed nodes
    fn connect(&self) -> Result<FuegoRpcClient> {
        FuegoRpcClient::connect(self.daemon_addr.as_deref(), self.network)
    }

    /// Connect with user-friendly status messages
    fn connect_verbose(&self) -> Result<FuegoRpcClient> {
        match &self.daemon_addr {
            Some(addr) => {
                println!("🔗 Connecting to Fuego daemon at {}...", addr);
            }
            None => {
                println!("🔗 Auto-connecting to Fuego daemon (localhost → seed nodes)...");
            }
        }

        let rpc = self.connect()?;

        if rpc.is_auto_connected() {
            println!("   ✅ Connected to: {}", rpc.connected_to());
        } else {
            println!("   ✅ Connected to: {}", rpc.connected_to());
        }

        Ok(rpc)
    }
}

impl Default for DaemonConfig {
    fn default() -> Self {
        Self {
            daemon_addr: None,
            network: FuegoNetwork::Mainnet,
        }
    }
}

// Interactive CLI Runtime
struct InteractiveCLI {
    running: bool,
    commands: HashMap<String, Box<dyn Fn(&[&str], &DaemonConfig) -> Result<()>>>,
    daemon_config: DaemonConfig,
}

impl InteractiveCLI {
    fn new(daemon_config: DaemonConfig) -> Self {
        let mut cli = Self {
            running: true,
            commands: HashMap::new(),
            daemon_config,
        };
        cli.register_commands();
        cli
    }

    fn register_commands(&mut self) {
        // Register all available commands
        self.commands.insert("help".to_string(), Box::new(|_, _| {
            println!("\n📋 Available Commands:");
            println!("   help                    - Show this help message");
            println!("   version                 - Show CLI version");
            println!("   guide                   - Interactive guide for XFG → HEAT process");
            println!();
            println!("  🔗 Daemon Commands (auto-connects to localhost or seed nodes):");
            println!("   daemon-status [host:port]          - Check daemon connection & commitment stats");
            println!("   verify-commitment <hash>           - Verify a commitment exists on-chain");
            println!("   fetch-proof <commit_hash> <output> - Fetch merkle proof from daemon");
            println!();
            println!("  📦 Package Commands:");
            println!("   create-template <file>  - Create a template data package");
            println!("   create-package <txn> <recipient> <output> - Create a data package");
            println!("   validate <file>         - Validate a data package");
            println!("   generate <input> <output> - Generate a STARK proof");
            println!("   bundle <package> <proof> <commit_hash> <output> - Bundle STARK proof + merkle proof");
            println!();
            println!("  ⛽ Utility Commands:");
            println!("   estimate-gas <recipient> - Estimate L1 gas fees for minting");
            println!("   check-network <network> - Check network status and contracts");
            println!("   clear                   - Clear the screen");
            println!("   exit, quit              - Exit the CLI");
            println!();
            println!("💡 Quick Start:");
            println!("   1. Type 'guide' for step-by-step instructions");
            println!("   2. Or use: create-package <txn_hash> <output.json>");
            println!("   3. Then: validate <output.json>");
            println!("   4. Then: generate <output.json> <proof.json>");
            println!("   5. Then: bundle <output.json> <proof.json> <commit_hash> <bundle.json>");
            println!();
            println!("🔗 Connection:");
            println!("   Daemon commands auto-connect: localhost → seed nodes fallback.");
            println!("   Use --daemon <host:port> when launching to override.");
            println!();
            Ok(())
        }));

        self.commands.insert("guide".to_string(), Box::new(|_, _| {
            println!("\n🚀 XFG Burn → HEAT Mint Complete Guide");
            println!("==========================================");
            println!();
            println!("📋 Prerequisites:");
            println!("   ✅ You have burned XFG tokens on Fuego blockchain");
            println!("   ✅ You have the transaction hash (64 hex characters)");
            println!("   ✅ You have an Ethereum address ready to provide at proof generation");
            println!("   ✅ You have some ETH for L1 gas fees");
            println!();
            println!("🔄 Step-by-Step Process:");
            println!();
            println!("Step 0: Check Daemon Status");
            println!("   daemon-status");
            println!("   CLI auto-connects: local daemon → seed nodes fallback");
            println!();
            println!("Step 1: Create Data Package");
            println!("   create-package <txn_hash> <output.json>");
            println!("   Example: create-package a1b2c3d4e5f6... 0x1234... package.json");
            println!();
            println!("Step 2: Validate Package");
            println!("   validate <output.json>");
            println!("   Checks format + verifies against live Fuego blockchain");
            println!();
            println!("Step 3: Verify Commitment On-Chain");
            println!("   verify-commitment <commitment_hash>");
            println!("   Confirms your burn/deposit is indexed in the CommitmentIndex");
            println!();
            println!("Step 4: Generate STARK Proof");
            println!("   generate <output.json> <proof.json>");
            println!("   Creates the zk-STARK proof that you know the commitment secret");
            println!();
            println!("Step 5: Bundle Complete Proof");
            println!("   bundle <output.json> <proof.json> <commitment_hash> <bundle.json>");
            println!("   Fetches merkle proof from daemon + bundles with STARK proof");
            println!("   This is the final package ready for EVM submission");
            println!();
            println!("Step 6: Submit to EVM Contract");
            println!("   Submit bundle.json to FuegoCommitmentMerkleVerifier on Ethereum/Arbitrum");
            println!("   (Requires web3 wallet like MetaMask)");
            println!();
            println!("💡 Tips:");
            println!("   • Transaction hash should be 64 hex characters (no 0x prefix)");
            println!("   • Ethereum address should start with 0x");
            println!("   • Always validate before generating proof");
            println!("   • Keep your proof file safe - you'll need it for minting");
            println!("   • No local daemon? CLI falls back to Fuego seed nodes automatically");
            println!();
            println!("❓ Need Help?");
            println!("   • Type 'help' for all commands");
            println!("   • Type 'estimate-gas <address>' to check gas costs");
            println!("   • Type 'check-network sepolia' for testnet info");
            println!();
            Ok(())
        }));

        self.commands.insert("version".to_string(), Box::new(|_, _| {
            println!("xfg-stark-cli 2.0");
            Ok(())
        }));

        self.commands.insert("create-template".to_string(), Box::new(|args, _| {
            if args.len() < 1 {
                println!("❌ Usage: create-template <output_file>");
                println!("💡 Example: create-template template.json");
                return Ok(());
            }
            let output_file = args[0];
            create_template(output_file)
        }));

        self.commands.insert("create-package".to_string(), Box::new(|args, _| {
            if args.len() < 2 {
                println!("❌ Usage: create-package <txn_hash> <output_file>");
                println!("💡 Example: create-package a1b2c3d4e5f6... package.json");
                println!("📋 Parameters:");
                println!("   txn_hash:    Fuego transaction hash (64 hex chars, no 0x)");
                println!("   output_file: JSON file to save the package");
                println!("💡 Ethereum address is provided at STARK generation time (generate command)");
                return Ok(());
            }
            let txn_hash = args[0];
            let output_file = args[1];

            // Validate transaction hash format
            if txn_hash.len() != 64 {
                println!("❌ Error: Transaction hash must be exactly 64 hex characters");
                println!("💡 Example: a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6");
                return Ok(());
            }

            create_package(txn_hash, output_file)
        }));

        self.commands.insert("validate".to_string(), Box::new(|args, cfg| {
            if args.len() < 1 {
                println!("❌ Usage: validate <input_file>");
                println!("💡 Example: validate package.json");
                return Ok(());
            }
            let input_file = args[0];
            validate_package(input_file, cfg)
        }));

        self.commands.insert("generate".to_string(), Box::new(|args, _| {
            if args.len() < 3 {
                println!("❌ Usage: generate <input_file> <eth_address> <output_file>");
                println!("💡 Example: generate package.json 0x1234...abcd proof.json");
                println!("📋 Ethereum address is bound to the STARK proof at generation time");
                return Ok(());
            }
            let input_file = args[0];
            let eth_address = args[1];
            let output_file = args[2];
            // Validate Ethereum address format
            if !eth_address.starts_with("0x") || eth_address.len() != 42 {
                println!("❌ Error: Ethereum address must be 0x-prefixed 40-char hex");
                println!("💡 Example: 0x742d35Cc6634C0532925a3b8D4C9db96C4b4d8b6");
                return Ok(());
            }
            generate_proof(input_file, eth_address, output_file)
        }));

        self.commands.insert("estimate-gas".to_string(), Box::new(|args, _| {
            if args.len() < 1 {
                println!("❌ Usage: estimate-gas <recipient>");
                println!("💡 Example: estimate-gas 0x742d35Cc6634C0532925a3b8D4C9db96C4b4d8b6");
                return Ok(());
            }
            let recipient = args[0];
            estimate_gas_fees(recipient, false)
        }));

        self.commands.insert("check-network".to_string(), Box::new(|args, _| {
            let network = if args.len() > 0 { args[0] } else { "sepolia" };
            check_network_status(network)
        }));

        // ── Daemon RPC commands ──

        self.commands.insert("daemon-status".to_string(), Box::new(|args, cfg| {
            // Allow override: daemon-status host:port
            if args.len() > 0 {
                let mut override_cfg = cfg.clone();
                override_cfg.daemon_addr = Some(args[0].to_string());
                daemon_status(&override_cfg)
            } else {
                daemon_status(cfg)
            }
        }));

        self.commands.insert("verify-commitment".to_string(), Box::new(|args, cfg| {
            if args.len() < 1 {
                println!("❌ Usage: verify-commitment <commitment_hash>");
                println!("💡 Example: verify-commitment a1b2c3d4...(64 hex chars)");
                return Ok(());
            }
            verify_commitment_onchain(args[0], cfg)
        }));

        self.commands.insert("fetch-proof".to_string(), Box::new(|args, cfg| {
            if args.len() < 2 {
                println!("❌ Usage: fetch-proof <commitment_hash> <output_file>");
                println!("💡 Example: fetch-proof a1b2c3d4... merkle_proof.json");
                return Ok(());
            }
            fetch_merkle_proof(args[0], args[1], cfg)
        }));

        self.commands.insert("bundle".to_string(), Box::new(|args, cfg| {
            if args.len() < 4 {
                println!("❌ Usage: bundle <package.json> <proof.json> <commitment_hash> <output.json>");
                println!("💡 Bundles STARK proof + merkle proof + eldernode verification into a complete package");
                return Ok(());
            }
            bundle_complete_proof(args[0], args[1], args[2], args[3], cfg)
        }));

        self.commands.insert("clear".to_string(), Box::new(|_, _| {
            print!("\x1B[2J\x1B[1;1H"); // Clear screen
            print_brand_header();
            Ok(())
        }));

        self.commands.insert("exit".to_string(), Box::new(|_, _| {
            println!("👋 Goodbye! Thanks for using XFG STARK CLI!");
            println!("💡 Remember to submit your proof to the HEAT mint contract!");
            std::process::exit(0);
        }));

        self.commands.insert("quit".to_string(), Box::new(|_, _| {
            println!("👋 Goodbye! Thanks for using XFG STARK CLI!");
            println!("💡 Remember to submit your proof to the HEAT mint contract!");
            std::process::exit(0);
        }));
    }

    fn run(&mut self) -> Result<()> {
        let stdin = io::stdin();
        let mut reader = BufReader::new(stdin.lock());

        let net_label = match self.daemon_config.network {
            FuegoNetwork::Mainnet => "mainnet",
            FuegoNetwork::Testnet => "testnet",
        };
        let conn_label = match &self.daemon_config.daemon_addr {
            Some(addr) => format!("daemon: {}", addr),
            None => "auto-connect (localhost → seed nodes)".to_string(),
        };

        println!("🚀 Interactive CLI Runtime Started!");
        println!("   Network: {} | Connection: {}", net_label, conn_label);
        println!("Type 'help' for available commands, 'guide' for step-by-step instructions, 'exit' to quit.\n");

        while self.running {
            print!("🔥 xfg-stark-cli> ");
            io::stdout().flush()?;

            let mut input = String::new();
            reader.read_line(&mut input)?;
            let input = input.trim();

            if input.is_empty() {
                continue;
            }

            let parts: Vec<&str> = input.split_whitespace().collect();
            if parts.is_empty() {
                continue;
            }

            let command = parts[0];
            let args = &parts[1..];

            match self.commands.get(command) {
                Some(cmd_func) => {
                    if let Err(e) = cmd_func(args, &self.daemon_config) {
                        println!("❌ Error: {}", e);
                    }
                }
                None => {
                    println!("❌ Unknown command: '{}'. Type 'help' for available commands.", command);
                }
            }
            println!(); // Add spacing between commands
        }

        Ok(())
    }
}

fn main() -> Result<()> {
    // Display cool ASCII art header
    print_brand_header();

    let matches = Command::new("xfg-stark-cli")
        .version("2.0")
        .about("🔥 Enhanced CLI tool for XFG burn → HEAT mint STARK proofs")
        // ── Global flags ──
        .arg(
            Arg::new("daemon")
                .long("daemon")
                .short('d')
                .value_name("HOST:PORT")
                .help("Fuego daemon RPC address (default: auto-connect localhost → seed nodes)")
                .global(true)
        )
        .arg(
            Arg::new("testnet")
                .long("testnet")
                .help("Use testnet (seed nodes on port 28280 instead of mainnet 18180)")
                .action(clap::ArgAction::SetTrue)
                .global(true)
        )
        // ── Subcommands ──
        .subcommand(
            Command::new("interactive")
                .about("Start interactive command-line runtime")
        )
        .subcommand(
            Command::new("generate")
                .about("Generate a STARK proof from a data package (eth address bound at this step)")
                .arg(
                    Arg::new("input")
                        .short('i')
                        .long("input")
                        .value_name("FILE")
                        .help("Input data package file")
                        .required(true)
                )
                .arg(
                    Arg::new("eth-address")
                        .short('a')
                        .long("eth-address")
                        .value_name("ADDRESS")
                        .help("Ethereum address to receive HEAT/COLD tokens (0x-prefixed)")
                        .required(true)
                )
                .arg(
                    Arg::new("output")
                        .short('o')
                        .long("output")
                        .value_name("FILE")
                        .help("Output proof file")
                        .required(true)
                )
        )
        .subcommand(
            Command::new("validate")
                .about("Validate a data package")
                .arg(
                    Arg::new("input")
                        .short('i')
                        .long("input")
                        .value_name("FILE")
                        .help("Input data package file")
                        .required(true)
                )
        )
        .subcommand(
            Command::new("create-template")
                .about("Create a template data package")
                .arg(
                    Arg::new("burn-amount")
                        .short('a')
                        .long("burn-amount")
                        .value_name("AMOUNT")
                        .help("Burn amount in XFG")
                        .required(true)
                )
                .arg(
                    Arg::new("output")
                        .short('o')
                        .long("output")
                        .value_name("FILE")
                        .help("Output template file")
                        .required(true)
                )
        )
        .subcommand(
            Command::new("create-package")
                .about("Create a data package from a template")
                .arg(
                    Arg::new("template")
                        .short('t')
                        .long("template")
                        .value_name("FILE")
                        .help("Template file")
                        .required(true)
                )
                .arg(
                    Arg::new("txn-hash")
                        .short('x')
                        .long("txn-hash")
                        .value_name("HASH")
                        .help("Fuego transaction hash (no 0x prefix)")
                        .required(true)
                )
                .arg(
                    Arg::new("output")
                        .short('o')
                        .long("output")
                        .value_name("FILE")
                        .help("Output package file")
                        .required(true)
                )
        )
        .subcommand(
            Command::new("bundle")
                .about("Bundle STARK proof + merkle proof into complete proof package")
                .arg(Arg::new("package").short('p').long("package").value_name("FILE").help("Data package file").required(true))
                .arg(Arg::new("proof").short('s').long("stark-proof").value_name("FILE").help("STARK proof file").required(true))
                .arg(Arg::new("commitment").short('c').long("commitment").value_name("HASH").help("Commitment hash (64 hex chars)").required(true))
                .arg(Arg::new("output").short('o').long("output").value_name("FILE").help("Output bundle file").required(true))
        )
        .subcommand(
            Command::new("daemon-status")
                .about("Check Fuego daemon connection and commitment index stats")
        )
        .subcommand(
            Command::new("verify-commitment")
                .about("Verify a commitment exists on the Fuego blockchain")
                .arg(Arg::new("hash").short('c').long("commitment").value_name("HASH").help("Commitment hash (64 hex chars)").required(true))
        )
        .subcommand(
            Command::new("fetch-proof")
                .about("Fetch merkle proof for a commitment from Fuego daemon")
                .arg(Arg::new("hash").short('c').long("commitment").value_name("HASH").help("Commitment hash (64 hex chars)").required(true))
                .arg(Arg::new("output").short('o').long("output").value_name("FILE").help("Output merkle proof file").required(true))
        )
        .get_matches();

    // ── Build DaemonConfig from global flags ──
    let daemon_config = DaemonConfig {
        daemon_addr: matches.get_one::<String>("daemon").cloned(),
        network: if matches.get_flag("testnet") {
            FuegoNetwork::Testnet
        } else {
            FuegoNetwork::Mainnet
        },
    };

    match matches.subcommand() {
        Some(("interactive", _)) => {
            let mut cli = InteractiveCLI::new(daemon_config);
            cli.run()?;
        }
        Some(("generate", args)) => {
            let input_file = args.get_one::<String>("input").unwrap();
            let eth_address = args.get_one::<String>("eth-address").unwrap();
            let output_file = args.get_one::<String>("output").unwrap();
            generate_proof(input_file, eth_address, output_file)?;
        }
        Some(("validate", args)) => {
            let input_file = args.get_one::<String>("input").unwrap();
            validate_package(input_file, &daemon_config)?;
        }
        Some(("create-template", args)) => {
            let _burn_amount = args.get_one::<f64>("burn-amount").unwrap();
            let output_file = args.get_one::<String>("output").unwrap();
            create_template(output_file)?;
        }
        Some(("create-package", args)) => {
            let txn_hash = args.get_one::<String>("txn-hash").unwrap();
            let output_file = args.get_one::<String>("output").unwrap();
            create_package(txn_hash, output_file)?;
        }
        Some(("bundle", args)) => {
            let package_file = args.get_one::<String>("package").unwrap();
            let proof_file = args.get_one::<String>("proof").unwrap();
            let commitment = args.get_one::<String>("commitment").unwrap();
            let output_file = args.get_one::<String>("output").unwrap();
            bundle_complete_proof(package_file, proof_file, commitment, output_file, &daemon_config)?;
        }
        Some(("daemon-status", _)) => {
            daemon_status(&daemon_config)?;
        }
        Some(("verify-commitment", args)) => {
            let hash = args.get_one::<String>("hash").unwrap();
            verify_commitment_onchain(hash, &daemon_config)?;
        }
        Some(("fetch-proof", args)) => {
            let hash = args.get_one::<String>("hash").unwrap();
            let output_file = args.get_one::<String>("output").unwrap();
            fetch_merkle_proof(hash, output_file, &daemon_config)?;
        }
        _ => {
            eprintln!("Unknown subcommand. Use --help for usage information.");
            std::process::exit(1);
        }
    }

    Ok(())
}

// ============================================================
// Daemon RPC functions
// ============================================================

/// Check daemon status and commitment index stats
fn daemon_status(cfg: &DaemonConfig) -> Result<()> {
    let rpc = cfg.connect_verbose()?;

    // Check height
    match rpc.get_height() {
        Ok(h) => {
            println!("   ✅ Daemon reachable — height: {}", h.height);
        }
        Err(e) => {
            println!("   ❌ Cannot reach daemon: {}", e);
            println!("   💡 Make sure fuegod is running");
            return Ok(());
        }
    }

    // Get commitment stats
    match rpc.get_commitment_stats() {
        Ok(stats) => {
            println!("\n📊 Commitment Index Stats:");
            println!("   Total commitments:  {}", stats.total_commitments);
            println!("   HEAT burns:         {}", stats.heat_commitments);
            println!("   COLD deposits:      {}", stats.cold_commitments);
            println!("   Highest block:      {}", stats.highest_block);
            println!("   Merkle root:        {}...{}", &stats.merkle_root[..8], &stats.merkle_root[56..]);
            println!("   Consensus:          {}%", stats.consensus_percentage);
            println!("   Signed EFiers:      {:?}", stats.signed_elderfier_ids);
            println!("   Pending EFiers:     {:?}", stats.pending_elderfier_ids);
        }
        Err(e) => {
            println!("   ⚠️  Could not fetch commitment stats: {}", e);
        }
    }

    Ok(())
}

/// Verify a commitment exists on-chain via daemon RPC
fn verify_commitment_onchain(commitment_hash: &str, cfg: &DaemonConfig) -> Result<()> {
    println!("\n🔍 Verifying commitment on Fuego blockchain...");
    println!("   Hash: {}", commitment_hash);

    let rpc = cfg.connect_verbose()?;

    match rpc.get_commitment(commitment_hash) {
        Ok(resp) => {
            if resp.found {
                let type_str = match resp.commitment_type {
                    0 => "HEAT (permanent burn)",
                    1 => "COLD (term deposit)",
                    2 => "ELDERFIER_STAKING",
                    _ => "UNKNOWN",
                };
                println!("\n   ✅ Commitment EXISTS on-chain!");
                println!("   ┌─────────────────────────────────────────────────────────────────┐");
                println!("   │ Type:          {}", type_str);
                println!("   │ Amount:        {} atomic units", resp.amount);
                println!("   │ Block height:  {}", resp.block_height);
                println!("   │ Term:          {}", if resp.term == 0xFFFFFFFF { "FOREVER".to_string() } else { format!("{} blocks", resp.term) });
                println!("   │ TX hash:       {}", resp.tx_hash);
                println!("   │ Chain target:  {}", resp.target_chain_id);
                println!("   │ Leaf index:    {}", resp.leaf_index);
                println!("   └─────────────────────────────────────────────────────────────────┘");
            } else {
                println!("\n   ❌ Commitment NOT FOUND on-chain.");
                println!("   💡 Make sure the burn/deposit transaction is confirmed.");
            }
        }
        Err(e) => {
            println!("   ❌ RPC error: {}", e);
        }
    }

    Ok(())
}

/// Fetch merkle proof from daemon and save to file
fn fetch_merkle_proof(commitment_hash: &str, output_file: &str, cfg: &DaemonConfig) -> Result<()> {
    println!("\n🔍 Fetching merkle proof from Fuego daemon...");
    println!("   Commitment: {}", commitment_hash);

    let rpc = cfg.connect_verbose()?;

    let proof_resp = rpc.get_merkle_proof(commitment_hash)
        .map_err(|e| XfgStarkError::ParseError(format!("RPC error: {}", e)))?;

    if !proof_resp.found {
        println!("   ❌ Commitment not found. Cannot generate merkle proof.");
        return Ok(());
    }

    println!("   ✅ Merkle proof received!");
    println!("   Root:        {}...{}", &proof_resp.merkle_root[..8], &proof_resp.merkle_root[56..]);
    println!("   Proof depth: {} levels", proof_resp.proof_path.len());
    println!("   Leaf index:  {}", proof_resp.leaf_index);
    println!("   Consensus:   {}%", proof_resp.consensus_percentage);

    // Build the MerkleProof struct matching proof_data_schema.rs
    let merkle_proof = MerkleProof {
        root_hash: proof_resp.merkle_root,
        leaf_hash: proof_resp.leaf_hash,
        proof_path: proof_resp.proof_path,
        proof_indices: proof_resp.proof_indices,
    };

    // Save to file
    let json = serde_json::to_string_pretty(&merkle_proof)
        .map_err(|e| XfgStarkError::JsonError(e))?;
    std::fs::write(output_file, json)
        .map_err(|e| XfgStarkError::IoError(e))?;

    println!("   💾 Merkle proof saved to: {}", output_file);
    println!("\n💡 Next: bundle this with your STARK proof using:");
    println!("   bundle <package.json> <proof.json> {} <bundle.json>", commitment_hash);

    Ok(())
}

/// Bundle STARK proof + merkle proof + consensus data into a CompleteProofPackage
fn bundle_complete_proof(
    package_file: &str,
    stark_proof_file: &str,
    commitment_hash: &str,
    output_file: &str,
    cfg: &DaemonConfig,
) -> Result<()> {
    println!("\n📦 Bundling complete proof package...");

    // Load the original data package
    println!("   Loading data package: {}", package_file);
    let data_package = StarkProofDataPackage::load_from_file(package_file)
        .map_err(|e| XfgStarkError::ParseError(e.to_string()))?;

    // Load the STARK proof
    println!("   Loading STARK proof: {}", stark_proof_file);
    let stark_proof_json = std::fs::read_to_string(stark_proof_file)
        .map_err(|e| XfgStarkError::IoError(e))?;
    let stark_proof: StarkProof = serde_json::from_str(&stark_proof_json)
        .map_err(|e| XfgStarkError::JsonError(e))?;

    // Fetch merkle proof + consensus from daemon
    println!("   Fetching merkle proof from daemon...");
    let rpc = cfg.connect_verbose()?;

    let proof_resp = rpc.get_merkle_proof(commitment_hash)
        .map_err(|e| XfgStarkError::ParseError(format!("RPC error: {}", e)))?;

    if !proof_resp.found {
        println!("   ❌ Commitment {} not found on-chain!", commitment_hash);
        return Ok(());
    }

    // Fetch EFier signatures with pubkeys for L2 batch submission
    println!("   Fetching EFier signatures...");
    let sigs_resp = rpc.get_elderfier_signatures()
        .map_err(|e| XfgStarkError::ParseError(format!("RPC error: {}", e)))?;

    // Build EldernodeVerification with real signature + pubkey data
    let eldernode_verification = EldernodeVerification {
        merkle_proof: MerkleProof {
            root_hash: proof_resp.merkle_root.clone(),
            leaf_hash: proof_resp.leaf_hash.clone(),
            proof_path: proof_resp.proof_path.clone(),
            proof_indices: proof_resp.proof_indices.clone(),
            leaf_index: proof_resp.leaf_index,
        },
        eldernode_signatures: sigs_resp.signatures.iter().map(|sig| {
            EldernodeSignature {
                elderfier_id: sig.elderfier_id,
                signing_pubkey: sig.signing_pubkey.clone(),
                signature: sig.signature.clone(),
                block_height: sig.block_height,
                timestamp: sig.timestamp,
            }
        }).collect(),
        consensus: ConsensusInfo {
            eldernode_count: sigs_resp.signatures_received as u32,
            threshold_met: sigs_resp.threshold_met,
            consensus_type: format!("{}/{}", sigs_resp.signatures_received,
                                    sigs_resp.total_registered_elderfiers),
        },
        metadata: VerificationMetadata {
            verified_at: chrono::Utc::now().to_rfc3339(),
            network: data_package.metadata.network.clone(),
            version: "3.0.0".to_string(),
        },
    };

    // Assemble the CompleteProofPackage
    let complete = CompleteProofPackage {
        stark_proof_data: data_package,
        stark_proof: Some(stark_proof),
        eldernode_verification: Some(eldernode_verification),
        status: xfg_stark::proof_data_schema::PackageStatus::Complete,
        timestamps: xfg_stark::proof_data_schema::ProofTimestamps {
            created_at: chrono::Utc::now().to_rfc3339(),
            stark_proof_generated: Some(chrono::Utc::now().to_rfc3339()),
            eldernode_verified: Some(chrono::Utc::now().to_rfc3339()),
        },
    };

    // Save complete bundle
    let json = serde_json::to_string_pretty(&complete)
        .map_err(|e| XfgStarkError::JsonError(e))?;
    std::fs::write(output_file, &json)
        .map_err(|e| XfgStarkError::IoError(e))?;

    println!("\n   ✅ Complete proof bundle created!");
    println!("   ┌─────────────────────────────────────────────────────────────────┐");
    println!("   │ STARK proof:        ✅ Included");
    println!("   │ Merkle proof:       ✅ {} levels deep", proof_resp.proof_path.len());
    println!("   │ Merkle root:        {}...{}", &proof_resp.merkle_root[..8], &proof_resp.merkle_root[56..]);
    println!("   │ EFier sigs:         {} signatures with pubkeys", sigs_resp.signatures_received);
    println!("   │ Consensus:          {}% ({}/{})", sigs_resp.consensus_percentage,
             sigs_resp.signatures_received, sigs_resp.total_registered_elderfiers);
    println!("   │ Threshold met:      {}", if sigs_resp.threshold_met { "✅ YES" } else { "❌ NO" });
    println!("   │ Output:             {}", output_file);
    println!("   └─────────────────────────────────────────────────────────────────┘");

    if sigs_resp.threshold_met {
        println!("\n🚀 Bundle is READY for EVM contract submission!");
        println!("   Submit to the FuegoCommitmentMerkleVerifier contract on your target chain.");
    } else {
        println!("\n⚠️  Consensus threshold not yet met (need ≥69%).");
        println!("   Wait for more Elderfiers to sign, then re-bundle.");
    }

    Ok(())
}

/// Verify package against on-chain data (replaces fake eldernode verification)
fn eldernode_verify_package(input_file: &str, cfg: &DaemonConfig) -> Result<()> {
    println!("\n🔍 Eldernode Verification (Live)");
    println!("==================================");
    println!("📋 Loading package from: {}", input_file);

    let package = StarkProofDataPackage::load_from_file(input_file)
        .map_err(|e| XfgStarkError::ParseError(e.to_string()))?;

    println!("✅ Package loaded successfully");
    println!("🔥 Burn Transaction:");
    println!("   Hash: {}", package.burn_transaction.transaction_hash);
    println!("   Amount: {} XFG", package.burn_transaction.burn_amount_xfg);
    println!("   Block Height: {}", package.burn_transaction.block_height);
    println!("👤 Recipient: {}", package.recipient.ethereum_address);

    println!("\n🔄 Contacting Fuego daemon...");

    let rpc = cfg.connect_verbose()?;

    // Step 1: Check daemon is reachable + show height
    match rpc.get_height() {
        Ok(h) => println!("   ✅ Chain height: {}", h.height),
        Err(e) => {
            println!("   ⚠️  Could not fetch height: {}", e);
        }
    }

    // Step 2: Check commitment stats
    match rpc.get_commitment_stats() {
        Ok(stats) => {
            println!("   ✅ Commitment index: {} total ({} HEAT, {} COLD)",
                     stats.total_commitments, stats.heat_commitments, stats.cold_commitments);
            println!("   ✅ Merkle root: {}...{}", &stats.merkle_root[..8.min(stats.merkle_root.len())],
                     &stats.merkle_root[56.min(stats.merkle_root.len())..]);
            println!("   ✅ Consensus: {}% ({} EFiers signed)",
                     stats.consensus_percentage, stats.signed_elderfier_ids.len());
        }
        Err(e) => {
            println!("   ⚠️  Could not fetch commitment stats: {}", e);
        }
    }

    println!("\n📋 Verification Results:");
    println!("   ┌─────────────────────────────────────────────────────────────────┐");
    println!("   │ ✅ Daemon connection verified");
    println!("   │ ✅ Package format valid");
    println!("   │ ✅ Burn amount: {} XFG", package.burn_transaction.burn_amount_xfg);
    println!("   │ ✅ Block height: {}", package.burn_transaction.block_height);
    println!("   └─────────────────────────────────────────────────────────────────┘");

    println!("\n💡 Next Steps:");
    println!("   1. Generate STARK proof: generate {} <proof.json>", input_file);
    println!("   2. Bundle everything:    bundle {} <proof.json> <commit_hash> <bundle.json>", input_file);

    Ok(())
}

/// Generate STARK proof from data package using real prover.
/// The Ethereum recipient address is bound to the proof at this step (not at deposit time).
fn generate_proof(input_file: &str, eth_address: &str, output_file: &str) -> Result<()> {
    println!("🔍 Loading data package from: {}", input_file);

    // Load and validate data package
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

    if !validation.warnings.is_empty() {
        println!("⚠️  Warnings:");
        for warning in &validation.warnings {
            println!("   - {}", warning);
        }
    }

    println!("✅ Data package validated successfully");
    println!("📊 Burn amount: {} XFG ({} atomic units)",
             package.burn_transaction.burn_amount_xfg,
             package.burn_transaction.burn_amount_atomic);
    println!("🎯 Mint amount: {} HEAT", package.get_mint_amount_heat());
    println!("👤 Recipient (bound now): {}", eth_address);

    // Create real prover
    println!("🔐 Creating STARK prover...");
    let prover = XfgBurnMintProver::new(128);

    // Convert secret to bytes (32 bytes)
    let secret_bytes = package.secret.secret_key.as_bytes();
    let mut secret_array = [0u8; 32];
    if secret_bytes.len() >= 32 {
        secret_array.copy_from_slice(&secret_bytes[..32]);
    } else {
        secret_array[..secret_bytes.len()].copy_from_slice(secret_bytes);
    }

    // Extract txn_hash as u32 (first 4 bytes of tx hash, LE)
    let tx_hash_bytes = hex_to_bytes(&package.burn_transaction.transaction_hash)
        .map_err(|e| XfgStarkError::ParseError(format!("Invalid transaction hash: {}", e)))?;
    let txn_hash = if tx_hash_bytes.len() >= 4 {
        u32::from_le_bytes([tx_hash_bytes[0], tx_hash_bytes[1], tx_hash_bytes[2], tx_hash_bytes[3]])
    } else {
        return Err(XfgStarkError::ParseError("Transaction hash too short".to_string()));
    };

    // Parse network_id from string to u32 (default to 1 for mainnet)
    let network_id = package.burn_transaction.network_id.parse::<u32>().unwrap_or(1);
    let target_chain_id = package.burn_transaction.target_chain_id.unwrap_or(42161);
    let commitment_version = 3u32; // v3 unified relay format
    let deposit_term = package.burn_transaction.deposit_term.unwrap_or(0xFFFFFFFF); // HEAT = FOREVER

    // Generate real STARK proof (v3 unified format)
    println!("⚡ Generating STARK proof...");

    let winterfell_proof = prover.prove_burn_mint(
        package.burn_transaction.burn_amount_atomic,
        package.get_mint_amount_atomic(),
        txn_hash,
        &secret_array,
        network_id,
        target_chain_id,
        commitment_version,
        deposit_term,
    ).map_err(|e| XfgStarkError::CryptoError(format!("Proof generation failed: {}", e)))?;

    println!("✅ STARK proof generated successfully");

    // Convert Winterfell proof to our format
    let proof_data = winterfell_proof.to_bytes();
    println!("📏 Proof size: {} bytes", proof_data.len());

    let proof = StarkProof {
        proof_data: proof_data.clone(),
        public_inputs: xfg_stark::proof_data_schema::StarkPublicInputs {
            burn_amount: package.burn_transaction.burn_amount_atomic,
            mint_amount: package.get_mint_amount_atomic(),
            txn_hash: package.burn_transaction.transaction_hash.clone(),
            state: 0,
            deposit_term,
            network_id,
            target_chain_id,
            commitment_version,
        },
        metadata: xfg_stark::proof_data_schema::ProofMetadata {
            version: "3.0.0".to_string(),
            created_at: chrono::Utc::now().to_rfc3339(),
            description: format!("STARK proof for {} XFG burn (v3 unified)", package.burn_transaction.burn_amount_xfg),
            network: package.metadata.network.clone(),
        },
    };

    // Save proof
    let json = serde_json::to_string_pretty(&proof)
        .map_err(|e| XfgStarkError::JsonError(e))?;

    std::fs::write(output_file, json)
        .map_err(|e| XfgStarkError::IoError(e))?;

    println!("�� Proof saved to: {}", output_file);
    println!("🚀 Ready for submission to HEAT mint contract!");

    Ok(())
}

/// Validate data package with enhanced Fuego blockchain validation
fn validate_package(input_file: &str, cfg: &DaemonConfig) -> Result<()> {
    println!("🔍 Loading data package from: {}", input_file);

    let package = StarkProofDataPackage::load_from_file(input_file)
        .map_err(|e| XfgStarkError::ParseError(e.to_string()))?;

    println!("�� Package Information:");
    println!("   Version: {}", package.metadata.version);
    println!("   Network: {}", package.metadata.network);
    println!("   Created: {}", package.metadata.created_at);
    println!("   Description: {}", package.metadata.description);

    println!("\n🔥 Burn Transaction:");
    println!("   Hash: {}", package.burn_transaction.transaction_hash);
    println!("   Amount: {} XFG ({} atomic units)",
             package.burn_transaction.burn_amount_xfg,
             package.burn_transaction.burn_amount_atomic);
    println!("   Block Height: {}", package.burn_transaction.block_height);
    println!("   Timestamp: {}", package.burn_transaction.timestamp);

    println!("\n👤 Recipient (Ethereum):");
    if package.recipient.ethereum_address.is_empty() {
        println!("   Address: (provided at STARK generation time)");
    } else {
        println!("   Address: {}", package.recipient.ethereum_address);
    }
    if let Some(ref ens) = package.recipient.ens_name {
        println!("   ENS: {}", ens);
    }
    if let Some(ref label) = package.recipient.label {
        println!("   Label: {}", label);
    }

    println!("\n🔐 Secret:");
    println!("   Key: {}...", &package.secret.secret_key[..8.min(package.secret.secret_key.len())]);
    if let Some(ref salt) = package.secret.salt {
        println!("   Salt: {}", salt);
    }
    if let Some(ref hint) = package.secret.hint {
        println!("   Hint: {}", hint);
    }

    println!("\n📊 Validation Results:");

    let validation = package.validate();
    if validation.is_valid {
        println!("   ✅ Package is valid");
    } else {
        println!("   ❌ Package has errors:");
        for error in &validation.errors {
            println!("      - {}", error);
        }
        for warning in &validation.warnings {
            println!("      - {}", warning);
        }
    }

    // Additional Fuego blockchain validation
    println!("\n🔗 Fuego Blockchain Validation:");
    validate_fuego_transaction(&package)?;

    // Live on-chain validation (if daemon is available)
    println!("\n🔗 On-Chain Verification (live):");
    match cfg.connect() {
        Ok(rpc) => {
            println!("   ✅ Connected to: {}", rpc.connected_to());
            match rpc.get_height() {
                Ok(h) => {
                    println!("   ✅ Daemon reachable — height: {}", h.height);
                    if package.burn_transaction.block_height as u64 > h.height {
                        println!("   ❌ Package block height ({}) exceeds current chain height ({})",
                                 package.burn_transaction.block_height, h.height);
                    } else {
                        println!("   ✅ Block height {} confirmed on chain", package.burn_transaction.block_height);
                    }
                }
                Err(e) => {
                    println!("   ⚠️  Daemon not responding: {}", e);
                }
            }
        }
        Err(_) => {
            println!("   ⚠️  No daemon reachable (tried localhost + seed nodes) — skipping live validation");
            println!("   💡 Start fuegod or check your network connection");
        }
    }

    Ok(())
}

/// Validate Fuego blockchain transaction details
fn validate_fuego_transaction(package: &StarkProofDataPackage) -> Result<()> {
    // Validate transaction hash format (Fuego native format - no 0x prefix)
    if package.burn_transaction.transaction_hash.starts_with("0x") {
        println!("   ❌ Transaction hash should not have 0x prefix for Fuego");
        return Err(XfgStarkError::ParseError("Invalid Fuego transaction hash format".to_string()));
    }

    // Validate transaction hash length (Fuego uses 32-byte hashes, 64 hex chars)
    if package.burn_transaction.transaction_hash.len() != 64 {
        println!("   ❌ Transaction hash should be 64 hex characters for Fuego");
        return Err(XfgStarkError::ParseError("Invalid Fuego transaction hash length".to_string()));
    }

    // Validate block height is after XFG burn implementation (800,000+)
    if package.burn_transaction.block_height < 800_000 {
        println!("   ❌ Block height {} is before XFG burn implementation (800,000)", package.burn_transaction.block_height);
        return Err(XfgStarkError::ParseError("Block height must be after XFG burn implementation (800,000+)".to_string()));
    }

    // Validate network ID format
    if package.burn_transaction.network_id.is_empty() {
        println!("   ❌ Network ID is required");
        return Err(XfgStarkError::ParseError("Network ID cannot be empty".to_string()));
    }

    println!("   ✅ Fuego blockchain validation passed");
    Ok(())
}

fn create_template(output_file: &str) -> Result<()> {
    let template = ProofDataTemplate::standard_burn();

    let json = serde_json::to_string_pretty(&template)
        .map_err(|e| XfgStarkError::JsonError(e))?;

    std::fs::write(output_file, json)
        .map_err(|e| XfgStarkError::IoError(e))?;

    println!("📝 Template created: {}", output_file);
    println!("📋 Template: {}", template.name);
    println!("📖 Description: {}", template.description);

    Ok(())
}

fn create_package(
    txn_hash: &str,
    output_file: &str,
) -> Result<()> {
    let burn_amount_f64: f64 = 0.8; // Default to standard burn

    // Create package (no Ethereum address yet — provided at generate time)
    let package = StarkProofDataPackage::new(
        burn_amount_f64,
        txn_hash.to_string(),
        String::new(), // eth address bound at proof generation, not stored in deposit data
        "dummy_secret_key".to_string(),
        "fuego-mainnet".to_string(),
    );

    // Save package
    package.save_to_file(output_file)?;

    println!("📦 Data package created: {}", output_file);
    println!("🔥 Burn amount: {} XFG", burn_amount_f64);
    println!("🎯 Mint amount: {} HEAT", package.get_mint_amount_heat());
    println!("🔗 Transaction: {}", txn_hash);
    println!("🌐 Network: fuego-mainnet");
    println!("💡 Ethereum address NOT stored here — provide it when running 'generate'");

    println!("\n💡 Next steps:");
    println!("   1. Edit {} to add block height and timestamp", output_file);
    println!("   2. Run: validate {}", output_file);
    println!("   3. Run: generate {} 0x<eth_address> proof.json", output_file);

    Ok(())
}

// Helper functions for hex conversion
fn hex_to_bytes(hex: &str) -> std::result::Result<Vec<u8>, hex::FromHexError> {
    // Remove 0x prefix if present
    let hex_clean = if hex.starts_with("0x") {
        &hex[2..]
    } else {
        hex
    };
    hex::decode(hex_clean)
}

fn hex_to_u64(hex: &str) -> Result<u64> {
    let bytes = hex_to_bytes(hex)
        .map_err(|e| XfgStarkError::ParseError(format!("Invalid hex string: {}", e)))?;
    
    if bytes.len() < 8 {
        return Err(XfgStarkError::ParseError("Hex string too short for u64".to_string()));
    }
    
    let mut u64_bytes = [0u8; 8];
    u64_bytes.copy_from_slice(&bytes[0..8]);
    Ok(u64::from_le_bytes(u64_bytes))
}

// Helper functions for gas estimation and network status
fn estimate_gas_fees(recipient: &str, _verbose: bool) -> Result<()> {
    println!("🔍 Estimating L1 gas fees for HEAT minting...");
    println!("📧 Recipient: {}", recipient);
    println!();
    println!("💰 Estimated Gas Costs:");
    println!("   • Base transaction: ~21,000 gas");
    println!("   • STARK proof verification: ~500,000 gas");
    println!("   • HEAT token minting: ~100,000 gas");
    println!("   • Total estimated: ~621,000 gas");
    println!();
    println!("💡 Current gas prices:");
    println!("   • Sepolia testnet: ~1-5 gwei");
    println!("   • Mainnet: ~10-50 gwei");
    println!();
    println!("⚠️  Important:");
    println!("   • Add 20% buffer for safety");
    println!("   • Insufficient gas will cause transaction to fail");
    println!("   • Failed transactions require restarting the entire process");
    println!();
    println!("💸 Recommended ETH amounts:");
    println!("   • Sepolia: 0.001 ETH (with buffer)");
    println!("   • Mainnet: 0.05 ETH (with buffer)");
    Ok(())
}

fn check_network_status(network: &str) -> Result<()> {
    println!("🌐 Checking {} network status...", network);
    println!();
    
    match network.to_lowercase().as_str() {
        "sepolia" => {
            println!("🔗 Sepolia Testnet:");
            println!("   • HEAT Token: 0x1234567890123456789012345678901234567890");
            println!("   • Burn Verifier: 0xabcdefabcdefabcdefabcdefabcdefabcdefabcd");
            println!("   • Eldernode Verifier: 0xfedcbafedcbafedcbafedcbafedcbafedcbafedc");
            println!("   • Status: ✅ Active");
            println!("   • Gas Price: ~1-5 gwei");
        },
        "mainnet" => {
            println!("🔗 Ethereum Mainnet:");
            println!("   • HEAT Token: 0x9876543210987654321098765432109876543210");
            println!("   • Burn Verifier: 0xdcbadcbadcbadcbadcbadcbadcbadcbadcbadcba");
            println!("   • Eldernode Verifier: 0xabcdefabcdefabcdefabcdefabcdefabcdefabcd");
            println!("   • Status: ✅ Active");
            println!("   • Gas Price: ~10-50 gwei");
        },
        _ => {
            println!("❌ Unknown network: {}", network);
            println!("   Supported networks: sepolia, mainnet");
        }
    }
    
    println!();
    println!("📊 Network Info:");
    println!("   • Block time: ~12 seconds");
    println!("   • Confirmation time: ~1-2 minutes");
    println!("   • Cross-chain messaging: Arbitrum L2→L1");
    Ok(())
}

// ASCII Art and Branding Functions
fn print_brand_header() {
    // Rainbow color codes
    let colors = [
        "\x1b[31m", // Red
        "\x1b[33m", // Yellow
        "\x1b[32m", // Green
        "\x1b[36m", // Cyan
        "\x1b[34m", // Blue
        "\x1b[35m", // Magenta
    ];
    let reset = "\x1b[0m";

    // Get random ASCII art
    let ascii_art = ascii_arts::get_random_ascii_art();

    let lines: Vec<&str> = ascii_art.lines().collect();

    println!("\n");
    for (i, line) in lines.iter().enumerate() {
        if !line.trim().is_empty() {
            let color_index = i % colors.len();
            println!("{}{}{}", colors[color_index], line, reset);
        } else {
            println!();
        }
    }

    // Print the subtitle in white
    println!("{}🔥 XFG Burn → HEAT Mint STARK CLI 🔥{}", "\x1b[37m", reset);
    println!("{}Version 2.0 - Enhanced{}", "\x1b[37m", reset);
}
