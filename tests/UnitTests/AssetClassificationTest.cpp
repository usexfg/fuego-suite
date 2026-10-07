// Commitment-term asset classification.
//
// An output is minted as one asset and the input that later spends it must present the
// same asset, or per-asset conservation can be broken. Currency::classifyCommitmentTermAsset
// is the single table both sides use; these cases pin that contract.
//
// What is NOT covered here: the rule that a commitment-spend ring may not mix asset
// classes. That lives in Blockchain::checkCommitmentSpendInput and needs a Blockchain
// with a populated commitment index to exercise; see
// docs/plans/2026-10-01-consensus-ring-verifier-harness-plan.md.

#include "gtest/gtest.h"

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/Currency.h"

using namespace CryptoNote;

namespace {

TransactionOutputTarget commitmentTarget(uint32_t term) {
  TransactionOutputCommitment out;
  memset(out.commitKey.data, 0x11, sizeof(out.commitKey.data));
  out.term = term;
  return out;
}

} // namespace

TEST(AssetClassification, ProtocolMarkerTermsMapToTheirAsset) {
  EXPECT_EQ(AssetType::HEAT, Currency::classifyCommitmentTermAsset(parameters::HEAT_TERM));
  EXPECT_EQ(AssetType::LP,   Currency::classifyCommitmentTermAsset(parameters::DEPOSIT_TERM_LP));
  EXPECT_EQ(AssetType::XFG,  Currency::classifyCommitmentTermAsset(parameters::DEPOSIT_TERM_POOL_XFG));
  EXPECT_EQ(AssetType::HEAT, Currency::classifyCommitmentTermAsset(parameters::DEPOSIT_TERM_POOL_HEAT));
  EXPECT_EQ(AssetType::XFG,  Currency::classifyCommitmentTermAsset(parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG));
}

// A certificate of deposit is HEAT. Classifying it as XFG made inAssets.heat != outAssets.heat
// and rejected every HEAT CD, so the finite terms are pinned across both networks' ranges.
TEST(AssetClassification, CertificatesOfDepositAreHeat) {
  const uint32_t cdTerms[] = {1, 10, 720, 5400, 64800};
  for (uint32_t term : cdTerms) {
    EXPECT_EQ(AssetType::HEAT, Currency::classifyCommitmentTermAsset(term)) << "term " << term;
  }
}

// Term 0 used to be HEAT on the output side and XFG on the input side, a 1:1 HEAT -> XFG
// conversion outside the pool. It is plain HEAT on both sides now.
TEST(AssetClassification, TermZeroIsPlainHeatOnBothSides) {
  EXPECT_EQ(AssetType::HEAT, Currency::classifyCommitmentTermAsset(0));
  EXPECT_EQ(AssetType::HEAT, Currency::classifyOutputAsset(commitmentTarget(0), 0));
}

// The output-side classifier must not carry its own copy of the table.
TEST(AssetClassification, OutputSideUsesTheSharedTable) {
  const uint32_t terms[] = {
    0, 1, 720, 5400, 64800,
    parameters::HEAT_TERM,
    parameters::DEPOSIT_TERM_LP,
    parameters::DEPOSIT_TERM_POOL_XFG,
    parameters::DEPOSIT_TERM_POOL_HEAT,
    parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG,
  };
  for (uint32_t term : terms) {
    EXPECT_EQ(Currency::classifyCommitmentTermAsset(term),
              Currency::classifyOutputAsset(commitmentTarget(term), term))
      << "term " << term << ": input and output sides disagree";
  }
}

TEST(AssetClassification, OrdinaryOutputsAreXfg) {
  KeyOutput key;
  memset(key.key.data, 0x22, sizeof(key.key.data));
  EXPECT_EQ(AssetType::XFG, Currency::classifyOutputAsset(TransactionOutputTarget(key), 0));

  MultisignatureOutput multisig;
  EXPECT_EQ(AssetType::XFG, Currency::classifyOutputAsset(TransactionOutputTarget(multisig), 0));
}
