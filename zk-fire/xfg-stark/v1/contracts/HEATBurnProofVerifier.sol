// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "./HEATToken.sol";
import "./interfaces/IArbSys.sol"; // ↖️ new precompile interface
import "./interfaces/IEldernodeVerifier.sol"; // ↖️ Eldernode verification interface

/**
 * @title Fuego Ξmbers Burn Proof Verifier
 * @dev Verifies XFG burn proofs and mints HEAT tokens on Arbitrum
 * @dev Only this contract can mint HEAT tokens through burn proof verification
 * @dev Standardized burn amount: 0.8 XFG = 8M HEAT
 * @dev Large burn amount: 800 XFG = 8B HEAT
 * @dev Privacy-focused: Recommend new addresses per claim
 * @dev Multi-layer validation: STARK proof + Elderfier consensus verification
 */
contract HEATBurnProofVerifier is Ownable, Pausable, ReentrancyGuard {
    
    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */
    
    event ProofVerified(
        bytes32 indexed burnTxHash,
        address indexed recipient,
        uint256 amount,
        bytes32 indexed nullifier
    );
    
    event EldernodeVerification(
        bytes32 indexed burnTxHash,
        uint64 eldernodeCount,
        uint64 consensusThreshold,
        bool consensusReached
    );
    
    event L1GasPaid(
        address indexed user,
        uint256 gasAmount,
        uint256 ticketId,
        bytes32 indexed commitment
    );
    
    event GasFeeRefunded(
        address indexed user,
        uint256 refundAmount,
        uint256 ticketId
    );
    
    event PrivacyViolation(
        address indexed violator,
        string reason
    );
    
    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */
    
    /// @dev HEAT token contract
    EmbersTokenHEAT public immutable heatToken;
    
    /// @dev STARK proof verifier contract
    address public immutable verifier;
    
    /// @dev Eldernode verification contract (optional - can be zero for STARK-only)
    IEldernodeVerifier public eldernodeVerifier;
    
    /// @dev Arbitrum messenger precompile (0x64) – used to send L2→L1 message
    IArbSys public constant ARB_SYS = IArbSys(address(0x64));
    
    /// @dev Standardized XFG burn amount (0.8 XFG)
    uint256 public constant STANDARDIZED_XFG_BURN = 8_000_000; // 0.8 XFG in smallest units
    
    /// @dev Standardized HEAT mint amount (8,000,000 HEAT)
    uint256 public constant STANDARDIZED_HEAT_MINT = 8_000_000 * 10**18;
    
    /// @dev Large XFG burn amount (800 XFG)
    uint256 public constant LARGE_XFG_BURN = 8_000_000_000; // 800 XFG in smallest units

    /// @dev Large HEAT mint amount (8,000,000,000 HEAT)
    uint256 public constant LARGE_HEAT_MINT = 8_000_000_000 * 10**18;

    /// @dev Version 2 - Medium XFG burn amount (80 XFG)
    uint256 public constant MEDIUM_XFG_BURN = 800_000_000; // 80 XFG in smallest units

    /// @dev Version 2 - Medium HEAT mint amount (800,000,000 HEAT)
    uint256 public constant MEDIUM_HEAT_MINT = 800_000_000 * 10**18;

    /// @dev Fuego network ID (chain ID) - 46414e44-4f4d-474f-4c44-001210110110
    uint256 public constant FUEGO_NETWORK_ID = 93385046440755750514194170694064996624;
    
    /// @dev Minimum Eldernode consensus threshold (e.g., 3 out of 5)
    uint64 public constant MIN_ELDERNODE_CONSENSUS = 3;   // todo: add new F3 tiers - FastPass (2/2), Fallback (4/5), Full Quorum (8/10)
    
    /// @dev Whether Eldernode verification is required (can be disabled for testing)
    bool public eldernodeVerificationRequired = true;
    
    /// @dev Used nullifiers to prevent double-spending
    mapping(bytes32 => bool) public nullifiersUsed;
    
    /// @dev Statistics
    uint256 public totalProofsVerified;
    uint256 public totalHEATMinted;
    uint256 public totalClaims;
    uint256 public totalEldernodeVerifications;
    uint256 public totalEldernodeFailures;
    
    /* -------------------------------------------------------------------------- */
    /*                                 Constructor                                */
    /* -------------------------------------------------------------------------- */
    
    constructor(
        address _heatToken,
        address _verifier,
        address _eldernodeVerifier,
        address initialOwner
    ) Ownable(initialOwner) {
        require(_heatToken != address(0), "Invalid HEAT token address");
        require(_verifier != address(0), "Invalid verifier address");
        
        heatToken = EmbersTokenHEAT(_heatToken);
        verifier = _verifier;
        eldernodeVerifier = IEldernodeVerifier(_eldernodeVerifier);
    }
    
    /* -------------------------------------------------------------------------- */
    /*                              Core Functions                                */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Claim HEAT tokens by providing XFG burn proof with network validation
     * @param secret Secret from XFG transaction extra field
     * @param proof STARK proof bytes
     * @param publicInputs Public inputs for proof verification [nullifier, commitment, recipientHash, networkId] // <--tx_hash?
     * @param recipient Address to receive HEAT tokens
     * @param isLargeBurn True for 800 XFG burn (8B HEAT), false for 0.8 XFG burn (8M HEAT)
     * @param eldernodeProof Optional Eldernode consensus proof (if verification required)
     */
    function claimHEAT(
        bytes32 secret,
        bytes calldata proof,
        bytes32[] calldata publicInputs,
        address recipient,
        bool isLargeBurn,
        bytes calldata eldernodeProof
    ) external whenNotPaused nonReentrant {
        require(recipient != address(0), "Invalid recipient address");
        require(publicInputs.length == 4, "Invalid public inputs length (need 4: nullifier, commitment, recipientHash, networkId)");
        
        // Extract public inputs
        bytes32 nullifier = publicInputs[0];
        bytes32 commitment = publicInputs[1];
        bytes32 recipientHash = publicInputs[2];
        uint256 networkId = uint256(publicInputs[3]); /// Todo txn_hash?
        
        // Verify nullifier hasn't been used
        require(!nullifiersUsed[nullifier], "Nullifier already used");
        
        // Verify recipient hash matches
        require(
            recipientHash == keccak256(abi.encodePacked(recipient)),
            "Recipient hash mismatch"
        );
        
        // Verify network ID
        require(networkId == FUEGO_NETWORK_ID, "Invalid network ID");
        
        // Verify STARK proof
        bool proofValid = verifyStarkProof(proof, publicInputs);
        require(proofValid, "Invalid STARK proof");
        
        // Verify Eldernode consensus (if required and verifier is set)
        if (eldernodeVerificationRequired && address(eldernodeVerifier) != address(0)) {
            bool eldernodeValid = verifyEldernodeConsensus(commitment, eldernodeProof);
            require(eldernodeValid, "Eldernode consensus verification failed");
            totalEldernodeVerifications += 1;
        } else if (eldernodeVerificationRequired && address(eldernodeVerifier) == address(0)) {
            totalEldernodeFailures += 1;
            revert("Eldernode verification required but no verifier set");
        }
        
        // Mark nullifier used (prevent replay on L2)
        nullifiersUsed[nullifier] = true;

        // ------------------------------------------------------------------
        // 📤  SEND MESSAGE TO L1 HEAT TOKEN CONTRACT VIA ARB SYS
        // ------------------------------------------------------------------

        uint256 heatAmount = isLargeBurn ? LARGE_HEAT_MINT : STANDARDIZED_HEAT_MINT;

        // Compose calldata for L1 mint function
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint32)",
            commitment,
            recipient,
            heatAmount,
            1  // commitment_version = 1
        );

        // Enqueue call via ArbSys with L1 gas fees – returns ticket ID
        // Note: Arbitrum automatically refunds any leftover gas fees to the sender
        uint256 ticketId = ARB_SYS.sendTxToL1{value: msg.value}(address(heatToken), data);
        
        // Emit L1 gas payment event
        emit L1GasPaid(msg.sender, msg.value, ticketId, commitment);

        emit ProofVerified(burnTxHashFromCommitment(commitment), recipient, heatAmount, nullifier);
        emit EldernodeVerification(
            burnTxHashFromCommitment(commitment),
            eldernodeVerificationRequired ? 5 : 0, // Assuming 5 Eldernodes
            MIN_ELDERNODE_CONSENSUS,
            eldernodeVerificationRequired && address(eldernodeVerifier) != address(0)
        );

        totalProofsVerified += 1;
        totalHEATMinted += heatAmount;
        totalClaims += 1;

        return;
    }

    /**
     * @dev Claim HEAT tokens using version 2 proof (3 burn tiers)
     * @param secret Secret used in commitment
     * @param proof STARK proof bytes
     * @param publicInputs Public inputs for proof verification [nullifier, commitment, recipientHash, networkId]
     * @param recipient Address to receive HEAT tokens
     * @param burnTier Tier index: 0=0.8 XFG, 1=80 XFG, 2=800 XFG
     * @param eldernodeProof Optional Eldernode consensus proof (if verification required)
     */
    function claimHEATv2(
        bytes32 secret,
        bytes calldata proof,
        bytes32[] calldata publicInputs,
        address recipient,
        uint8 burnTier,
        bytes calldata eldernodeProof
    ) external whenNotPaused nonReentrant {
        require(recipient != address(0), "Invalid recipient address");
        require(publicInputs.length == 4, "Invalid public inputs length (need 4: nullifier, commitment, recipientHash, networkId)");
        require(burnTier <= 2, "Invalid burn tier (must be 0, 1, or 2)");

        // Extract public inputs
        bytes32 nullifier = publicInputs[0];
        bytes32 commitment = publicInputs[1];
        bytes32 recipientHash = publicInputs[2];
        uint256 networkId = uint256(publicInputs[3]);

        // Verify nullifier hasn't been used
        require(!nullifiersUsed[nullifier], "Nullifier already used");

        // Verify recipient hash matches
        require(
            recipientHash == keccak256(abi.encodePacked(recipient)),
            "Recipient hash mismatch"
        );

        // Verify network ID
        require(networkId == FUEGO_NETWORK_ID, "Invalid network ID");

        // Verify STARK proof
        bool proofValid = verifyStarkProof(proof, publicInputs);
        require(proofValid, "Invalid STARK proof");

        // Verify Eldernode consensus (if required)
        if (eldernodeVerificationRequired && address(eldernodeVerifier) != address(0)) {
            bool eldernodeValid = verifyEldernodeConsensus(commitment, eldernodeProof);
            require(eldernodeValid, "Eldernode consensus verification failed");
            totalEldernodeVerifications += 1;
        } else if (eldernodeVerificationRequired && address(eldernodeVerifier) == address(0)) {
            totalEldernodeFailures += 1;
            revert("Eldernode verification required but no verifier set");
        }

        // Mark nullifier used
        nullifiersUsed[nullifier] = true;

        // ------------------------------------------------------------------
        // 📤  SEND MESSAGE TO L1 HEAT TOKEN CONTRACT VIA ARB SYS (VERSION 2)
        // ------------------------------------------------------------------

        // Determine HEAT amount based on burn tier
        uint256 heatAmount;
        if (burnTier == 0) {
            heatAmount = STANDARDIZED_HEAT_MINT;  // 8M HEAT
        } else if (burnTier == 1) {
            heatAmount = MEDIUM_HEAT_MINT;        // 800M HEAT
        } else {
            heatAmount = LARGE_HEAT_MINT;         // 8B HEAT
        }

        // Compose calldata for L1 mint function with version=2
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint32)",
            commitment,
            recipient,
            heatAmount,
            2  // commitment_version = 2
        );

        // Enqueue call via ArbSys with L1 gas fees – returns ticket ID
        uint256 ticketId = ARB_SYS.sendTxToL1{value: msg.value}(address(heatToken), data);

        // Emit L1 gas payment event
        emit L1GasPaid(msg.sender, msg.value, ticketId, commitment);

        emit ProofVerified(burnTxHashFromCommitment(commitment), recipient, heatAmount, nullifier);
        emit EldernodeVerification(
            burnTxHashFromCommitment(commitment),
            eldernodeVerificationRequired ? 5 : 0,
            MIN_ELDERNODE_CONSENSUS,
            eldernodeVerificationRequired && address(eldernodeVerifier) != address(0)
        );

        totalProofsVerified += 1;
        totalHEATMinted += heatAmount;
        totalClaims += 1;

        return;
    }

    /**
     * @dev Estimate L1 gas fees for cross-chain minting
     * @param recipient Address to receive HEAT tokens
     * @param isLargeBurn True for 800 XFG burn (8B HEAT), false for 0.8 XFG burn (8M HEAT)
     * @return estimatedGasFee Estimated L1 gas fee in wei
     * @dev This is an estimate - actual fees may vary based on L1 gas prices
     */
    function estimateL1GasFee(address recipient, bool isLargeBurn) external view returns (uint256 estimatedGasFee) {
        uint256 heatAmount = isLargeBurn ? LARGE_HEAT_MINT : STANDARDIZED_HEAT_MINT;
        
        // Compose calldata for L1 mint function
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint32)",
            bytes32(0), // placeholder commitment
            recipient,
            heatAmount,
            1  // commitment_version = 1
        );
        
        // Estimate L1 gas fee based on calldata size and current L1 gas price
        // This is a simplified estimation - in production, you'd query actual L1 gas prices
        uint256 calldataSize = data.length;
        uint256 estimatedL1GasPrice = 20 gwei; // Conservative estimate
        
        // Base cost for L2→L1 message + calldata cost
        estimatedGasFee = (21000 + calldataSize * 16) * estimatedL1GasPrice;
        
        return estimatedGasFee;
    }

    /**
     * @dev Estimate L1 gas fees for version 2 cross-chain minting
     * @param recipient Address to receive HEAT tokens
     * @param burnTier Tier index: 0=0.8 XFG, 1=80 XFG, 2=800 XFG
     * @return estimatedGasFee Estimated L1 gas fee in wei
     * @dev This is an estimate - actual fees may vary based on L1 gas prices
     */
    function estimateL1GasFeeV2(address recipient, uint8 burnTier) external view returns (uint256 estimatedGasFee) {
        require(burnTier <= 2, "Invalid burn tier");

        uint256 heatAmount;
        if (burnTier == 0) {
            heatAmount = STANDARDIZED_HEAT_MINT;
        } else if (burnTier == 1) {
            heatAmount = MEDIUM_HEAT_MINT;
        } else {
            heatAmount = LARGE_HEAT_MINT;
        }

        // Compose calldata for L1 mint function
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint32)",
            bytes32(0), // placeholder commitment
            recipient,
            heatAmount,
            2  // commitment_version = 2
        );

        // Estimate L1 gas fee based on calldata size and current L1 gas price
        uint256 calldataSize = data.length;
        uint256 estimatedL1GasPrice = 20 gwei; // Conservative estimate

        // Base cost for L2→L1 message + calldata cost
        estimatedGasFee = (21000 + calldataSize * 16) * estimatedL1GasPrice;

        return estimatedGasFee;
    }

    /**
     * @dev Get recommended L1 gas fee with 20% buffer
     * @param recipient Address to receive HEAT tokens
     * @param isLargeBurn True for 800 XFG burn (8B HEAT), false for 0.8 XFG burn (8M HEAT)
     * @return recommendedFee Recommended L1 gas fee with 20% buffer
     * @dev Includes 20% buffer to prevent transaction failures
     */
    function getRecommendedGasFee(address recipient, bool isLargeBurn) external view returns (uint256 recommendedFee) {
        uint256 baseFee = estimateL1GasFee(recipient, isLargeBurn);
        recommendedFee = (baseFee * 120) / 100; // 20% buffer
        return recommendedFee;
    }
    
    /**
     * @dev Get current L1 gas price from Arbitrum
     * @return Current L1 gas price in wei
     */
    function getCurrentL1GasPrice() external view returns (uint256) {
        // In a real implementation, you'd query Arbitrum's L1 gas price oracle
        // For now, return a conservative estimate
        return 20 gwei;
    }
    
    /**
     * @dev Verify Eldernode consensus proof
     * @param commitment Commitment to verify
     * @param eldernodeProof Eldernode consensus proof data
     * @return True if consensus verification passes
     */
    function verifyEldernodeConsensus(bytes32 commitment, bytes calldata eldernodeProof) internal view returns (bool) {
        if (address(eldernodeVerifier) == address(0)) {
            return false;
        }
        
        try eldernodeVerifier.verifyConsensusProof(commitment, eldernodeProof) returns (bool isValid) {
            return isValid;
        } catch {
            return false;
        }
    }
    
    /**
     * @dev Set Eldernode verifier contract (owner only)
     * @param _eldernodeVerifier New Eldernode verifier address
     */
    function setEldernodeVerifier(address _eldernodeVerifier) external onlyOwner {
        eldernodeVerifier = IEldernodeVerifier(_eldernodeVerifier);
    }
    
    /**
     * @dev Toggle Eldernode verification requirement (owner only)
     * @param _required Whether Eldernode verification is required
     */
    function setEldernodeVerificationRequired(bool _required) external onlyOwner {
        eldernodeVerificationRequired = _required;
    }
    
    /* -------------------------------------------------------------------------- */
    /*                              Helper Functions                              */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Verify STARK proof using the verifier contract
     * @param proof STARK proof bytes
     * @param publicInputs Public inputs for verification
     * @return True if proof is valid
     */
    function verifyStarkProof(bytes calldata proof, bytes32[] calldata publicInputs) internal view returns (bool) {
        // Call the STARK verifier contract
        (bool success, bytes memory result) = verifier.staticcall(
            abi.encodeWithSignature(
                "verifyProof(bytes,bytes32[])",
                proof,
                publicInputs
            )
        );
        
        if (!success) {
            return false;
        }
        
        return abi.decode(result, (bool));
    }
    
    /**
     * @dev Extract burn transaction hash from commitment (for events)
     * @param commitment Commitment from STARK proof
     * @return Burn transaction hash
     */
    function burnTxHashFromCommitment(bytes32 commitment) internal pure returns (bytes32) {
        // This is a simplified version - in practice, you'd extract the actual tx hash
        // from the commitment based on your commitment format
        return keccak256(abi.encodePacked("burn_tx_", commitment));
    }
    
    /* -------------------------------------------------------------------------- */
    /*                              Admin Functions                               */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Pause the contract (owner only)
     */
    function pause() external onlyOwner {
        _pause();
    }
    
    /**
     * @dev Unpause the contract (owner only)
     */
    function unpause() external onlyOwner {
        _unpause();
    }
    
    /**
     * @dev Emergency function to recover stuck tokens
     * @param token Token address to recover
     * @param to Recipient address
     * @param amount Amount to recover
     */
    function emergencyRecover(
        address token,
        address to,
        uint256 amount
    ) external onlyOwner {
        require(to != address(0), "Invalid recipient");
        require(amount > 0, "Invalid amount");
        
        if (token == address(0)) {
            // Recover ETH
            (bool success, ) = to.call{value: amount}("");
            require(success, "ETH transfer failed");
        } else {
            // Recover ERC20 tokens
            require(
                IERC20(token).transfer(to, amount),
                "Token transfer failed"
            );
        }
    }
    
    /**
     * @dev Emergency function to update HEAT token minter (if needed)
     * @param newMinter New minter address
     */
    function emergencyUpdateMinter(address newMinter) external onlyOwner {
        require(newMinter != address(0), "Invalid minter address");
        heatToken.updateMinter(newMinter);
    }
    
    /**
     * @dev Get contract statistics
     * @return stats Statistics string
     */
    function getStatistics() external view returns (string memory) {
        return string(abi.encodePacked(
            "Total Proofs Verified: ", uint2str(totalProofsVerified), "\n",
            "Total HEAT Minted: ", uint2str(totalHEATMinted), "\n",
            "Total Claims: ", uint2str(totalClaims), "\n",
            "Eldernode Verifications: ", uint2str(totalEldernodeVerifications), "\n",
            "Eldernode Failures: ", uint2str(totalEldernodeFailures), "\n",
            "Eldernode Required: ", eldernodeVerificationRequired ? "true" : "false"
        ));
    }
    
    /**
     * @dev Convert uint to string (helper function)
     * @param _i Number to convert
     * @return String representation
     */
    function uint2str(uint256 _i) internal pure returns (string memory) {
        if (_i == 0) {
            return "0";
        }
        uint256 j = _i;
        uint256 length;
        while (j != 0) {
            length++;
            j /= 10;
        }
        bytes memory bstr = new bytes(length);
        uint256 k = length;
        while (_i != 0) {
            k -= 1;
            uint8 temp = (48 + uint8(_i - _i / 10 * 10));
            bytes1 b1 = bytes1(temp);
            bstr[k] = b1;
            _i /= 10;
        }
        return string(bstr);
    }
    
    /**
     * @dev Get conversion rates
     * @return _heatPerXfg HEAT per XFG
     * @return _standardizedBurn Standardized burn amount
     * @return _standardizedMint Standardized mint amount
     */
    function getConversionRates() external pure returns (
        uint256 _heatPerXfg,
        uint256 _standardizedBurn,
        uint256 _standardizedMint
    ) {
        return (
            10_000_000 * 10**18, // HEAT per XFG (1 XFG = 10M HEAT)
            STANDARDIZED_XFG_BURN,
            STANDARDIZED_HEAT_MINT
        );
    }
    
    /**
     * @dev Get burn and mint amounts for both burn types
     * @return standardizedXfgBurn Standardized XFG burn amount (0.8 XFG)
     * @return standardizedHeatMint Standardized HEAT mint amount (8M HEAT)
     * @return largeXfgBurn Large XFG burn amount (800 XFG)
     * @return largeHeatMint Large HEAT mint amount (8B HEAT)
     */
    function getBurnMintAmounts() external pure returns (
        uint256 standardizedXfgBurn,
        uint256 standardizedHeatMint,
        uint256 largeXfgBurn,
        uint256 largeHeatMint
    ) {
        return (STANDARDIZED_XFG_BURN, STANDARDIZED_HEAT_MINT, LARGE_XFG_BURN, LARGE_HEAT_MINT);
    }
    
    /* -------------------------------------------------------------------------- */
    /*                              Receive Function                              */
    /* -------------------------------------------------------------------------- */
    
    /**
     * @dev Allow contract to receive ETH (for emergency recovery)
     */
    receive() external payable {}
}


