// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "./FuegoCOLDAOToken.sol";
import "./interfaces/IArbSys.sol";
import "./interfaces/IEldernodeVerifier.sol";
import "./interfaces/ICOLDAOGovernor.sol";

/**
 * @title COLD Deposit Proof Verifier (Version 3)
 * @dev Verifies XFG deposit proofs and mints CD INTEREST tokens on Arbitrum
 * @dev XFG principal is LOCKED (not burned) - unlocks after 3 months on Fuego
 * @dev Only CD INTEREST is minted to depositor (principal stays locked)
 * @dev CD tokens serve multiple purposes:
 *      1. Voting power in COLDAO governance
 *      2. Interest earned from locked XFG deposits (highest APY)
 *      3. Liquidity rewards for HEAT/ETH pair LPs (lower APY than XFG deposits)
 * @dev Interest rate hierarchy: XFG principal deposits > HEAT/ETH LP rewards
 * @dev Interest calculation: Supply ratio first (1:100,000), then APY applied
 * @dev Example: 0.8 XFG at 8% APY → 0.000008 base × 0.08 = 0.00000064 CD minted
 * @dev Three deposit tiers: 0.8 XFG, 80 XFG, 800 XFG
 * @dev Privacy-focused: Recommend new addresses per claim
 * @dev Multi-layer validation: STARK proof + Elderfier consensus verification
 */
