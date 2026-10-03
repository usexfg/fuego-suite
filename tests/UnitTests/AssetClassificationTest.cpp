// Asset classification for commitment terms, and the per-asset conservation
// invariant that depends on it.
//
// The defect under test: Blockchain::classifyInputAsset used to read the asset
// from commitmentOutputs()[amount][outputIndexes[0]] -- the first ring member.
// Ring member order is attacker-chosen, and a ring signature hides which member
// is real, so reordering decoys changed the asset an input was credited. A mixed
// ring let an input be credited under the wrong asset, breaking per-asset
// conservation at the v11 HEAT/Hearth activation.
//
// The table now also REJECTS unrecognised terms instead of defaulting them:
//   - term 0 had no producer and classified HEAT on the output side but XFG on
//     the input side;
//   - DIGM_TERM has no sound per-asset accounting (DigmMintEngine sums
//     commitment inputs as HEAT without knowing the real ring member's asset).
//
// Cases are deterministic: fixed terms, no randomness.

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteCore/AssetType.h"
#include "CryptoNoteConfig.h"

using namespace CryptoNote;

namespace {

// Mainnet CD bounds (Currency.cpp: depositMinTerm/depositMaxTerm).
constexpr uint32_t kCdMinTerm = 6 * 900;    // 5400
constexpr uint32_t kCdMaxTerm = 64 * 900;   // 64800

// Testnet bounds differ, which is why they are parameters.
constexpr uint32_t kTestnetCdMinTerm = 10;
constexpr uint32_t kTestnetCdMaxTerm = 720;

AssetType classify(uint32_t term, uint32_t minT = kCdMinTerm, uint32_t maxT = kCdMaxTerm) {
  AssetType asset = AssetType::XFG;
  EXPECT_TRUE(classifyCommitmentTermAsset(term, minT, maxT, asset))
      << "term 0x" << std::hex << term << std::dec << " should be recognised";
  return asset;
}

} // namespace

// The marker terms each carry a distinct, documented asset.
TEST(CommitmentTermAsset, MarkerTerms) {
  EXPECT_EQ(classify(parameters::HEAT_TERM), AssetType::HEAT);
  EXPECT_EQ(classify(parameters::DEPOSIT_TERM_LP), AssetType::LP);
  EXPECT_EQ(classify(parameters::DEPOSIT_TERM_POOL_XFG), AssetType::XFG);
  EXPECT_EQ(classify(parameters::DEPOSIT_TERM_POOL_HEAT), AssetType::HEAT);
  EXPECT_EQ(classify(parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG), AssetType::XFG);
}

// Any term inside the CD range is a certificate of deposit, which is
// HEAT-denominated. The accrual, CD_APY_POOL, BONUS_VAULT and m_feePoolBalance are
// all HEAT, so classifying a CD as XFG made inAssets.heat != outAssets.heat and
// consensus rejected every HEAT CD.
TEST(CommitmentTermAsset, CdRangeIsHeat) {
  const std::vector<uint32_t> cdTerms = {
    kCdMinTerm,
    kCdMinTerm + 1,
    (kCdMinTerm + kCdMaxTerm) / 2,
    kCdMaxTerm,
  };
  for (uint32_t term : cdTerms) {
    EXPECT_EQ(classify(term), AssetType::HEAT) << "term " << term;
  }
}

// term 0 must be REJECTED. It had no producer, and it classified HEAT on the
// output side but XFG on the input side, so any output of that term broke
// conservation. The unlocked-commitment case is already HEAT_TERM.
TEST(CommitmentTermAsset, ZeroTermIsRejected) {
  AssetType asset = AssetType::XFG;
  EXPECT_FALSE(classifyCommitmentTermAsset(0, kCdMinTerm, kCdMaxTerm, asset));
}

// DIGM_TERM must be REJECTED: DigmMintEngine sums commitment inputs as HEAT
// without knowing the real ring member's asset, so it cannot be accounted for.
TEST(CommitmentTermAsset, DigmTermIsRejected) {
  AssetType asset = AssetType::XFG;
  EXPECT_FALSE(classifyCommitmentTermAsset(parameters::DIGM_TERM, kCdMinTerm, kCdMaxTerm, asset));
}

// Terms below the CD range are not CDs and are not any other class.
TEST(CommitmentTermAsset, BelowCdRangeIsRejected) {
  AssetType asset = AssetType::XFG;
  for (uint32_t term = 1; term < kCdMinTerm; term += 977) {
    EXPECT_FALSE(classifyCommitmentTermAsset(term, kCdMinTerm, kCdMaxTerm, asset))
        << "term " << term;
  }
}

