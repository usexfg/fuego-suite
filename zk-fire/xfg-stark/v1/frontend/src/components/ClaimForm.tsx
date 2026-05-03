import React, { useState } from 'react';
import { ethers } from 'ethers';

interface ClaimFormProps {
  walletAddress: string;
  onSubmit: (claimKey: string, signature: string, timestamp: number) => Promise<void>;
  isLoading: boolean;
}

const EIP712_DOMAIN = {
  name: "COLD Deposits",
  version: "1",
  chainId: 421614
};

const EIP712_TYPES = {
  DomainBinding: [
    { name: "domain", type: "string" },
    { name: "claimKey", type: "bytes32" },
    { name: "walletAddress", type: "address" },
    { name: "timestamp", type: "uint256" }
  ]
};

export function ClaimForm({ walletAddress, onSubmit, isLoading }: ClaimFormProps) {
  const [claimKey, setClaimKey] = useState('');
  const [isSigningError, setIsSigningError] = useState<string | null>(null);

  const handleSignAndSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setIsSigningError(null);

    // Validate claim key format (0x + 64 hex chars)
    if (!/^0x[0-9a-fA-F]{64}$/.test(claimKey)) {
      setIsSigningError('Invalid claim key format. Expected 0x + 64 hex characters');
      return;
    }

    try {
      if (!window.ethereum) {
        throw new Error('Web3 wallet required');
      }

      // Create domain message
      const timestamp = Math.floor(Date.now() / 1000);
      const message = {
        domain: "usexfg.org",
        claimKey: claimKey,
        walletAddress: walletAddress,
        timestamp: timestamp
      };

      // Request signature from wallet
      const provider = new ethers.BrowserProvider(window.ethereum);
      const signer = await provider.getSigner();

      const signature = await signer._signTypedData(
        EIP712_DOMAIN,
        EIP712_TYPES,
        message
      );

      // Submit to API — include the timestamp we signed over
      await onSubmit(claimKey, signature, timestamp);

    } catch (error: any) {
      if (error.code === 'ACTION_REJECTED' || error.code === -32603) {
        setIsSigningError('Signature request cancelled');
      } else if (error.message?.includes('is not currently supported on chainId')) {
        setIsSigningError('Please switch to Arbitrum Sepolia testnet in MetaMask');
      } else {
        setIsSigningError(error.message || 'Failed to sign message');
      }
    }
  };

  return (
    <form onSubmit={handleSignAndSubmit} className="space-y-6">
      <div>
        <label htmlFor="claimKey" className="block text-sm font-medium text-white mb-2">
          Claim Key (Nullifier)
        </label>
        <input
          id="claimKey"
          type="text"
          placeholder="0x..."
          value={claimKey}
          onChange={(e) => setClaimKey(e.target.value)}
          disabled={isLoading}
          className="w-full px-4 py-3 bg-slate-600 border border-slate-500 rounded-lg text-white placeholder-slate-400 focus:outline-none focus:border-blue-500 disabled:opacity-50"
        />
        <p className="text-slate-400 text-xs mt-2">
          Paste the claim key (nullifier) derived from your COLD deposit.
          <br />
          Format: 0x followed by 64 hexadecimal characters (256-bit hash)
        </p>
      </div>

      {isSigningError && (
        <div className="bg-red-900/30 border border-red-700/50 rounded-lg p-4 text-red-300 text-sm">
          {isSigningError}
        </div>
      )}

      <div className="bg-slate-600/50 border border-slate-500/50 rounded-lg p-4 space-y-3">
        <h4 className="text-sm font-semibold text-white">What happens next:</h4>
        <ol className="text-sm text-slate-300 space-y-2 list-decimal list-inside">
          <li>Sign with MetaMask (EIP-712 domain-bound signature)</li>
          <li>API verifies commitment exists on Fuego via RPC</li>
          <li>API returns domain signature from usexfg.org</li>
          <li>Submit domain signature to L2 contract to mint tokens</li>
        </ol>
      </div>

      <button
        type="submit"
        disabled={isLoading || !claimKey}
        className="w-full px-4 py-3 bg-green-600 hover:bg-green-700 disabled:bg-green-900 text-white font-semibold rounded-lg transition"
      >
        {isLoading ? 'Processing...' : 'Sign & Validate Claim'}
      </button>

      <div className="bg-blue-900/20 border border-blue-700/50 rounded-lg p-4 text-blue-200 text-xs space-y-2">
        <p className="font-semibold">Privacy Notice:</p>
        <p>
          • Your claim key is validated against the Fuego blockchain via RPC
        </p>
        <p>
          • The API never receives your original commitment or transaction hash
        </p>
        <p>
          • Only the domain signature is returned (no persistent logging)
        </p>
      </div>
    </form>
  );
}