contract COLDProofVerifier is Ownable, Pausable, ReentrancyGuard {

    /* -------------------------------------------------------------------------- */
    /*                                   Events                                   */
    /* -------------------------------------------------------------------------- */

    event ProofVerified(
        bytes32 indexed depositTxHash,
        address indexed recipient,
        uint256 xfgPrincipal,
        uint256 cdInterest,
        bytes32 indexed nullifier
    );

    event EldernodeVerification(
        bytes32 indexed depositTxHash,
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

    event InterestCalculated(
        uint256 xfgPrincipal,
        uint256 baseAmount,
        uint256 apyBps,
        uint256 cdInterest
    );

    /* -------------------------------------------------------------------------- */
    /*                                   State                                    */
    /* -------------------------------------------------------------------------- */

    /// @dev Fuego COLDAO token contract (CD)
    FuegoCOLDAOToken public immutable cdToken;

    /// @dev STARK proof verifier contract
    address public immutable verifier;

    /// @dev Eldernode verification contract (optional - can be zero for STARK-only)
    IEldernodeVerifier public eldernodeVerifier;

    /// @dev COLDAO governor contract (provides current APY)
    ICOLDAOGovernor public coldaoGovernor;

    /// @dev Arbitrum messenger precompile (0x64) – used to send L2→L1 message
    IArbSys public constant ARB_SYS = IArbSys(address(0x64));

    /// @dev Tier 0 constants (0.8 XFG)
    uint256 public constant TIER0_XFG_DEPOSIT = 8_000_000; // 0.8 XFG in atomic units (7 decimals)

    /// @dev Tier 1 constants (80 XFG)
    uint256 public constant TIER1_XFG_DEPOSIT = 800_000_000; // 80 XFG in atomic units

    /// @dev Tier 2 constants (800 XFG)
    uint256 public constant TIER2_XFG_DEPOSIT = 8_000_000_000; // 800 XFG in atomic units

    /// @dev Supply ratio: 1 COLD : 100,000 XFG
    uint256 public constant SUPPLY_RATIO_DENOMINATOR = 100_000;

    /// @dev CD token decimals (12)
    uint256 public constant CD_DECIMALS = 12;

    /// @dev XFG decimals (7)
    uint256 public constant XFG_DECIMALS = 7;

    /// @dev Fuego network ID (chain ID)
    uint256 public constant FUEGO_NETWORK_ID = 93385046440755750514194170694064996624;

    /// @dev Minimum Eldernode consensus threshold
    uint64 public constant MIN_ELDERNODE_CONSENSUS = 3;

    /// @dev Whether Eldernode verification is required
    bool public eldernodeVerificationRequired = true;

    /// @dev Used nullifiers to prevent double-spending
    mapping(bytes32 => bool) public nullifiersUsed;

    /// @dev Statistics
    uint256 public totalProofsVerified;
    uint256 public totalCDInterestMinted;
    uint256 public totalXFGPrincipalLocked;
    uint256 public totalClaims;
    uint256 public totalEldernodeVerifications;
    uint256 public totalEldernodeFailures;

    /* -------------------------------------------------------------------------- */
    /*                                 Constructor                                */
    /* -------------------------------------------------------------------------- */

    constructor(
        address _cdToken,
        address _verifier,
        address _eldernodeVerifier,
        address _coldaoGovernor,
        address initialOwner
    ) Ownable(initialOwner) {
        require(_cdToken != address(0), "Invalid CD token address");
        require(_verifier != address(0), "Invalid verifier address");
        require(_coldaoGovernor != address(0), "Invalid COLDAO governor address");

        cdToken = FuegoCOLDAOToken(_cdToken);
        verifier = _verifier;
        eldernodeVerifier = IEldernodeVerifier(_eldernodeVerifier);
        coldaoGovernor = ICOLDAOGovernor(_coldaoGovernor);
    }

    /* -------------------------------------------------------------------------- */
    /*                              Core Functions                                */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Claim CD interest tokens by providing XFG deposit proof (Version 3)
     * @param secret Secret from XFG transaction extra field
     * @param proof STARK proof bytes
     * @param publicInputs Public inputs for proof verification [nullifier, commitment, recipientHash, networkId]
     * @param recipient Address to receive CD tokens
     * @param depositTier Tier index: 0=0.8 XFG, 1=80 XFG, 2=800 XFG
     * @param eldernodeProof Optional Eldernode consensus proof (if verification required)
     */
    function claimCDInterest(
        bytes32 secret,
        bytes calldata proof,
        bytes32[] calldata publicInputs,
        address recipient,
        uint8 depositTier,
        bytes calldata eldernodeProof
    ) external payable whenNotPaused nonReentrant {
        require(recipient != address(0), "Invalid recipient address");
        require(publicInputs.length == 4, "Invalid public inputs length");
        require(depositTier <= 2, "Invalid deposit tier (must be 0, 1, or 2)");

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

        // Mark nullifier used (prevent replay on L2)
        nullifiersUsed[nullifier] = true;

        // Determine XFG principal based on deposit tier
        uint256 xfgPrincipal;
        if (depositTier == 0) {
            xfgPrincipal = TIER0_XFG_DEPOSIT; // 0.8 XFG
        } else if (depositTier == 1) {
            xfgPrincipal = TIER1_XFG_DEPOSIT; // 80 XFG
        } else {
            xfgPrincipal = TIER2_XFG_DEPOSIT; // 800 XFG
        }

        // Calculate CD interest amount
        uint256 cdInterest = calculateInterest(xfgPrincipal);
        require(cdInterest > 0, "Interest amount must be greater than 0");

        // Get current edition ID from CD token
        uint256 editionId = cdToken.currentEditionId() - 1; // Current active edition

        // ------------------------------------------------------------------
        // 📤  SEND MESSAGE TO L1 CD TOKEN CONTRACT VIA ARB SYS
        // ------------------------------------------------------------------

        // Compose calldata for L1 mint function with version=3
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint256,uint256,uint32)",
            commitment,
            recipient,
            editionId,
            cdInterest,
            xfgPrincipal,
            3 // commitment_version = 3 for COLD deposits
        );

        // Send cross-chain message to L1
        uint256 ticketId = ARB_SYS.sendTxToL1{value: msg.value}(address(cdToken), data);

        emit L1GasPaid(msg.sender, msg.value, ticketId, commitment);
        emit ProofVerified(
            depositTxHashFromCommitment(commitment),
            recipient,
            xfgPrincipal,
            cdInterest,
            nullifier
        );
        emit EldernodeVerification(
            depositTxHashFromCommitment(commitment),
            eldernodeVerificationRequired ? 5 : 0,
            MIN_ELDERNODE_CONSENSUS,
            eldernodeVerificationRequired && address(eldernodeVerifier) != address(0)
        );

        // Update statistics
        totalProofsVerified += 1;
        totalCDInterestMinted += cdInterest;
        totalXFGPrincipalLocked += xfgPrincipal;
        totalClaims += 1;
    }

    /* -------------------------------------------------------------------------- */
    /*                          Interest Calculation                              */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Calculate CD interest from XFG principal
     * @dev Formula: (XFG / 100,000) × APY
     * @dev Step 1: Apply supply ratio (1 COLD : 100,000 XFG)
     * @dev Step 2: Apply APY from COLDAO governor
     * @param xfgPrincipal XFG principal amount (in atomic units with 7 decimals)
     * @return cdInterest CD interest amount (in atomic units with 12 decimals)
     *
     * Example: 0.8 XFG at 8% APY
     *   xfgPrincipal = 8,000,000 (0.8 XFG in atomic units)
     *   baseAmount = 8,000,000 / 100,000 = 80 (base COLD atomic units)
     *   Convert to 12 decimals: 80 * 10^12 / 10^7 = 8,000,000 (0.000008 COLD)
     *   Apply 8% APY: 8,000,000 * 800 / 10,000 = 640 (0.00000064 CD)
     */
    function calculateInterest(uint256 xfgPrincipal)
        public
        view
        returns (uint256 cdInterest)
    {
        // Get current APY from COLDAO governor (in basis points, e.g., 800 = 8%)
        uint256 apyBps = coldaoGovernor.getCurrentAPY();
        require(apyBps > 0, "APY must be greater than 0");
        require(apyBps <= 10000, "APY cannot exceed 100%");

        // Step 1: Apply supply ratio (1 COLD : 100,000 XFG)
        // baseAmount = xfgPrincipal / SUPPLY_RATIO_DENOMINATOR
        // This gives us XFG value in COLD units (still 7 decimals)
        uint256 baseAmount = xfgPrincipal / SUPPLY_RATIO_DENOMINATOR;

        // Step 2: Convert from XFG decimals (7) to CD decimals (12)
        // Multiply by 10^(12-7) = 10^5
        uint256 baseAmountCD = baseAmount * 10**(CD_DECIMALS - XFG_DECIMALS);

        // Step 3: Apply APY
        // cdInterest = baseAmountCD * apyBps / 10,000
        cdInterest = (baseAmountCD * apyBps) / 10000;

        emit InterestCalculated(xfgPrincipal, baseAmountCD, apyBps, cdInterest);

        return cdInterest;
    }

    /* -------------------------------------------------------------------------- */
    /*                          Gas Estimation Functions                          */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Estimate L1 gas fees for cross-chain CD minting
     * @param recipient Address to receive CD tokens
     * @param depositTier Tier index: 0=0.8 XFG, 1=80 XFG, 2=800 XFG
     * @return estimatedGasFee Estimated L1 gas fee in wei
     */
    function estimateL1GasFee(address recipient, uint8 depositTier)
        external
        view
        returns (uint256 estimatedGasFee)
    {
        require(depositTier <= 2, "Invalid deposit tier");

        uint256 xfgPrincipal;
        if (depositTier == 0) {
            xfgPrincipal = TIER0_XFG_DEPOSIT;
        } else if (depositTier == 1) {
            xfgPrincipal = TIER1_XFG_DEPOSIT;
        } else {
            xfgPrincipal = TIER2_XFG_DEPOSIT;
        }

        uint256 cdInterest = calculateInterest(xfgPrincipal);
        uint256 editionId = cdToken.currentEditionId() - 1;

        // Compose the same calldata that will be sent
        bytes memory data = abi.encodeWithSignature(
            "mintFromL2(bytes32,address,uint256,uint256,uint256,uint32)",
            bytes32(0), // dummy commitment
            recipient,
            editionId,
            cdInterest,
            xfgPrincipal,
            3 // version 3
        );

        // Get current L1 base fee
        uint256 l1BaseFee = block.basefee;

        // Estimate calldata cost (16 gas per non-zero byte, 4 per zero byte)
        uint256 calldataGas = 0;
        for (uint256 i = 0; i < data.length; i++) {
            if (data[i] != 0) {
                calldataGas += 16;
            } else {
                calldataGas += 4;
            }
        }

        // Estimate execution gas (mintFromL2 on L1)
        uint256 executionGas = 200000; // Conservative estimate for ERC1155 mint + storage updates

        // Total gas = calldata + execution + overhead
        uint256 totalGas = calldataGas + executionGas + 50000;

        // Estimate fee with 20% buffer
        estimatedGasFee = (totalGas * l1BaseFee * 12) / 10;

        return estimatedGasFee;
    }

    /* -------------------------------------------------------------------------- */
    /*                          Verification Functions                            */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Verify STARK proof
     * @param proof STARK proof bytes
     * @param publicInputs Public inputs array
     * @return valid True if proof is valid
     */
    function verifyStarkProof(bytes calldata proof, bytes32[] calldata publicInputs)
        internal
        view
        returns (bool valid)
    {
        // Call the STARK verifier contract
        (bool success, bytes memory result) = verifier.staticcall(
            abi.encodeWithSignature("verify(bytes,bytes32[])", proof, publicInputs)
        );

        if (!success) {
            return false;
        }

        return abi.decode(result, (bool));
    }

    /**
     * @dev Verify Eldernode consensus
     * @param commitment Commitment from proof
     * @param eldernodeProof Eldernode consensus proof
     * @return valid True if consensus is reached
     */
    function verifyEldernodeConsensus(bytes32 commitment, bytes calldata eldernodeProof)
        internal
        view
        returns (bool valid)
    {
        if (address(eldernodeVerifier) == address(0)) {
            return false;
        }

        return eldernodeVerifier.verifyConsensus(commitment, eldernodeProof, MIN_ELDERNODE_CONSENSUS);
    }

    /**
     * @dev Derive deposit transaction hash from commitment
     * @param commitment Commitment hash
     * @return txHash Derived transaction hash
     */
    function depositTxHashFromCommitment(bytes32 commitment) internal pure returns (bytes32 txHash) {
        return keccak256(abi.encodePacked("COLD_DEPOSIT:", commitment));
    }

    /* -------------------------------------------------------------------------- */
    /*                          Admin Functions                                   */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Update Eldernode verifier contract
     * @param newVerifier New verifier contract address
     */
    function updateEldernodeVerifier(address newVerifier) external onlyOwner {
        eldernodeVerifier = IEldernodeVerifier(newVerifier);
    }

    /**
     * @dev Update COLDAO governor contract
     * @param newGovernor New governor contract address
     */
    function updateCOLDAOGovernor(address newGovernor) external onlyOwner {
        require(newGovernor != address(0), "Invalid governor address");
        coldaoGovernor = ICOLDAOGovernor(newGovernor);
    }

    /**
     * @dev Toggle Eldernode verification requirement
     * @param required True to require Eldernode verification
     */
    function setEldernodeVerificationRequired(bool required) external onlyOwner {
        eldernodeVerificationRequired = required;
    }

    /**
     * @dev Pause the contract (emergency use only)
     */
    function pause() external onlyOwner {
        _pause();
    }

    /**
     * @dev Unpause the contract
     */
    function unpause() external onlyOwner {
        _unpause();
    }

    /**
     * @dev Rescue accidentally sent ETH
     */
    function rescueETH() external onlyOwner {
        payable(owner()).transfer(address(this).balance);
    }

    /* -------------------------------------------------------------------------- */
    /*                          View Functions                                    */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Check if nullifier has been used
     * @param nullifier Nullifier to check
     * @return used True if nullifier has been used
     */
    function isNullifierUsed(bytes32 nullifier) external view returns (bool used) {
        return nullifiersUsed[nullifier];
    }

    /**
     * @dev Get total XFG locked in human-readable format
     * @return xfgLocked Total XFG locked (with 7 decimal places)
     */
    function getTotalXFGLockedReadable() external view returns (uint256 xfgLocked) {
        // XFG has 7 decimal places, so divide by 10^7
        return totalXFGPrincipalLocked / 10_000_000;
    }

    /**
     * @dev Get contract statistics
     * @return stats Array of statistics
     */
    function getStatistics() external view returns (uint256[6] memory stats) {
        stats[0] = totalProofsVerified;
        stats[1] = totalCDInterestMinted;
        stats[2] = totalXFGPrincipalLocked;
        stats[3] = totalClaims;
        stats[4] = totalEldernodeVerifications;
        stats[5] = totalEldernodeFailures;
    }

    /* -------------------------------------------------------------------------- */
    /*                          Receive Function                                  */
    /* -------------------------------------------------------------------------- */

    /**
     * @dev Receive function to accept ETH for L1 gas fees
     */
    receive() external payable {}

} /** winter is coming */
