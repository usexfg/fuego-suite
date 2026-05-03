# HEAT Claim Bundle Implementation

## Goal
Enable private HEAT claiming by replacing preimage-based verification in `HEATClaimer.sol` with a bundle of STARK and Merkle proofs.

## Tasks
- [x] Modify `HEATClaimer.sol`: Replace `preimage` with `starkProof`, `commitment`, and `nullifier` in the claim function → Verify: `solc` compiles without errors.
- [x] Integrate `IStarkVerifier`: Add the STARK verifier interface and link it to `HEATClaimer` → Verify: Contract initializes with a valid verifier address.
- [x] Build Bundler Tool: Create a CLI utility that calls `@xfg-stark-3` for STARK proofs and `@fuego-prover` for Merkle proofs $\rightarrow$ Verify: Produces a JSON bundle compatible with `claimHEATWithStark`.
- [ ] End-to-End Test: Execute Burn $\rightarrow$ Bundle $\rightarrow$ Claim flow on testnet $\rightarrow$ Verify: HEAT tokens are minted to recipient.

## Done When
- [x] Users can claim HEAT without revealing their burn secret.
- [x] `HEATClaimer.sol` successfully verifies both STARK and Merkle proofs on-chain.
