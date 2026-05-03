// COLD Deposits Validation Utilities

import { ethers } from 'ethers';
import {
  DomainBindingMessage,
  ClaimRequest,
  FuegoDepositData,
  TIER_CONFIGS,
  LEGACY_CUTOFF_TIMESTAMP,
  VALID_NETWORK_IDS
} from '../types/cold';

// EIP-712 domain for signature verification
const EIP712_DOMAIN = {
  name: "COLD Deposits",
  version: "1",
  chainId: 421614 // Arbitrum Sepolia
};

const EIP712_TYPES = {
  DomainBinding: [
    { name: "domain", type: "string" },
    { name: "recipient", type: "address" },
    { name: "depositTxHash", type: "bytes32" },
    { name: "timestamp", type: "uint256" },
    { name: "nonce", type: "bytes32" }
  ]
};

/**
 * Verify domain binding signature (EIP-712)
 */
export function verifyDomainBindingSignature(
  domainBinding: DomainBindingMessage,
  signature: string
): string {
  try {
    const recoveredAddress = ethers.verifyTypedData(
      EIP712_DOMAIN,
      EIP712_TYPES,
      domainBinding,
      signature
    );

    return recoveredAddress;
  } catch (error) {
    throw new Error(`Signature verification failed: ${error.message}`);
  }
}

/**
 * Validate claim request (domain binding + signature)
 */
export async function validateClaimRequest(
  request: ClaimRequest,
  usedNonces: Set<string>
): Promise<void> {
  const { domainBinding, signature } = request;

  // 1. Verify signature and recover address
  const recoveredAddress = verifyDomainBindingSignature(domainBinding, signature);

  if (recoveredAddress.toLowerCase() !== domainBinding.recipient.toLowerCase()) {
    throw new Error("Signature verification failed: recovered address does not match recipient");
  }

  // 2. Validate domain
  if (domainBinding.domain !== "usexfg.org") {
    throw new Error(`Invalid domain: ${domainBinding.domain}`);
  }

  // 3. Validate timestamp freshness (±5 minutes)
  const now = Date.now();
  const diff = Math.abs(now - domainBinding.timestamp);
  const maxDiff = 5 * 60 * 1000; // 5 minutes in milliseconds

  if (diff > maxDiff) {
    throw new Error(`Request timestamp expired (diff: ${Math.floor(diff / 1000)}s, max: ${maxDiff / 1000}s)`);
  }

  // 4. Validate nonce not reused
  if (usedNonces.has(domainBinding.nonce)) {
    throw new Error("Nonce already used");
  }

  // 5. Validate recipient is valid Ethereum address
  if (!ethers.isAddress(domainBinding.recipient)) {
    throw new Error("Invalid recipient address");
  }

  // 6. Validate deposit tx hash format (bytes32)
  if (!isValidBytes32(domainBinding.depositTxHash)) {
    throw new Error("Invalid deposit transaction hash format");
  }

  // 7. Validate nonce format (bytes32)
  if (!isValidBytes32(domainBinding.nonce)) {
    throw new Error("Invalid nonce format");
  }
}

/**
 * Validate deposit data from Fuego daemon
 */
export async function validateDepositData(
  depositData: FuegoDepositData,
  recipient: string
): Promise<void> {
  // 1. Deposit exists
  if (!depositData) {
    throw new Error("Deposit not found on Fuego blockchain");
  }

  // 2. Deposit is still locked
  if (depositData.status !== "locked") {
    throw new Error("Deposit is not in locked status");
  }

  // 3. Lock period not expired
  const lockSeconds = depositData.lockPeriodMonths === 3
    ? 90 * 24 * 60 * 60   // 3 months
    : 365 * 24 * 60 * 60; // 12 months

  const unlockTime = depositData.timestamp + lockSeconds;
  const nowSeconds = Math.floor(Date.now() / 1000);

  if (nowSeconds > unlockTime) {
    throw new Error("Deposit lock period has expired");
  }

  // 4. Amount and period match valid tier
  const tier = calculateTier(depositData.amount, depositData.lockPeriodMonths);
  if (tier === null) {
    throw new Error(
      `Invalid deposit amount/period combination: ${depositData.amount} XFG (atomic), ${depositData.lockPeriodMonths} months`
    );
  }

  // 5. Network ID is valid
  if (!VALID_NETWORK_IDS.includes(depositData.networkId)) {
    throw new Error(`Invalid Fuego network ID: ${depositData.networkId}`);
  }

  // 6. Nullifier format is valid (bytes32)
  if (!isValidBytes32(depositData.nullifier)) {
    throw new Error("Invalid nullifier format");
  }

  // 7. Commitment format is valid (bytes32)
  if (!isValidBytes32(depositData.commitment)) {
    throw new Error("Invalid commitment format");
  }
}

