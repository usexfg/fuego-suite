import React, { useState } from 'react';
import { WalletConnection } from './components/WalletConnection';
import { ClaimForm } from './components/ClaimForm';
import { TransactionStatus } from './components/TransactionStatus';

type TransactionState = {
  status: 'idle' | 'validating' | 'signing' | 'submitting' | 'confirmed' | 'error';
  message: string;
  txHash?: string;
  error?: string;
};

export function App() {
  const [connectedWallet, setConnectedWallet] = useState<string | null>(null);
  const [transactionState, setTransactionState] = useState<TransactionState>({
    status: 'idle',
    message: ''
  });

  const handleWalletConnected = (address: string) => {
    setConnectedWallet(address);
    setTransactionState({ status: 'idle', message: '' });
  };

  const handleWalletDisconnected = () => {
    setConnectedWallet(null);
    setTransactionState({ status: 'idle', message: '' });
  };

  const handleClaimSubmit = async (claimKey: string, signature: string, timestamp: number) => {
    if (!connectedWallet) {
      setTransactionState({
        status: 'error',
        message: 'Wallet not connected',
        error: 'Please connect your wallet first'
      });
      return;
    }

    try {
      setTransactionState({
        status: 'validating',
        message: 'Validating commitment on Fuego blockchain...'
      });

      // Submit to API
      const response = await fetch('/api/cold/claim', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          claimKey,
          signature,
          walletAddress: connectedWallet,
          timestamp
        })
      });

      if (!response.ok) {
        const errorData = await response.json();
        throw new Error(errorData.error || 'API validation failed');
      }

      const data = await response.json();

      if (!data.success) {
        throw new Error(data.error || 'Claim validation failed');
      }

      setTransactionState({
        status: 'signing',
        message: 'Ready to submit to L2 contract. Domain signature received.',
        txHash: data.domainSignature
      });

      // Note: In full implementation, would submit domain signature to L2 contract here
      // For MVP, we just show the domain signature received

      setTransactionState({
        status: 'confirmed',
        message: 'Claim validated! Domain signature received. Submit to L2 contract to mint tokens.',
        txHash: data.domainSignature
      });

    } catch (error: any) {
      setTransactionState({
        status: 'error',
        message: 'Claim validation failed',
        error: error.message || 'Unknown error occurred'
      });
    }
  };

  return (
    <div className="min-h-screen bg-gradient-to-br from-slate-900 via-slate-800 to-slate-900">
      {/* Header */}
      <header className="border-b border-slate-700 bg-slate-800/50">
        <div className="max-w-4xl mx-auto px-4 py-6">
          <h1 className="text-3xl font-bold text-white">
            COLD/HEAT Deposit Claim
          </h1>
          <p className="text-slate-400 mt-2">
            Primary: use <code className="text-blue-300">xfg-stark bundle</code> CLI with EFier consensus.
            This UI uses Option B (domain sig) as fallback — see README.
          </p>
        </div>
      </header>

      {/* Main Content */}
      <main className="max-w-4xl mx-auto px-4 py-12">
        <div className="space-y-8">
          {/* Wallet Connection */}
          <div className="bg-slate-700/50 rounded-lg border border-slate-600 p-6">
            <h2 className="text-xl font-semibold text-white mb-4">
              1. Connect Wallet
            </h2>
            <WalletConnection
              connectedWallet={connectedWallet}
              onWalletConnected={handleWalletConnected}
              onWalletDisconnected={handleWalletDisconnected}
            />
          </div>

          {/* Claim Form */}
          {connectedWallet ? (
            <div className="bg-slate-700/50 rounded-lg border border-slate-600 p-6">
              <h2 className="text-xl font-semibold text-white mb-4">
                2. Submit Claim
              </h2>
              <ClaimForm
                walletAddress={connectedWallet}
                onSubmit={handleClaimSubmit}
                isLoading={
                  transactionState.status === 'validating' ||
                  transactionState.status === 'signing' ||
                  transactionState.status === 'submitting'
                }
              />
            </div>
          ) : (
            <div className="bg-slate-700/50 rounded-lg border border-slate-600 p-6">
              <div className="text-center text-slate-400">
                <p>Connect your wallet to submit a claim</p>
              </div>
            </div>
          )}

          {/* Transaction Status */}
          {transactionState.status !== 'idle' && (
            <div className="bg-slate-700/50 rounded-lg border border-slate-600 p-6">
              <h2 className="text-xl font-semibold text-white mb-4">
                Status
              </h2>
              <TransactionStatus state={transactionState} />
            </div>
          )}

          {/* Info Section */}
          <div className="bg-blue-900/20 rounded-lg border border-blue-700/50 p-6">
            <h3 className="text-lg font-semibold text-blue-300 mb-3">
              Privacy-First Design
            </h3>
            <ul className="text-blue-200 space-y-2 text-sm">
              <li>✓ Your claim key is verified against Fuego blockchain via RPC</li>
              <li>✓ API never receives your original commitment or transaction hash</li>
              <li>✓ Domain signature proves API validated your claim</li>
              <li>✓ Submit domain signature to L2 contract to mint tokens</li>
              <li>✓ All state is on-chain (transparent & decentralized)</li>
            </ul>
          </div>
        </div>
      </main>
    </div>
  );
}

export default App;
