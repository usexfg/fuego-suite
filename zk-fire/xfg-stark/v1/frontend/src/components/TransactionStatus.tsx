import React from 'react';

interface TransactionState {
  status: 'idle' | 'validating' | 'signing' | 'submitting' | 'confirmed' | 'error';
  message: string;
  txHash?: string;
  error?: string;
}

interface TransactionStatusProps {
  state: TransactionState;
}

export function TransactionStatus({ state }: TransactionStatusProps) {
  const getStatusColor = () => {
    switch (state.status) {
      case 'validating':
      case 'signing':
      case 'submitting':
        return 'bg-yellow-900/30 border-yellow-700/50 text-yellow-300';
      case 'confirmed':
        return 'bg-green-900/30 border-green-700/50 text-green-300';
      case 'error':
        return 'bg-red-900/30 border-red-700/50 text-red-300';
      default:
        return 'bg-slate-600/50 border-slate-500/50 text-slate-300';
    }
  };

  const getStatusIcon = () => {
    switch (state.status) {
      case 'validating':
      case 'signing':
      case 'submitting':
        return (
          <div className="animate-spin">
            <svg className="w-5 h-5" fill="currentColor" viewBox="0 0 20 20">
              <path
                fillRule="evenodd"
                d="M4.293 4.293a1 1 0 011.414 0L10 8.586l4.293-4.293a1 1 0 111.414 1.414L11.414 10l4.293 4.293a1 1 0 01-1.414 1.414L10 11.414l-4.293 4.293a1 1 0 01-1.414-1.414L8.586 10 4.293 5.707a1 1 0 010-1.414z"
                clipRule="evenodd"
              />
            </svg>
          </div>
        );
      case 'confirmed':
        return (
          <svg className="w-5 h-5" fill="currentColor" viewBox="0 0 20 20">
            <path
              fillRule="evenodd"
              d="M10 18a8 8 0 100-16 8 8 0 000 16zm3.707-9.293a1 1 0 00-1.414-1.414L9 10.586 7.707 9.293a1 1 0 00-1.414 1.414l2 2a1 1 0 001.414 0l4-4z"
              clipRule="evenodd"
            />
          </svg>
        );
      case 'error':
        return (
          <svg className="w-5 h-5" fill="currentColor" viewBox="0 0 20 20">
            <path
              fillRule="evenodd"
              d="M10 18a8 8 0 100-16 8 8 0 000 16zM8.707 7.293a1 1 0 00-1.414 1.414L8.586 10l-1.293 1.293a1 1 0 101.414 1.414L10 11.414l1.293 1.293a1 1 0 001.414-1.414L11.414 10l1.293-1.293a1 1 0 00-1.414-1.414L10 8.586 8.707 7.293z"
              clipRule="evenodd"
            />
          </svg>
        );
      default:
        return null;
    }
  };

  return (
    <div className={`rounded-lg border p-6 ${getStatusColor()}`}>
      <div className="flex items-start gap-4">
        <div className="mt-1">{getStatusIcon()}</div>
        <div className="flex-1">
          <p className="font-semibold mb-2">{state.message}</p>

          {state.txHash && (
            <div className="bg-slate-900/50 rounded px-3 py-2 text-xs font-mono break-all mb-3">
              {state.txHash}
            </div>
          )}

          {state.error && (
            <div className="text-sm mt-3 bg-black/20 rounded px-3 py-2">
              <p className="font-medium mb-1">Error Details:</p>
              <p>{state.error}</p>
            </div>
          )}

          {state.status === 'confirmed' && (
            <div className="mt-4 space-y-2 text-sm">
              <p className="font-medium">Next Steps:</p>
              <ol className="list-decimal list-inside space-y-1 text-white">
                <li>Copy the domain signature above</li>
                <li>Go to the L2 contract claim function</li>
                <li>Submit: claimKey, domainSignature, walletAddress</li>
                <li>Confirm transaction in MetaMask</li>
                <li>Tokens will mint to your wallet</li>
              </ol>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
