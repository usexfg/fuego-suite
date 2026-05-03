import React, { useState, useEffect } from 'react';
import { ethers } from 'ethers';

interface WalletConnectionProps {
  connectedWallet: string | null;
  onWalletConnected: (address: string) => void;
  onWalletDisconnected: () => void;
}

export function WalletConnection({
  connectedWallet,
  onWalletConnected,
  onWalletDisconnected
}: WalletConnectionProps) {
  const [isConnecting, setIsConnecting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // Check if wallet is already connected on mount
  useEffect(() => {
    const checkConnection = async () => {
      if (typeof window !== 'undefined' && window.ethereum) {
        try {
          const accounts = await window.ethereum.request({
            method: 'eth_accounts'
          });
          if (accounts.length > 0) {
            onWalletConnected(accounts[0]);
          }
        } catch (err) {
          // Silently fail, wallet may not be installed
        }
      }
    };

    checkConnection();
  }, []);

  const handleConnect = async () => {
    if (!window.ethereum) {
      setError('MetaMask or another Web3 wallet is required');
      return;
    }

    setIsConnecting(true);
    setError(null);

    try {
      const accounts = await window.ethereum.request({
        method: 'eth_requestAccounts',
        params: []
      });

      if (accounts.length > 0) {
        onWalletConnected(accounts[0]);
      }
    } catch (err: any) {
      setError(
        err.code === -32002
          ? 'Please open MetaMask and complete the connection request'
          : 'Failed to connect wallet'
      );
    } finally {
      setIsConnecting(false);
    }
  };

  const handleDisconnect = () => {
    onWalletDisconnected();
    setError(null);
  };

  if (connectedWallet) {
    return (
      <div className="space-y-4">
        <div className="bg-green-900/30 border border-green-700/50 rounded-lg p-4">
          <p className="text-green-300 text-sm font-medium">Connected to Arbitrum Sepolia</p>
          <p className="text-green-200 font-mono text-sm mt-2 break-all">
            {connectedWallet}
          </p>
        </div>
        <button
          onClick={handleDisconnect}
          className="w-full px-4 py-2 bg-red-600 hover:bg-red-700 text-white rounded-lg transition"
        >
          Disconnect Wallet
        </button>
      </div>
    );
  }

  return (
    <div className="space-y-4">
      <button
        onClick={handleConnect}
        disabled={isConnecting}
        className="w-full px-4 py-3 bg-blue-600 hover:bg-blue-700 disabled:bg-blue-900 text-white rounded-lg font-medium transition"
      >
        {isConnecting ? 'Connecting...' : 'Connect Wallet (MetaMask)'}
      </button>

      {error && (
        <div className="bg-red-900/30 border border-red-700/50 rounded-lg p-4 text-red-300 text-sm">
          {error}
        </div>
      )}

      <p className="text-slate-400 text-sm">
        Requires Arbitrum Sepolia testnet. You will need:
        <br />
        • MetaMask or WalletConnect compatible wallet
        <br />
        • Arbitrum Sepolia network configured
        <br />
        • Some ETH for gas fees
      </p>
    </div>
  );
}
