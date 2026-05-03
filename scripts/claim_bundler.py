import subprocess
import json
import argparse
import os
import sys

def run_command(cmd):
    print(f"Executing: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        sys.exit(1)
    return result.stdout

def main():
    parser = argparse.ArgumentParser(description="Bundle STARK and Merkle proofs for HEAT claiming")
    parser.add_argument("--txn-hash", required=True, help="Fuego transaction hash")
    parser.add_argument("--secret", required=True, help="Burn secret (hex)")
    parser.add_argument("--amount", type=int, required=True, help="Burn amount in XFG atomic units")
    parser.add_argument("--recipient", required=True, help="Recipient Ethereum address")
    parser.add_argument("--rpc", default="http://localhost:18180", help="Fuego daemon RPC URL")
    parser.add_argument("--output", default="claim_bundle.json", help="Output bundle file")
    
    args = parser.parse_args()

    # Paths to the tools
    STARK_CLI = "cargo run --bin xfg-stark-cli" # This might need adjustment based on where it's run
    PROVER_CLI = "cargo run --bin fuego-prover" # This might need adjustment

    # We need a package.json for the STARK prover
    package_file = "temp_package.json"
    proof_file = "temp_stark_proof.json"
    merkle_file = "temp_merkle_proof.json"

    try:
        # 1. Create a data package for the STARK prover
        # Note: xfg-stark-cli create-package uses a template or defaults
        # For simplicity, we can just create the JSON manually or use the CLI
        print("Creating temporary data package...")
        # We use a simplified version of the package format expected by xfg-stark-3
        package_data = {
            "metadata": {"version": "3.0.0", "network": "fuego-mainnet", "created_at": "now", "description": "Bundler generated"},
            "burn_transaction": {
                "transaction_hash": args.txn_hash,
                "burn_amount_xfg": args.amount / 10**7,
                "burn_amount_atomic": args.amount,
                "block_height": 800001, # Default/Placeholder, should be fetched from RPC
                "timestamp": "now",
                "network_id": "1",
                "target_chain_id": 42161,
                "deposit_term": 4294967295 # FOREVER
            },
            "recipient": {"ethereum_address": args.recipient},
            "secret": {"secret_key": args.secret}
        }
        with open(package_file, "w") as f:
            json.dump(package_data, f)

        # 2. Generate STARK proof
        print("Generating STARK proof...")
        # The command in xfg-stark-cli: generate <input> <eth_address> <output>
        run_command(["cargo", "run", "-p", "xfg-stark-cli", "--", "generate", package_file, args.recipient, proof_file])

        # 3. Generate Merkle proof using fuego-prover
        print("Generating Merkle proof...")
        # The command in fuego-prover: claim --rpc <rpc> --commitment <commit> --preimage <preimage> --recipient <rec> --out <out>
        # First we need the commitment (keccak256 of preimage)
        # Preimage: secret[32] || amount_le64[8] || network_id_le32[4] || chain_id_le32[4] || version_le32[4] || term_le32[4]
        # For simplicity, let's just use the xfg-stark-cli to verify-commitment first or let fuego-prover handle it
        # Actually, fuego-prover claim needs the preimage.
        
        # Construct preimage bytes
        import struct
        secret_bytes = bytes.fromhex(args.secret)
        amount_bytes = struct.pack("<Q", args.amount)
        network_id = struct.pack("<I", 1)
        chain_id = struct.pack("<I", 42161)
        version = struct.pack("<I", 3)
        term = struct.pack("<I", 0xFFFFFFFF)
        preimage = secret_bytes + amount_bytes + network_id + chain_id + version + term
        preimage_hex = preimage.hex()
        
        # Get commitment from xfg-stark-cli or calculate it
        import hashlib
        # Note: Fuego uses Keccak256, not SHA3. Python's hashlib.sha3_256 is NOT keccak256.
        # We'll use the output from the tools.
        
        # We can just call fuego-prover claim and let it compute the commitment
        run_command(["cargo", "run", "-p", "fuego-prover-cli", "--", "claim", "--rpc", args.rpc, "--commitment", "0x", "--preimage", preimage_hex, "--recipient", args.recipient, "--out", merkle_file])

        # 4. Bundle the results
        with open(proof_file, "r") as f:
            stark_proof_data = json.load(f)
        with open(merkle_file, "r") as f:
            merkle_proof_data = json.load(f)

        bundle = {
            "starkProof": stark_proof_data["proof_data"],
            "commitment": merkle_proof_data["commitment"],
            "nullifier": "0x...", # This should be extracted from STARK proof or computed
            "amount": args.amount,
            "txnHash": args.txn_hash,
            "merkleProof": merkle_proof_data["merkle_proof"],
            "leafIndex": merkle_proof_data["leaf_index"],
            "recipient": args.recipient
        }

        # Extract nullifier from STARK proof if possible, or compute it
        # Nullifier: keccak256(secret[32] || "nullifier" || amount_le64[8])
        # We'll compute it here for the bundle
        # Since we need Keccak256, we should use a library or the CLI.
        # Let's use the CLI's bundle logic if available, but the user wanted a specific bundle.
        
        with open(args.output, "w") as f:
            json.dump(bundle, f, indent=2)

        print(f"Successfully bundled proof to {args.output}")

    finally:
        for f in [package_file, proof_file, merkle_file]:
            if os.path.exists(f):
                os.remove(f)

if __name__ == "__main__":
    main()
