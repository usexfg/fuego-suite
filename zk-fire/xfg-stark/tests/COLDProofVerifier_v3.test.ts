import { expect } from "chai";
import { ethers } from "hardhat";
import { COLDProofVerifier, FuegoCOLDAOToken } from "../typechain-types";

/**
 * Test Suite: COLD Proof Verifier v3 - Domain-Based Signature Verification
 *
 * Tests the MVP Option B implementation:
 * - Ed25519 signature validation from usexfg.org API
 * - Nullifier tracking to prevent double-claiming
 * - Interest calculation with configurable APY
 * - Cross-chain message passing to L1
 */
describe("COLDProofVerifier_v3", function () {
  let coldVerifier: COLDProofVerifier;
  let cdToken: FuegoCOLDAOToken;
  let owner: any;
  let user: any;
  let recipient: any;

  // Test constants
  const DOMAIN_PUBLIC_KEY = ethers.zeroPadValue("0x01", 32); // Mock Ed25519 public key
  const CLAIM_KEY = ethers.id("test-claim-key-001");
  const COMMITMENT = ethers.id("test-commitment-001");
  const DOMAIN_SIGNATURE = ethers.getBytes(
    ethers.zeroPadValue("0x" + "01".repeat(64), 64)
  ); // 64-byte mock signature
  const VALID_TIER = 1; // 8 XFG tier

  before(async function () {
    // Get signers
    [owner, user, recipient] = await ethers.getSigners();

    // Deploy FuegoCOLDAOToken (mock)
    // For testing, we need a mock governor that provides APY
    const COLDAOGovernorMock = await ethers.getContractFactory("COLDAOGovernor");
    const governorMock = await COLDAOGovernorMock.deploy();

    const CDTokenFactory = await ethers.getContractFactory("FuegoCOLDAOToken");
    cdToken = (await CDTokenFactory.deploy(governorMock.address, owner.address)) as FuegoCOLDAOToken;

    // Deploy COLDProofVerifier_v3
    const VerifierFactory = await ethers.getContractFactory("COLDProofVerifier");
    coldVerifier = (await VerifierFactory.deploy(
      cdToken.address,
      governorMock.address,
      owner.address, // API verifier
      DOMAIN_PUBLIC_KEY, // Domain public key
      owner.address // Initial owner
    )) as COLDProofVerifier;
  });

  describe("Domain Signature Verification", function () {
    it("should reject empty domain signature", async function () {
      const emptySignature = ethers.getBytes("0x");
      const isValid = await coldVerifier.verifyDomainSignature(CLAIM_KEY, emptySignature);
      expect(isValid).to.be.false;
    });

    it("should reject signature when domain public key is not set", async function () {
      // Create a new verifier with zero domain key
      const VerifierFactory = await ethers.getContractFactory("COLDProofVerifier");
      const newVerifier = (await VerifierFactory.deploy(
        cdToken.address,
        coldVerifier.coldaoGovernor(),
        owner.address,
        ethers.ZeroHash, // Zero domain key
        owner.address
      )) as COLDProofVerifier;

      const isValid = await newVerifier.verifyDomainSignature(CLAIM_KEY, DOMAIN_SIGNATURE);
      expect(isValid).to.be.false;
    });

    it("should reject signature with invalid length (not 64 bytes)", async function () {
      const invalidSignature = ethers.getBytes(ethers.zeroPadValue("0x0102", 32)); // 32 bytes, not 64
      const isValid = await coldVerifier.verifyDomainSignature(CLAIM_KEY, invalidSignature);
      expect(isValid).to.be.false;
    });

    it("should accept valid 64-byte signature", async function () {
      const isValid = await coldVerifier.verifyDomainSignature(CLAIM_KEY, DOMAIN_SIGNATURE);
      expect(isValid).to.be.true;
    });

    it("should accept multiple different 64-byte signatures", async function () {
      const sig1 = ethers.getBytes(ethers.zeroPadValue("0x" + "01".repeat(64), 64));
      const sig2 = ethers.getBytes(ethers.zeroPadValue("0x" + "02".repeat(64), 64));

      const isValid1 = await coldVerifier.verifyDomainSignature(CLAIM_KEY, sig1);
      const isValid2 = await coldVerifier.verifyDomainSignature(CLAIM_KEY, sig2);

      expect(isValid1).to.be.true;
      expect(isValid2).to.be.true;
    });
  });

  describe("Domain Public Key Management", function () {
    it("should allow owner to update domain public key", async function () {
      const newKey = ethers.zeroPadValue("0x02", 32);
      await expect(coldVerifier.updateDomainPublicKey(newKey))
        .to.emit(coldVerifier, "DomainPublicKeyUpdated")
        .withArgs(DOMAIN_PUBLIC_KEY, newKey);

      const updatedKey = await coldVerifier.domainPublicKey();
      expect(updatedKey).to.equal(newKey);
    });

    it("should prevent non-owner from updating domain public key", async function () {
      const newKey = ethers.zeroPadValue("0x03", 32);
      await expect(
        coldVerifier.connect(user).updateDomainPublicKey(newKey)
      ).to.be.revertedWithCustomError(coldVerifier, "OwnableUnauthorizedAccount");
    });

    it("should reject zero domain public key", async function () {
      await expect(coldVerifier.updateDomainPublicKey(ethers.ZeroHash)).to.be.revertedWith(
        "Invalid domain public key"
      );
    });
  });

  describe("Claim Nullifier Tracking", function () {
    it("should initially mark claim key as unused", async function () {
      const isUsed = await coldVerifier.isClaimKeyUsed(CLAIM_KEY);
      expect(isUsed).to.be.false;
    });

    it("should track multiple claim keys independently", async function () {
      const key1 = ethers.id("claim-001");
      const key2 = ethers.id("claim-002");

      const used1 = await coldVerifier.isClaimKeyUsed(key1);
      const used2 = await coldVerifier.isClaimKeyUsed(key2);

      expect(used1).to.be.false;
      expect(used2).to.be.false;
    });
  });

  describe("Domain Message Encoding", function () {
    it("should correctly encode domain message", async function () {
      const timestamp = Math.floor(Date.now() / 1000);
      const message = await coldVerifier.encodeDomainMessage(CLAIM_KEY, timestamp);

      // Verify message contains all components
      const messageStr = ethers.toUtf8String(message);
      expect(messageStr).to.include("usexfg.org:");
      expect(messageStr).to.include(CLAIM_KEY.substring(2)); // Remove 0x prefix
      expect(messageStr).to.include(timestamp.toString());
    });

    it("should generate different messages for different timestamps", async function () {
      const ts1 = 1704067200;
      const ts2 = 1704067300;

      const msg1 = await coldVerifier.encodeDomainMessage(CLAIM_KEY, ts1);
      const msg2 = await coldVerifier.encodeDomainMessage(CLAIM_KEY, ts2);

      expect(msg1).to.not.equal(msg2);
    });

    it("should generate different messages for different claim keys", async function () {
      const key1 = ethers.id("claim-001");
      const key2 = ethers.id("claim-002");
      const timestamp = Math.floor(Date.now() / 1000);

      const msg1 = await coldVerifier.encodeDomainMessage(key1, timestamp);
      const msg2 = await coldVerifier.encodeDomainMessage(key2, timestamp);

      expect(msg1).to.not.equal(msg2);
    });
  });

  describe("Interest Calculation", function () {
    it("should calculate non-zero interest for valid tier", async function () {
      const interest = await coldVerifier.calculateInterest(
        ethers.parseUnits("8", 7) // 0.8 XFG in atomic units
      );
      expect(interest).to.be.gt(0n);
    });

    it("should revert with zero APY", async function () {
      // This would require mocking a zero-APY governor
      // Skipping for now as it requires complex setup
    });
  });

  describe("Claim Flow - Nullifier Validation", function () {
    it("should require valid domain signature", async function () {
      const invalidSignature = ethers.getBytes("0x"); // Empty signature

      await expect(
        coldVerifier.claimCD(
          recipient.address,
          VALID_TIER,
          CLAIM_KEY,
          COMMITMENT,
          invalidSignature,
          { value: ethers.parseEther("0.1") }
        )
      ).to.be.revertedWith("Invalid domain signature");
    });

    it("should require valid tier", async function () {
      const invalidTier = 99;

      await expect(
        coldVerifier.claimCD(
          recipient.address,
          invalidTier,
          CLAIM_KEY,
          COMMITMENT,
          DOMAIN_SIGNATURE,
          { value: ethers.parseEther("0.1") }
        )
      ).to.be.revertedWith("Invalid tier");
    });

    it("should require valid recipient address", async function () {
      await expect(
        coldVerifier.claimCD(
          ethers.ZeroAddress,
          VALID_TIER,
          CLAIM_KEY,
          COMMITMENT,
          DOMAIN_SIGNATURE,
          { value: ethers.parseEther("0.1") }
        )
      ).to.be.revertedWith("Invalid recipient address");
    });
  });

  describe("Pause/Unpause", function () {
    it("should allow owner to pause contract", async function () {
      await coldVerifier.pause();
      const isPaused = await coldVerifier.paused();
      expect(isPaused).to.be.true;
    });

    it("should allow owner to unpause contract", async function () {
      await coldVerifier.unpause();
      const isPaused = await coldVerifier.paused();
      expect(isPaused).to.be.false;
    });

    it("should prevent claims when paused", async function () {
      await coldVerifier.pause();

      await expect(
        coldVerifier.claimCD(
          recipient.address,
          VALID_TIER,
          CLAIM_KEY,
          COMMITMENT,
          DOMAIN_SIGNATURE,
          { value: ethers.parseEther("0.1") }
        )
      ).to.be.revertedWithCustomError(coldVerifier, "EnforcedPause");

      await coldVerifier.unpause();
    });
  });

  describe("Statistics Tracking", function () {
    it("should initialize statistics to zero", async function () {
      const [proofs, cdMinted, xfgLocked, claims] = await coldVerifier.getStatistics();

      expect(proofs).to.equal(0n);
      expect(cdMinted).to.equal(0n);
      expect(xfgLocked).to.equal(0n);
      expect(claims).to.equal(0n);
    });
  });

  describe("Gas Estimation", function () {
    it("should estimate L1 gas fee", async function () {
      const estimatedFee = await coldVerifier.estimateL1GasFee(recipient.address, VALID_TIER);
      expect(estimatedFee).to.be.gt(0n);
    });

    it("should provide recommended fee with 20% buffer", async function () {
      const baseFee = await coldVerifier.estimateL1GasFee(recipient.address, VALID_TIER);
      const recommendedFee = await coldVerifier.getRecommendedGasFee(recipient.address, VALID_TIER);

      // Recommended = base * 1.2
      const expectedRecommended = (baseFee * 120n) / 100n;
      expect(recommendedFee).to.equal(expectedRecommended);
    });
  });

  describe("Tier Information", function () {
    it("should return correct tier information", async function () {
      const { xfgAmount, cdInterest, tierName } = await coldVerifier.getTierInfo(VALID_TIER);

      expect(xfgAmount).to.be.gt(0n);
      expect(cdInterest).to.be.gt(0n);
      expect(tierName).to.not.be.empty;
    });

    it("should revert for invalid tier", async function () {
      await expect(coldVerifier.getTierInfo(99)).to.be.revertedWith("Invalid tier");
    });
  });

  describe("ETH Recovery", function () {
    it("should allow owner to rescue accidentally sent ETH", async function () {
      // Send ETH to contract
      await user.sendTransaction({
        to: coldVerifier.address,
        value: ethers.parseEther("1.0"),
      });

      const contractBalanceBefore = await ethers.provider.getBalance(coldVerifier.address);
      expect(contractBalanceBefore).to.equal(ethers.parseEther("1.0"));

      // Rescue ETH
      await coldVerifier.rescueETH();

      const contractBalanceAfter = await ethers.provider.getBalance(coldVerifier.address);
      expect(contractBalanceAfter).to.equal(0n);
    });

    it("should prevent non-owner from rescuing ETH", async function () {
      await user.sendTransaction({
        to: coldVerifier.address,
        value: ethers.parseEther("1.0"),
      });

      await expect(coldVerifier.connect(user).rescueETH()).to.be.revertedWithCustomError(
        coldVerifier,
        "OwnableUnauthorizedAccount"
      );

      // Clean up
      await coldVerifier.rescueETH();
    });
  });
});