// The CD range is runtime, not a constant: the same term can be a valid CD on
// testnet and invalid on mainnet.
TEST(CommitmentTermAsset, CdRangeFollowsCallerBounds) {
  const uint32_t term = 500;  // between testnet max (720)? no -- below it
  AssetType asset = AssetType::XFG;
  // 500 is inside [10, 720] -> a valid testnet CD
  EXPECT_TRUE(classifyCommitmentTermAsset(term, kTestnetCdMinTerm, kTestnetCdMaxTerm, asset));
  EXPECT_EQ(asset, AssetType::HEAT);
  // 500 is below mainnet min (5400) -> not a valid mainnet CD
  EXPECT_FALSE(classifyCommitmentTermAsset(term, kCdMinTerm, kCdMaxTerm, asset));

  // A term above testnet max but below mainnet min is rejected on both.
  const uint32_t mid = 1000;
  EXPECT_FALSE(classifyCommitmentTermAsset(mid, kTestnetCdMinTerm, kTestnetCdMaxTerm, asset));
  EXPECT_FALSE(classifyCommitmentTermAsset(mid, kCdMinTerm, kCdMaxTerm, asset));
}

// A rejected term must not overwrite the caller's output. Callers rely on this to
// fall back safely.
TEST(CommitmentTermAsset, RejectionLeavesOutputUntouched) {
  AssetType asset = AssetType::LP;
  EXPECT_FALSE(classifyCommitmentTermAsset(0, kCdMinTerm, kCdMaxTerm, asset));
  EXPECT_EQ(asset, AssetType::LP) << "rejection must not write to outAsset";
}

// The invariant the mixed-ring fix relies on: within one asset class, every
// member classifies the same regardless of order, while a mixed ring always
// contains members that disagree so the all-members check can reject it.
TEST(CommitmentTermAsset, RingPermutationCannotChangeAsset) {
  const std::vector<uint32_t> heatRing = {
    parameters::HEAT_TERM, kCdMinTerm, parameters::DEPOSIT_TERM_POOL_HEAT,
  };
  for (size_t i = 0; i < heatRing.size(); ++i) {
    for (size_t j = 0; j < heatRing.size(); ++j) {
      EXPECT_EQ(classify(heatRing[i]), classify(heatRing[j]));
    }
  }

  // A mixed ring must contain a disagreement, otherwise the all-members-same-asset
  // check would accept it.
  const std::vector<uint32_t> mixedRing = {
    parameters::HEAT_TERM, parameters::DEPOSIT_TERM_LP,
  };
  EXPECT_NE(classify(mixedRing[0]), classify(mixedRing[1]));
}

// An unrecognised member makes the whole ring unresolvable -- which is what stops
// an attacker hiding a foreign term behind a valid one.
TEST(CommitmentTermAsset, ForeignTermMakesRingUnresolvable) {
  AssetType asset = AssetType::XFG;
  EXPECT_TRUE(classifyCommitmentTermAsset(parameters::HEAT_TERM, kCdMinTerm, kCdMaxTerm, asset));
  EXPECT_FALSE(classifyCommitmentTermAsset(0, kCdMinTerm, kCdMaxTerm, asset));
  EXPECT_FALSE(classifyCommitmentTermAsset(parameters::DIGM_TERM, kCdMinTerm, kCdMaxTerm, asset));
}

// ---------------------------------------------------------------------------
// The all-members-same-asset rule (resolveCommitmentRingAsset).
//
// This is the invariant that replaced "take the asset of ring member 0".
// ---------------------------------------------------------------------------

// The defect: the asset came from ring member 0, and member order is
// attacker-chosen. These cases pin that the result no longer depends on it.
TEST(CommitmentRingAsset, MixedRingIsUnresolvable) {
  // HEAT member first, LP member later -- exactly the reordering that used to
  // decide the credited asset.
  std::vector<uint32_t> heatFirst = {parameters::HEAT_TERM, parameters::HEAT_TERM,
                                     parameters::DEPOSIT_TERM_LP};
  AssetType asset = AssetType::XFG;
  EXPECT_FALSE(resolveCommitmentRingAsset(heatFirst, 0, kCdMinTerm, kCdMaxTerm, asset));
}

// Reversing the ring must not change the outcome: either order is unresolvable.
TEST(CommitmentRingAsset, ReorderingDoesNotChangeOutcome) {
  std::vector<uint32_t> heatFirst = {parameters::HEAT_TERM, parameters::DEPOSIT_TERM_LP};
  std::vector<uint32_t> lpFirst = {parameters::DEPOSIT_TERM_LP, parameters::HEAT_TERM};

  AssetType a = AssetType::XFG;
  AssetType b = AssetType::XFG;
  EXPECT_FALSE(resolveCommitmentRingAsset(heatFirst, 0, kCdMinTerm, kCdMaxTerm, a));
  EXPECT_FALSE(resolveCommitmentRingAsset(lpFirst, 0, kCdMinTerm, kCdMaxTerm, b));
}

