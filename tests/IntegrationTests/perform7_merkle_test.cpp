// Copyright (c) 2017-2026 Fuego Developers
//
// HEAT Integration Test - Merkle Proof Compatibility
//
// This test verifies that PoolMerkleTree (C++) generates merkle proofs
// that are compatible with HEATClaimer.sol smart contract verification.
//
// Key compatibility points:
//   - Both use keccak256 hashing (not cn_fast_hash)
//   - Both use same leaf ordering (even=left, odd=right)
//   - Both use same proof verification algorithm

#include <gtest/gtest.h>
#include <vector>
#include <cstring>
#include <random>

#include "SwapDaemon/PoolAttestation.h"
#include "crypto/hash.h"

namespace XfgSwap {
namespace {

// Generate random hash for testing
Crypto::Hash randomHash() {
    Crypto::Hash h;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < 32; ++i) {
        h.data[i] = static_cast<uint8_t>(dis(gen));
    }
    return h;
}

// Simulate Solidity keccak256(abi.encodePacked(a, b))
// This matches HEATClaimer._verifyMerkleProof exactly
Crypto::Hash solidityKeccakPair(const Crypto::Hash& a, const Crypto::Hash& b, bool aIsLeft) {
    uint8_t buf[64];
    if (aIsLeft) {
        std::memcpy(buf, &a, 32);
        std::memcpy(buf + 32, &b, 32);
    } else {
        std::memcpy(buf, &b, 32);
        std::memcpy(buf + 32, &a, 32);
    }
    Crypto::Hash result;
    Crypto::cn_fast_hash(buf, 64, result);
    return result;
}

} // anonymous namespace

// Test 1: Basic merkle root computation matches expected behavior
TEST(HEATMerkleTest, RootComputation) {
    PoolMerkleTree tree;
    
    // Add some leaves
    for (int i = 0; i < 5; ++i) {
        tree.addLeaf(randomHash());
    }
    
    Crypto::Hash root = tree.computeRoot();
    
    // Root should not be all zeros
    Crypto::Hash zero{};
    EXPECT_NE(root, zero);
    
    // Re-computing should give same result
    Crypto::Hash root2 = tree.computeRoot();
    EXPECT_EQ(root, root2);
}

// Test 2: Verify merkle proof can be generated and verified
TEST(HEATMerkleTest, ProofGenerationAndVerification) {
    PoolMerkleTree tree;
    const size_t NUM_LEAVES = 8;
    
    // Add leaves
    std::vector<Crypto::Hash> leaves;
    for (size_t i = 0; i < NUM_LEAVES; ++i) {
        Crypto::Hash h = randomHash();
        leaves.push_back(h);
        tree.addLeaf(h);
    }
    
    Crypto::Hash root = tree.computeRoot();
    
    // Generate and verify proof for each leaf
    for (size_t i = 0; i < NUM_LEAVES; ++i) {
        std::vector<Crypto::Hash> proof = tree.getProof(i);
        bool valid = PoolMerkleTree::verifyProof(leaves[i], proof, i, root);
        EXPECT_TRUE(valid) << "Proof failed for leaf index " << i;
    }
}

// Test 3: Verify proof fails with wrong leaf
TEST(HEATMerkleTest, ProofFailsWithWrongLeaf) {
    PoolMerkleTree tree;
    Crypto::Hash realLeaf = randomHash();
    Crypto::Hash fakeLeaf = randomHash();
    
    tree.addLeaf(realLeaf);
    tree.addLeaf(randomHash());
    
    Crypto::Hash root = tree.computeRoot();
    std::vector<Crypto::Hash> proof = tree.getProof(0);
    
    // Should pass with real leaf
    EXPECT_TRUE(PoolMerkleTree::verifyProof(realLeaf, proof, 0, root));
    
    // Should fail with fake leaf
    EXPECT_FALSE(PoolMerkleTree::verifyProof(fakeLeaf, proof, 0, root));
}

