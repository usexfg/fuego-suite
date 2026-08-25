// SPDX-License-Identifier: GPL-3.0
pragma solidity ^0.8.20;

/// @title Point Time-Lock Contract for XFG atomic swaps (PTLC, Phase 1 bridge wrapper)
/// @notice Phase 1 bridge: on-chain HTLC (keccak256) + off-chain point commitment.
///         The `ptlcPoint` (T=t*G) is stored as metadata for per-hop decorrelation;
///         on-chain claim still verifies keccak256(preimage)==hashLock, but the
///         off-chain DLEQ proof `log_G(T)=log_{escrowPubKey}(Q)` guarantees the same t.
///         Phase 4 will add native Schnorr adaptor verify `s'*G == R+e*P+T` via
///         EIP-6601 secp256k1 precompile when available.
/// @dev Deployed alongside HashedTimelock; xfg-swapd picks HashedTimelock for HTLC
///      and PtlcTimelock for PTLC/BRIDGE (same ABI, extra event).
import "./HashedTimelock.sol";

contract PtlcTimelock is HashedTimelock {
    event PtlcLocked(bytes32 indexed contractId, bytes32 indexed ptlcPoint, address indexed sender);

    // Lock with explicit point for PTLC/BRIDGE. HashLock is still H(t) for on-chain claim.
    function lockWithPoint(address payable recipient, bytes32 hashLock, bytes32 ptlcPoint, uint256 timeoutBlock)
        external payable returns (bytes32 contractId)
    {
        contractId = lock(recipient, hashLock, timeoutBlock);
        emit PtlcLocked(contractId, ptlcPoint, msg.sender);
    }

    // View helper: return stored contract with its ptlcPoint from event (off-chain).
    // On-chain storage remains hashLock; ptlcPoint is not stored to save gas.
}
