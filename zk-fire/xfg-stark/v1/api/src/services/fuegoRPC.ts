// Fuego Daemon RPC Client (Option B legacy — primary claim flow uses xfg-stark-cli)
// Queries a trusted Fuego node for deposit/commitment data.
// All RPC calls use the /json_rpc endpoint.
//
// Primary xfg-stark-cli RPC endpoints (used by the relay CLI):
//   /get_height
//   /get_commitment          — full entry: type, amount, block, tier, tx hash, target-chain
//   /get_commitment_stats    — merkle root, consensus %, signed/pending EFier IDs
//   /get_commitment_merkle_proof — merkle path for bundle
//   /check_commitment_exists — boolean existence check

import axios, { AxiosInstance } from 'axios';
import { FuegoDepositData } from '../types/cold';

export class FuegoRPCClient {
  private client: AxiosInstance;
  private networkId: string;

  constructor(rpcUrl: string, networkId: string) {
    this.client = axios.create({
      baseURL: rpcUrl,
      timeout: 30000,
      headers: {
        'Content-Type': 'application/json'
      }
    });
    this.networkId = networkId;
  }

  /**
   * Query full commitment entry from Fuego daemon (by commitment hash, not tx hash).
   * Returns null if not found.
   * Note: primary relay uses get_commitment RPC; this wraps it for Option B API use.
   */
  async queryDeposit(commitmentHash: string): Promise<FuegoDepositData | null> {
    try {
      // RPC call to Fuego daemon — /json_rpc endpoint
      const response = await this.client.post('/json_rpc', {
        jsonrpc: '2.0',
        method: 'get_commitment',
        params: {
          commitment_hash: commitmentHash
        },
        id: 1
      });

      if (response.data.error) {
        throw new Error(`Fuego RPC error: ${response.data.error.message}`);
      }

      const result = response.data.result;

      if (!result) {
        return null; // Deposit not found
      }

      // Parse deposit data from Fuego response
      const depositData: FuegoDepositData = {
        txHash: result.tx_hash,
        sender: result.sender,
        amount: parseInt(result.amount),           // XFG atomic units (7 decimals)
        lockPeriodMonths: parseInt(result.lock_period_months),
        timestamp: parseInt(result.timestamp),     // Unix timestamp
        nullifier: result.nullifier,               // bytes32
        commitment: result.commitment,             // bytes32
        status: result.status,                     // "locked" | "unlocked"
        networkId: this.networkId
      };

      return depositData;

    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Fuego RPC connection error: ${error.message}`);
      }
      throw error;
    }
  }

  /**
   * Check if a commitment exists on Fuego chain (check_commitment_exists RPC)
   */
  async checkCommitmentExists(commitmentHash: string): Promise<{ exists: boolean; blockHeight: number }> {
    try {
      const response = await this.client.post('/json_rpc', {
        jsonrpc: '2.0',
        method: 'check_commitment_exists',
        params: {
          commitment_hash: commitmentHash
        },
        id: 1
      });

      if (response.data.error) {
        throw new Error(`Fuego RPC error: ${response.data.error.message}`);
      }

      const result = response.data.result;
      return {
        exists: result?.exists === true,
        blockHeight: parseInt(result?.block_height || '0')
      };

    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Fuego RPC connection error: ${error.message}`);
      }
      throw error;
    }
  }

  /**
   * Get commitment stats: merkle root, EF consensus %, signed EFier IDs
   */
  async getCommitmentStats(): Promise<{ merkleRoot: string; consensusPercent: number; thresholdMet: boolean }> {
    try {
      const response = await this.client.post('/json_rpc', {
        jsonrpc: '2.0',
        method: 'get_commitment_stats',
        params: {},
        id: 1
      });

      if (response.data.error) {
        throw new Error(`Fuego RPC error: ${response.data.error.message}`);
      }

      const result = response.data.result;
      return {
        merkleRoot: result?.merkle_root || '',
        consensusPercent: parseFloat(result?.consensus_percent || '0'),
        thresholdMet: result?.threshold_met === true
      };

    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Fuego RPC connection error: ${error.message}`);
      }
      throw error;
    }
  }

  /**
   * Check if nullifier has been used on Fuego chain (legacy — nullifiers tracked on L2)
   */
  async isNullifierUsed(nullifier: string): Promise<boolean> {
    try {
      const response = await this.client.post('/json_rpc', {
        jsonrpc: '2.0',
        method: 'is_nullifier_used',
        params: {
          nullifier: nullifier
        },
        id: 1
      });

      if (response.data.error) {
        throw new Error(`Fuego RPC error: ${response.data.error.message}`);
      }

      return response.data.result?.used === true;

    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Fuego RPC connection error: ${error.message}`);
      }
      throw error;
    }
  }

  /**
   * Get current Fuego blockchain height
   */
  async getHeight(): Promise<number> {
    try {
      const response = await this.client.post('/json_rpc', {
        jsonrpc: '2.0',
        method: 'get_height',
        params: {},
        id: 1
      });

      if (response.data.error) {
        throw new Error(`Fuego RPC error: ${response.data.error.message}`);
      }

      return parseInt(response.data.result.height);

    } catch (error) {
      if (axios.isAxiosError(error)) {
        throw new Error(`Fuego RPC connection error: ${error.message}`);
      }
      throw error;
    }
  }

  /**
   * Health check - verify daemon is reachable
   */
  async healthCheck(): Promise<boolean> {
    try {
      const height = await this.getHeight();
      return height > 0;
    } catch (error) {
      console.error('Fuego daemon health check failed:', error);
      return false;
    }
  }
}

// Factory functions for mainnet and testnet clients
export function createMainnetClient(rpcUrl: string): FuegoRPCClient {
  return new FuegoRPCClient(
    rpcUrl,
    "93385046440755750514194170694064996624" // Fuego mainnet
  );
}

export function createTestnetClient(rpcUrl: string): FuegoRPCClient {
  return new FuegoRPCClient(
    rpcUrl,
    "112015110234323138517908755257434054688" // Fuego testnet
  );
}