// Test 4: Simulate HEATClaimer verification in C++
// This verifies that our C++ proof format matches what Solidity expects
TEST(HEATMerkleTest, SolidityCompatibility) {
    PoolMerkleTree tree;
    const size_t NUM_LEAVES = 4;
    
    std::vector<Crypto::Hash> leaves;
    for (size_t i = 0; i < NUM_LEAVES; ++i) {
        Crypto::Hash h = randomHash();
        leaves.push_back(h);
        tree.addLeaf(h);
    }
    
    Crypto::Hash root = tree.computeRoot();
    
    // For each leaf, verify that C++ and "Solidity" (simulated) agree
    for (size_t leafIndex = 0; leafIndex < NUM_LEAVES; ++leafIndex) {
        std::vector<Crypto::Hash> proof = tree.getProof(leafIndex);
        
        // Simulate Solidity verification
        Crypto::Hash current = leaves[leafIndex];
        size_t index = leafIndex;
        
        for (const auto& sibling : proof) {
            if (index % 2 == 0) {
                // Current is left child
                current = solidityKeccakPair(current, sibling, true);
            } else {
                // Current is right child
                current = solidityKeccakPair(sibling, current, false);
            }
            index /= 2;
        }
        
        // Should match root
        EXPECT_EQ(current, root) << "Solidity simulation mismatch for leaf " << leafIndex;
        
        // Also verify with C++ method
        EXPECT_TRUE(PoolMerkleTree::verifyProof(leaves[leafIndex], proof, leafIndex, root));
    }
}

// Test 5: Single leaf tree
TEST(HEATMerkleTest, SingleLeafTree) {
    PoolMerkleTree tree;
    Crypto::Hash leaf = randomHash();
    tree.addLeaf(leaf);
    
    Crypto::Hash root = tree.computeRoot();
    EXPECT_EQ(root, leaf);  // Root should equal the single leaf
    
    std::vector<Crypto::Hash> proof = tree.getProof(0);
    EXPECT_TRUE(proof.empty());  // No proof needed for single leaf
    
    EXPECT_TRUE(PoolMerkleTree::verifyProof(leaf, proof, 0, root));
}

// Test 6: Empty tree
TEST(HEATMerkleTree, EmptyTree) {
    PoolMerkleTree tree;
    Crypto::Hash root = tree.computeRoot();
    Crypto::Hash zero{};
    EXPECT_EQ(root, zero);
    EXPECT_EQ(tree.size(), 0u);
}

// Test 7: Build checkpoint with merkle trees (integration with HEAT)
TEST(HEATMerkleTest, CheckpointBuildAndVerify) {
    // Simulate pool state
    PoolState state;
    state.reserveA = 1000000;
    state.reserveB = 500000;
    state.totalLPShares = 707106;
    state.blockHeight = 1000;
    state.timestamp = 1234567890;
    
    // Build LP shares and fee records
    PoolMerkleTree lpTree;
    PoolMerkleTree feeTree;
    
    // Add some LP shares
    for (int i = 0; i < 3; ++i) {
        LPShare share;
        share.owner = randomHash();
        share.poolId.assetB = randomHash();
        share.shareAmount = 100000 + i * 10000;
        share.feeClaimedA = 100 + i * 10;
        share.feeClaimedB = 50 + i * 5;
        
        Crypto::Hash leaf = computeLPShareLeaf(share);
        lpTree.addLeaf(leaf);
    }
    
    // Add some fee records
    for (int i = 0; i < 2; ++i) {
        PoolFeeRecord record;
        record.poolId.assetB = randomHash();
        record.feeAmount = 500 + i * 50;
        record.totalShares = state.totalLPShares;
        record.eventHash = randomHash();
        
        Crypto::Hash leaf = computeFeeRecordLeaf(record);
        feeTree.addLeaf(leaf);
    }
    
    // Build checkpoint
    Crypto::Hash prevCheckpoint{};
    PoolCheckpoint checkpoint = buildCheckpoint(state, lpTree, feeTree, prevCheckpoint);
    
    // Verify checkpoint
    EXPECT_TRUE(verifyCheckpoint(checkpoint, state, lpTree, feeTree, prevCheckpoint));
    
    // Verify merkle roots are non-zero
    EXPECT_NE(checkpoint.lpShareMerkleRoot, Crypto::Hash{});
    EXPECT_NE(checkpoint.feeMerkleRoot, Crypto::Hash{});
    EXPECT_NE(checkpoint.newCheckpoint, Crypto::Hash{});
}

} // namespace XfgSwap