/**
 * Calculate tier from XFG amount and lock period
 * Returns null if invalid combination
 *
 * 8 tiers total: 4 amounts × 2 terms
 * - Amount Tiers: 0.8 XFG, 8 XFG, 80 XFG, 800 XFG
 * - Term Tiers: 3 months, 12 months
 * - Encoding: (amountIndex * 2) + termIndex
 */
export function calculateTier(
  xfgAmountAtomic: number,
  lockMonths: number
): number | null {
  // Convert atomic units (7 decimals) to readable XFG
  const xfgAmount = xfgAmountAtomic / 10_000_000;

  // Determine amount index (0 = 0.8, 1 = 8, 2 = 80, 3 = 800)
  let amountIndex: number;
  const tolerance = 0.0001;

  if (Math.abs(xfgAmount - 0.8) < tolerance) {
    amountIndex = 0; // 0.8 XFG
  } else if (Math.abs(xfgAmount - 8) < tolerance) {
    amountIndex = 1; // 8 XFG
  } else if (Math.abs(xfgAmount - 80) < tolerance) {
    amountIndex = 2; // 80 XFG
  } else if (Math.abs(xfgAmount - 800) < tolerance) {
    amountIndex = 3; // 800 XFG
  } else {
    return null; // Invalid amount
  }

  // Determine term index (0 = 3 months, 1 = 12 months)
  let termIndex: number;
  if (lockMonths === 3) {
    termIndex = 0; // 3 months
  } else if (lockMonths === 12) {
    termIndex = 1; // 12 months
  } else {
    return null; // Invalid lock period
  }

  // Calculate tier: (amountIndex * 2) + termIndex
  // Tier 0: 0.8 XFG × 3mo
  // Tier 1: 0.8 XFG × 12mo
  // Tier 2: 8 XFG × 3mo
  // Tier 3: 8 XFG × 12mo
  // Tier 4: 80 XFG × 3mo
  // Tier 5: 80 XFG × 12mo
  // Tier 6: 800 XFG × 3mo
  // Tier 7: 800 XFG × 12mo
  const tier = (amountIndex * 2) + termIndex;

  return tier;
}

/**
 * Check if deposit qualifies for legacy rate (80% APY)
 * Only 800 XFG deposits (tier 6 & 7) before 2026-01-01
 */
export function isLegacyDeposit(depositTimestamp: number, tier: number): boolean {
  // Only tier 6 and tier 7 (800 XFG) had legacy option
  if (tier !== 6 && tier !== 7) {
    return false;
  }

  // Check if deposit was before 2026-01-01
  return depositTimestamp < LEGACY_CUTOFF_TIMESTAMP;
}

/**
 * Get CD interest amount for tier (standard or legacy)
 * CD has 12 decimals: 1 CD = 10^12 atomic units
 * Formula: (XFG_amount / 100,000) × APY × 10^12
 */
export function getCDAmount(tier: number, isLegacy: boolean): string {
  // Legacy amounts (only tier 6 & 7 @ 80% APY)
  if (isLegacy && tier === 6) {
    return "6400000000"; // (800 / 100,000) × 0.80 × 10^12
  }
  if (isLegacy && tier === 7) {
    return "6400000000"; // (800 / 100,000) × 0.80 × 10^12
  }

  // Standard amounts (v3 canonical — COLD_V3_FINAL_UPDATE.md)
  const amounts: Record<number, string> = {
    0: "640000",        // 0.8 XFG × 3mo  @ 8% APY
    1: "2160000",       // 0.8 XFG × 12mo @ 27% APY
    2: "14400000",      // 8 XFG × 3mo    @ 18% APY
    3: "26400000",      // 8 XFG × 12mo   @ 33% APY
    4: "216000000",     // 80 XFG × 3mo   @ 27% APY
    5: "336000000",     // 80 XFG × 12mo  @ 42% APY
    6: "2640000000",    // 800 XFG × 3mo  @ 33% APY
    7: "5520000000"     // 800 XFG × 12mo @ 69% APY
  };

  const amount = amounts[tier];
  if (!amount) {
    throw new Error(`Invalid tier: ${tier}`);
  }

  return amount;
}

/**
 * Validate bytes32 format (0x + 64 hex chars)
 */
export function isValidBytes32(value: string): boolean {
  return /^0x[0-9a-fA-F]{64}$/.test(value);
}

/**
 * Validate Ethereum address format
 */
export function isValidAddress(address: string): boolean {
  return ethers.isAddress(address);
}
