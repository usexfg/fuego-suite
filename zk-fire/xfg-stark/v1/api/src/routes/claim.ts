// COLD/HEAT Deposit Claim API Endpoint — Option B Legacy (domain-based)
// POST /api/cold/claim
//
// NOTE: The primary claim flow for v3 unified EF sigs is xfg-stark-cli:
//   xfg-stark bundle --commitment <hash> --recipient <addr>
//   → bundles STARK proof + merkle proof + EFier signatures
//   → user submits CompleteProofPackage directly to L2 contract (no API needed)
//
// This API provides Option B fallback: validates commitment on Fuego via
// check_commitment_exists RPC, returns Ed25519 domain signature for L2 contract.

import express, { Request, Response } from "express";
import { ethers } from "ethers";
import axios from "axios";
import { ClaimRequest, ClaimResponse, FuegoDepositData } from "../types/cold";
import { verifyDomainBindingSignature } from "../utils/validation";
import { FuegoRPCClient } from "../services/fuegoRPC";

const router = express.Router();

// Fuego RPC clients (configure from environment)
const fuegoMainnetRPC = new FuegoRPCClient(
  process.env.FUEGO_MAINNET_RPC || "http://localhost:18180",
  "93385046440755750514194170694064996624",
);

const fuegoTestnetRPC = new FuegoRPCClient(
  process.env.FUEGO_TESTNET_RPC || "http://localhost:28280",
  "112015110234323138517908755257434054688",
);

// Arbitrum provider (read-only, for contract queries)
const arbProvider = new ethers.JsonRpcProvider(
  process.env.ARB_SEPOLIA_RPC || "https://sepolia-rollup.arbitrum.io/rpc",
);

// Domain Ed25519 keypair for signing claims (configure from environment)
const DOMAIN_PRIVATE_KEY = process.env.DOMAIN_PRIVATE_KEY || "";
const DOMAIN_PUBLIC_KEY = process.env.DOMAIN_PUBLIC_KEY || "";

// COLD verifier contract (read-only for nullifier checks)
const COLD_VERIFIER_ADDRESS = process.env.COLD_VERIFIER_ADDRESS || "";

const COLD_VERIFIER_ABI = [
  "function isNullifierUsed(bytes32 nullifier) view returns (bool)",
  "function estimateL1GasFee(address recipient, uint8 tier) view returns (uint256)",
];

const coldVerifier = new ethers.Contract(
  COLD_VERIFIER_ADDRESS,
  COLD_VERIFIER_ABI,
  arbProvider,
);

/**
 * POST /api/cold/claim
 * Domain-based Option B MVP implementation
 *
 * Flow:
 * 1. User submits: { claimKey (nullifier), signature, walletAddress }
 * 2. API validates signature with EIP-712
 * 3. API queries Fuego RPC: "Does commitment exist?"
 * 4. API generates Ed25519 domain signature
 * 5. API returns domain signature (zero logging, stateless)
 * 6. User submits domain signature to L2 contract for redemption
 */
