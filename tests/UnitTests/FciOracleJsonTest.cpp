// Daemon-side contract for the file produced by tools/fci-oracle.
//
// MinerConfig::tryLoadOracleJson is a flat "key": number scanner, not a JSON
// parser. That is deliberate — it keeps the daemon dependency-free — but it
// means the oracle's output shape and the daemon's expectations have to be
// pinned together by a test. These cases feed the scanner the exact document
// the oracle writes, including the quoted metadata and the nested provenance
// block, and assert every basket key still lands as an integer number of cents.

#include <fstream>
#include <string>

#include "gtest/gtest.h"

#include <boost/program_options.hpp>

#include "CryptoNoteCore/MinerConfig.h"

using namespace CryptoNote;

namespace {

std::string writeOracle(const std::string& name, const std::string& body) {
  const std::string path = ::testing::TempDir() + name;
  std::ofstream out(path);
  out << body;
  out.close();
  return path;
}

MinerConfig loadOracle(const std::string& path) {
  MinerConfig config;
  config.fciOraclePath = path;
  boost::program_options::variables_map options;
  config.init(options);
  return config;
}

}  // namespace

TEST(FciOracleJsonTest, ParsesEveryBasketKeyIntoCents) {
  const auto path = writeOracle("fci_all_six.json", R"({
    "power": 0.196,
    "milk": 4.106,
    "bread": 4.0345,
    "eggs": 2.272,
    "gas": 4.631,
    "rice": 2.4648
  })");
  const auto config = loadOracle(path);

  EXPECT_EQ(0x3f, config.fciPresentMask);
  EXPECT_EQ(20u, config.fciPower);   // $0.196/kWh rounds to 20 cents
  EXPECT_EQ(411u, config.fciMilk);   // $4.106/gal rounds to 411 cents
  EXPECT_EQ(403u, config.fciBread);  // $4.0345/kg rounds to 403 cents
  EXPECT_EQ(227u, config.fciEggs);   // $2.272/dozen rounds to 227 cents
  EXPECT_EQ(463u, config.fciGas);    // $4.631/gal rounds to 463 cents
  EXPECT_EQ(246u, config.fciRice);   // $2.4648/kg rounds to 246 cents
}

// The oracle writes quoted metadata and a nested _provenance object alongside
// the numbers. A flat scanner must skip all of it, including provenance keys
// that start with a basket key ("bread_bls_value" must not be read as "bread").
TEST(FciOracleJsonTest, SkipsQuotedMetadataAndNestedProvenance) {
  const auto path = writeOracle("fci_oracle_shape.json", R"({
  "bread": 4.0345,
  "eggs": 2.2720,
  "gas": 4.6310,
  "milk": 4.1060,
  "power": 0.1960,
  "rice": 2.4648,
  "_latest_period": "2026-09",
  "_source": "BLS Public Data API v2",
  "_notes": {},
  "_provenance": {"bread_bls_period":"2026-09","bread_bls_series":"APU0000702111","bread_bls_value":1.83,"bread_unit":"USD/kg","eggs_bls_value":2.272,"headline_cpi_index":336.94,"headline_cpi_series":"CUUR0000SA0","rice_bls_value":1.118,"rice_unit":"USD/kg"}
}
)");
  const auto config = loadOracle(path);

  EXPECT_EQ(0x3f, config.fciPresentMask);
  EXPECT_EQ(403u, config.fciBread);
  EXPECT_EQ(246u, config.fciRice);
  EXPECT_EQ(20u, config.fciPower);
}

TEST(FciOracleJsonTest, LeavesAbsentKeysOutOfTheVote) {
  const auto path = writeOracle("fci_partial.json", R"({
    "power": 0.196,
    "gas": 4.631
  })");
  const auto config = loadOracle(path);

  EXPECT_EQ(0x11, config.fciPresentMask);
  EXPECT_EQ(20u, config.fciPower);
  EXPECT_EQ(463u, config.fciGas);
  EXPECT_EQ(0u, config.fciMilk);
  EXPECT_EQ(0u, config.fciRice);
}

// Zero is the "absent" sentinel on the wire, so a price that cannot be
// expressed as at least one cent must not set its mask bit.
TEST(FciOracleJsonTest, RejectsPricesThatCannotReachOneCent) {
  const auto path = writeOracle("fci_degenerate.json", R"({
    "power": 0.001,
    "milk": 0,
    "bread": -2.50,
    "gas": 4.631
  })");
  const auto config = loadOracle(path);

  EXPECT_EQ(0x10, config.fciPresentMask);
  EXPECT_EQ(0u, config.fciPower);
  EXPECT_EQ(0u, config.fciMilk);
  EXPECT_EQ(0u, config.fciBread);
  EXPECT_EQ(463u, config.fciGas);
}

TEST(FciOracleJsonTest, RoundsToTheNearestCent) {
  const auto path = writeOracle("fci_rounding.json", R"({
    "milk": 3.99,
    "bread": 3.999,
    "eggs": 2.004
  })");
  const auto config = loadOracle(path);

  EXPECT_EQ(399u, config.fciMilk);   // 399 is $3.99
  EXPECT_EQ(400u, config.fciBread);  // 399.9 rounds up
  EXPECT_EQ(200u, config.fciEggs);   // 200.4 rounds down
}

TEST(FciOracleJsonTest, MissingFileLeavesTheBasketEmpty) {
  MinerConfig config;
  config.fciOraclePath = ::testing::TempDir() + "fci_does_not_exist.json";
  boost::program_options::variables_map options;
  config.init(options);

  EXPECT_EQ(0, config.fciPresentMask);
}