// A single-asset ring resolves, and every member position yields the same asset.
TEST(CommitmentRingAsset, SingleAssetRingResolves) {
  std::vector<uint32_t> heatRing = {parameters::HEAT_TERM, parameters::DEPOSIT_TERM_POOL_HEAT,
                                    kCdMinTerm};
  for (size_t i = 0; i < heatRing.size(); ++i) {
    AssetType asset = AssetType::XFG;
    ASSERT_TRUE(resolveCommitmentRingAsset(heatRing, i, kCdMinTerm, kCdMaxTerm, asset))
        << "member " << i;
    EXPECT_EQ(asset, AssetType::HEAT) << "member " << i;
  }

  std::vector<uint32_t> xfgRing = {parameters::DEPOSIT_TERM_POOL_XFG,
                                   parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG};
  for (size_t i = 0; i < xfgRing.size(); ++i) {
    AssetType asset = AssetType::XFG;
    ASSERT_TRUE(resolveCommitmentRingAsset(xfgRing, i, kCdMinTerm, kCdMaxTerm, asset));
    EXPECT_EQ(asset, AssetType::XFG) << "member " << i;
  }

  std::vector<uint32_t> lpRing = {parameters::DEPOSIT_TERM_LP, parameters::DEPOSIT_TERM_LP};
  for (size_t i = 0; i < lpRing.size(); ++i) {
    AssetType asset = AssetType::XFG;
    ASSERT_TRUE(resolveCommitmentRingAsset(lpRing, i, kCdMinTerm, kCdMaxTerm, asset));
    EXPECT_EQ(asset, AssetType::LP) << "member " << i;
  }
}

// A single HEAT_TERM member resolves to HEAT -- the case that must keep working,
// or every HEAT withdrawal breaks.
TEST(CommitmentRingAsset, SingleHeatMemberResolves) {
  std::vector<uint32_t> ring = {parameters::HEAT_TERM};
  AssetType asset = AssetType::XFG;
  ASSERT_TRUE(resolveCommitmentRingAsset(ring, 0, kCdMinTerm, kCdMaxTerm, asset));
  EXPECT_EQ(asset, AssetType::HEAT);
}

// An unrecognised term anywhere makes the ring unresolvable, so an attacker cannot
// hide a foreign term behind valid ones.
TEST(CommitmentRingAsset, UnrecognisedTermAnywhereIsUnresolvable) {
  const std::vector<uint32_t> badTerms = {0, parameters::DIGM_TERM, 1, kCdMinTerm - 1};
  for (uint32_t bad : badTerms) {
    for (size_t pos = 0; pos < 3; ++pos) {
      std::vector<uint32_t> ring = {parameters::HEAT_TERM, parameters::HEAT_TERM,
                                    parameters::HEAT_TERM};
      ring[pos] = bad;
      AssetType asset = AssetType::XFG;
      EXPECT_FALSE(resolveCommitmentRingAsset(ring, 0, kCdMinTerm, kCdMaxTerm, asset))
          << "bad term " << bad << " at position " << pos;
    }
  }
}

// Malformed rings are rejected rather than read out of bounds.
TEST(CommitmentRingAsset, MalformedRingsAreRejected) {
  AssetType asset = AssetType::XFG;
  EXPECT_FALSE(resolveCommitmentRingAsset({}, 0, kCdMinTerm, kCdMaxTerm, asset));

  std::vector<uint32_t> ring = {parameters::HEAT_TERM, parameters::HEAT_TERM};
  EXPECT_FALSE(resolveCommitmentRingAsset(ring, 2, kCdMinTerm, kCdMaxTerm, asset));
  EXPECT_FALSE(resolveCommitmentRingAsset(ring, SIZE_MAX, kCdMinTerm, kCdMaxTerm, asset));
}

// The ring bounds are the caller's, so testnet and mainnet resolve differently.
TEST(CommitmentRingAsset, RingResolutionFollowsCallerBounds) {
  std::vector<uint32_t> ring = {500, 500};
  AssetType asset = AssetType::XFG;
  EXPECT_TRUE(resolveCommitmentRingAsset(ring, 0, kTestnetCdMinTerm, kTestnetCdMaxTerm, asset));
  EXPECT_EQ(asset, AssetType::HEAT);
  EXPECT_FALSE(resolveCommitmentRingAsset(ring, 0, kCdMinTerm, kCdMaxTerm, asset));
}

// Rejection must not write to the caller's output, so a caller can fall back
// without the failed attempt having corrupted its state.
TEST(CommitmentRingAsset, RejectionLeavesOutputUntouched) {
  AssetType asset = AssetType::LP;
  std::vector<uint32_t> ring = {parameters::HEAT_TERM, parameters::DEPOSIT_TERM_LP};
  EXPECT_FALSE(resolveCommitmentRingAsset(ring, 0, kCdMinTerm, kCdMaxTerm, asset));
  EXPECT_EQ(asset, AssetType::LP);
}