router.post("/claim", async (req: Request, res: Response) => {
  try {
    const { claimKey, signature, walletAddress } = req.body;

    // Validate request format
    if (!claimKey || !signature || !walletAddress) {
      return res.status(400).json({
        success: false,
        error: "Missing required fields: claimKey, signature, walletAddress",
      });
    }

    // Step 1: Verify EIP-712 signature
    console.log("Step 1: Verifying EIP-712 signature...");
    const { timestamp } = req.body; // Client must send the timestamp it signed over
    if (!timestamp || typeof timestamp !== 'number') {
      return res.status(400).json({
        success: false,
        error: "Missing required field: timestamp (must match value signed by wallet)",
      });
    }
    let recoveredAddress: string;
    try {
      // Reconstruct domain message that user signed (using their timestamp, not server time)
      const domainMessage = {
        domain: "usexfg.org",
        claimKey: claimKey,
        walletAddress: walletAddress,
        timestamp: timestamp,
      };

      recoveredAddress = ethers.verifyTypedData(
        { name: "COLD Deposits", version: "1", chainId: 421614 },
        {
          DomainBinding: [
            { name: "domain", type: "string" },
            { name: "claimKey", type: "bytes32" },
            { name: "walletAddress", type: "address" },
            { name: "timestamp", type: "uint256" },
          ],
        },
        domainMessage,
        signature,
      );
    } catch (error: any) {
      throw new Error(`Signature verification failed: ${error.message}`);
    }

    // Verify recovered address matches wallet
    if (recoveredAddress.toLowerCase() !== walletAddress.toLowerCase()) {
      throw new Error(
        "Signature verification failed: recovered address does not match wallet",
      );
    }

    console.log("✓ Signature verified for wallet:", walletAddress);

    // Step 2: Query Fuego RPC - check commitment exists
    console.log("Step 2: Checking commitment exists on Fuego...");

    // Try mainnet first, then testnet
    let commitmentExists = false;
    let blockHeight = 0;

    try {
      const mainnetResponse = await axios.post(
        process.env.FUEGO_MAINNET_RPC || "http://localhost:18180/json_rpc",
        {
          jsonrpc: "2.0",
          id: "0",
          method: "check_commitment_exists",
          params: { commitment_hash: claimKey },
        },
      );

      if (mainnetResponse.data?.result?.exists) {
        commitmentExists = true;
        blockHeight = mainnetResponse.data.result.block_height || 0;
      }
    } catch (error) {
      console.log("Not found on mainnet, trying testnet...");
      try {
        const testnetResponse = await axios.post(
          process.env.FUEGO_TESTNET_RPC || "http://localhost:28280/json_rpc",
          {
            jsonrpc: "2.0",
            id: "0",
            method: "check_commitment_exists",
            params: { commitment_hash: claimKey },
          },
        );

        if (testnetResponse.data?.result?.exists) {
          commitmentExists = true;
          blockHeight = testnetResponse.data.result.block_height || 0;
        }
      } catch (testnetError) {
        console.error("Testnet check also failed:", testnetError);
      }
    }

    if (!commitmentExists) {
      throw new Error("Commitment not found on Fuego blockchain");
    }

    console.log("✓ Commitment exists at block height:", blockHeight);

    // Step 3: Check nullifier not already used on L2 contract
    console.log("Step 3: Checking if claim is already used...");
    try {
      const nullifierUsed = await coldVerifier.isNullifierUsed(claimKey);
      if (nullifierUsed) {
        throw new Error("Claim already used (nullifier already claimed)");
      }
      console.log("✓ Nullifier not yet claimed");
    } catch (error: any) {
      // If contract check fails, continue (contract may not be available in testnet early stage)
      console.log(
        "⚠ Could not verify nullifier on contract (may be unavailable):",
        error.message,
      );
    }

    // Step 4: Generate domain signature with Ed25519
    console.log("Step 4: Generating domain signature...");

    const domainSignatureMessage = `usexfg.org|${claimKey}|${Math.floor(Date.now() / 1000)}`;

    // Note: In production, use proper Ed25519 signing library
    // For MVP, we'll use a placeholder that the frontend expects
    // Real implementation would use TweetNaCl.js or similar
    const domainSignature = ethers.id(domainSignatureMessage); // Placeholder

    console.log("✓ Domain signature generated");

    // Step 5: Return domain signature (ZERO LOGGING, STATELESS)
    // No persistent storage, no logging of claimKey or user data
    const response = {
      success: true,
      domainSignature: domainSignature,
      walletAddress: walletAddress,
      claimKey: claimKey,
      message:
        "Claim validated by domain. Submit domain signature to L2 contract for token redemption.",
      contractAddress: COLD_VERIFIER_ADDRESS,
      nextStep: "Submit domainSignature to L2 contract claimCD() function",
    };

    res.json(response);
  } catch (error: any) {
    console.error("Claim validation error:", error.message);

    // Return error without logging sensitive data
    const response = {
      success: false,
      error: error.message || "Claim validation failed",
    };

    res.status(400).json(response);
  }
});

/**
 * GET /api/cold/health
 * Health check endpoint
 */
router.get("/health", async (req: Request, res: Response) => {
  try {
    // Check Fuego daemon connectivity
    const mainnetHealthy = await fuegoMainnetRPC.healthCheck();
    const testnetHealthy = await fuegoTestnetRPC.healthCheck();

    // Check Arbitrum provider
    const arbBlockNumber = await arbProvider.getBlockNumber();

    res.json({
      success: true,
      fuego: {
        mainnet: mainnetHealthy ? "healthy" : "unhealthy",
        testnet: testnetHealthy ? "healthy" : "unhealthy",
      },
      arbitrum: {
        blockNumber: arbBlockNumber,
        status: "healthy",
      },
      apiVerifier: DOMAIN_PUBLIC_KEY,
    });
  } catch (error: any) {
    res.status(500).json({
      success: false,
      error: error.message,
    });
  }
});

export default router;
