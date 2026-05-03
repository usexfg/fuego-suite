// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title IEd25519Verifier — Ed25519 signature verification interface
 * @dev Used by FuegoCommitmentMerkleVerifier to verify EFier signatures on merkle roots
 * @dev Deploy a concrete implementation and pass to MerkleVerifier constructor
 *
 * Implementations:
 * - Ed25519VerifierSolidity: Pure Solidity (~500k gas per verify, works on any EVM)
 * - Ed25519VerifierStylus: Arbitrum Stylus / Rust (~50k gas, Arbitrum only)
 * - Ed25519VerifierPrecompile: Native EVM precompile (when available)
 */
interface IEd25519Verifier {
    /**
     * @dev Verify an Ed25519 signature
     * @param pubkey 32-byte Ed25519 public key
     * @param message 32-byte message that was signed (merkle root hash)
     * @param signature 64-byte Ed25519 signature (R[32] || S[32])
     * @return True if signature is valid for this pubkey + message
     */
    function verify(
        bytes32 pubkey,
        bytes32 message,
        bytes calldata signature
    ) external view returns (bool);
}
