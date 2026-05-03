// COLD Deposits API Types
// Domain linking and proof submission types

export interface DomainBindingMessage {
  domain: string;           // "usexfg.org"
  recipient: string;        // Ethereum address
  depositTxHash: string;    // Fuego deposit transaction hash
  timestamp: number;        // Browser timestamp (milliseconds)
  nonce: string;            // Random nonce (bytes32)
}

export interface ClaimRequest {
  domainBinding: DomainBindingMessage;
  signature: string;        // EIP-712 signature
}

export interface FuegoDepositData {
  txHash: string;
  sender: string;
  amount: number;           // XFG amount (atomic units: 7 decimals)
  lockPeriodMonths: number; // 3 or 12
  timestamp: number;        // Unix timestamp (seconds)
  nullifier: string;        // Derived from deposit (bytes32)
  commitment: string;       // Hash of deposit data (bytes32)
  status: "locked" | "unlocked";
  networkId: string;        // Fuego mainnet or testnet ID
}

export interface STARKProofData {
  depositTxHash: string;
  recipient: string;
  tier: number;
  depositTimestamp: number;
  nullifier: string;
  commitment: string;
  networkId: string;
  proof: string;            // Serialized STARK proof
}

export interface ClaimResponse {
  success: boolean;
  txHash?: string;
  tier?: number;
  isLegacy?: boolean;
  estimatedCDAmount?: string;
  arbiscanUrl?: string;
  message?: string;
  error?: string;
}

export interface TierConfig {
  tier: number;
  xfgAmount: number;        // In readable XFG (0.8 or 800)
  lockMonths: number;       // 3 or 12
  apyBps: number;           // Basis points (800 = 8%)
  cdInterest: string;       // Atomic units (string for precision)
}

// 8-tier structure: 4 amounts × 2 lock terms
// Encoding: tier = (amountIndex * 2) + termIndex
// amountIndex: 0=0.8 XFG, 1=8 XFG, 2=80 XFG, 3=800 XFG
// termIndex: 0=3mo, 1=12mo
export const TIER_CONFIGS: TierConfig[] = [
  {
    tier: 0,
    xfgAmount: 0.8,
    lockMonths: 3,
    apyBps: 800,           // 8%
    cdInterest: "640000"   // 640,000 atomic units
  },
  {
    tier: 1,
    xfgAmount: 0.8,
    lockMonths: 12,
    apyBps: 2700,          // 27%
    cdInterest: "2160000"  // 2,160,000 atomic units
  },
  {
    tier: 2,
    xfgAmount: 8,
    lockMonths: 3,
    apyBps: 1800,           // 18%
    cdInterest: "14400000"  // 14,400,000 atomic units
  },
  {
    tier: 3,
    xfgAmount: 8,
    lockMonths: 12,
    apyBps: 3300,           // 33%
    cdInterest: "26400000"  // 26,400,000 atomic units
  },
  {
    tier: 4,
    xfgAmount: 80,
    lockMonths: 3,
    apyBps: 2700,            // 27%
    cdInterest: "216000000"  // 216,000,000 atomic units
  },
  {
    tier: 5,
    xfgAmount: 80,
    lockMonths: 12,
    apyBps: 4200,            // 42%
    cdInterest: "336000000"  // 336,000,000 atomic units
  },
  {
    tier: 6,
    xfgAmount: 800,
    lockMonths: 3,
    apyBps: 3300,             // 33%
    cdInterest: "2640000000"  // 2,640,000,000 atomic units
  },
  {
    tier: 7,
    xfgAmount: 800,
    lockMonths: 12,
    apyBps: 6900,             // 69%
    cdInterest: "5520000000"  // 5,520,000,000 atomic units
  }
];

// Legacy tier configs (pre-2026, 800 XFG only — tiers 6 & 7 get 80% APY)
export const LEGACY_TIER_CONFIGS: TierConfig[] = [
  {
    tier: 6,
    xfgAmount: 800,
    lockMonths: 3,
    apyBps: 8000,             // 80%
    cdInterest: "6400000000"  // 6,400,000,000 atomic units
  },
  {
    tier: 7,
    xfgAmount: 800,
    lockMonths: 12,
    apyBps: 8000,             // 80%
    cdInterest: "6400000000"  // 6,400,000,000 atomic units
  }
];

export const LEGACY_CUTOFF_TIMESTAMP = 1735689600; // 2026-01-01 00:00:00 UTC

export const FUEGO_MAINNET_NETWORK_ID = "93385046440755750514194170694064996624";
export const FUEGO_TESTNET_NETWORK_ID = "112015110234323138517908755257434054688";

export const VALID_NETWORK_IDS = [
  FUEGO_MAINNET_NETWORK_ID,
  FUEGO_TESTNET_NETWORK_ID
];
