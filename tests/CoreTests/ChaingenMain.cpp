// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2014-2016 SDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "Chaingen.h"

#include "Common/CommandLine.h"

#include <boost/filesystem.hpp>

#include "BlockReward.h"
#include "BlockValidation.h"
#include "ChainSplit1.h"
#include "ChainSwitch1.h"
#include "Chaingen001.h"
#include "DoubleSpend.h"
#include "IntegerOverflow.h"
#include "RingSignature.h"
#include "TransactionTests.h"
#include "TransactionValidation.h"
#include "Upgrade.h"
#include "RandomOuts.h"
#include "Deposit.h"

namespace po = boost::program_options;

namespace
{
  const command_line::arg_descriptor<std::string> arg_test_data_path              = {"test_data_path", "", ""};
  const command_line::arg_descriptor<bool>        arg_generate_test_data          = {"generate_test_data", ""};
  const command_line::arg_descriptor<bool>        arg_play_test_data              = {"play_test_data", ""};
  const command_line::arg_descriptor<bool>        arg_generate_and_play_test_data = {"generate_and_play_test_data", ""};
  const command_line::arg_descriptor<bool>        arg_test_transactions           = {"test_transactions", ""};
  const command_line::arg_descriptor<std::string> arg_filter                      = {"filter", "Run only scenarios whose name contains this substring", ""};
  const command_line::arg_descriptor<bool>        arg_include_slow                = {"include-slow", "Also run scenarios that take tens of minutes"};
  const command_line::arg_descriptor<std::string> arg_data_dir                    = {"data-dir", "Directory for the replay core's chain files (default: <temp>/fuego-chaingen)", ""};
}

int main(int argc, char* argv[])
{
  try {

  po::options_description desc_options("Allowed options");
  command_line::add_arg(desc_options, command_line::arg_help);
  command_line::add_arg(desc_options, arg_test_data_path);
  command_line::add_arg(desc_options, arg_generate_test_data);
  command_line::add_arg(desc_options, arg_play_test_data);
  command_line::add_arg(desc_options, arg_generate_and_play_test_data);
  command_line::add_arg(desc_options, arg_test_transactions);
  command_line::add_arg(desc_options, arg_filter);
  command_line::add_arg(desc_options, arg_data_dir);
  command_line::add_arg(desc_options, arg_include_slow);

  po::variables_map vm;
  bool r = command_line::handle_error_helper(desc_options, [&]()
  {
    po::store(po::parse_command_line(argc, argv, desc_options), vm);
    po::notify(vm);
    return true;
  });
  if (!r)
    return 1;

  if (command_line::get_arg(vm, command_line::arg_help))
  {
    std::cout << desc_options << std::endl;
    return 0;
  }

  chaingenFilter() = command_line::get_arg(vm, arg_filter);
  const bool includeSlow = command_line::get_arg(vm, arg_include_slow);
  chaingenDataDir() = command_line::get_arg(vm, arg_data_dir);
  if (chaingenDataDir().empty()) {
    chaingenDataDir() = (boost::filesystem::temp_directory_path() / "fuego-chaingen").string();
  }

  size_t tests_count = 0;
  std::vector<std::string> failed_tests;
  std::string tests_folder = command_line::get_arg(vm, arg_test_data_path);
  if (command_line::get_arg(vm, arg_generate_test_data))
  {
    GENERATE("chain001.dat", gen_simple_chain_001);
  }
  else if (command_line::get_arg(vm, arg_play_test_data))
  {
    PLAY("chain001.dat", gen_simple_chain_001);
  }
  else if (command_line::get_arg(vm, arg_generate_and_play_test_data))
  {
#define GENERATE_AND_PLAY_EX_2VER(TestCase) \
  GENERATE_AND_PLAY_EX(TestCase(CryptoNote::BLOCK_MAJOR_VERSION_1)) \
  GENERATE_AND_PLAY_EX(TestCase(CryptoNote::BLOCK_MAJOR_VERSION_2))

    GENERATE_AND_PLAY(DepositTests::TransactionWithDepositExtendsTotalDeposit);
    GENERATE_AND_PLAY(DepositTests::TransactionWithMultipleDepositOutsExtendsTotalDeposit);
    GENERATE_AND_PLAY(DepositTests::TransactionWithDepositUpdatesInterestAfterDepositUnlock);
    GENERATE_AND_PLAY(DepositTests::TransactionWithDepositIsClearedAfterInputSpend);
    GENERATE_AND_PLAY(DepositTests::TransactionWithDepositUpdatesInterestAfterDepositUnlockMultiple);

    GENERATE_AND_PLAY(DepositTests::BlocksOfFirstTypeCantHaveTransactionsOfTypeTwo);
    GENERATE_AND_PLAY(DepositTests::BlocksOfSecondTypeCanHaveTransactionsOfTypeOne);
    GENERATE_AND_PLAY(DepositTests::BlocksOfSecondTypeCanHaveTransactionsOfTypeTwo);
    GENERATE_AND_PLAY(DepositTests::TransactionOfTypeOneWithDepositInputIsRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionOfTypeOneWithDepositOutputIsRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithMinAmountIsAccepted);
    GENERATE_AND_PLAY(DepositTests::TransactionWithMinTermIsAccepted);
    GENERATE_AND_PLAY(DepositTests::TransactionWithMaxTermIsAccepted);
    GENERATE_AND_PLAY(DepositTests::TransactionWithoutSignaturesIsRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithZeroRequiredSignaturesIsRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithNumberOfRequiredSignaturesGreaterThanKeysIsRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithInvalidKeyIsRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithOutputToSpentInputWillBeRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithMultipleInputsThatSpendOneOutputWillBeRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithInputWithAmountThatIsDoesntHaveOutputWithSameAmountWillBeRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithInputWithIndexLargerThanNumberOfOutputsWithThisSumWillBeRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithInputThatPointsToTheOutputButHasAnotherTermWillBeRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionThatTriesToSpendOutputWhosTermHasntFinishedWillBeRejected);
    GENERATE_AND_PLAY(DepositTests::TransactionWithAmountThatHasAlreadyFinishedWillBeAccepted);
    GENERATE_AND_PLAY(DepositTests::TransactionWithDepositExtendsEmission);

    // Not run: each needs a reorg that drops a main-chain deposit tx, which
    // Fuego refuses (Blockchain::switch_to_alternative_blockchain missing-tx
    // rule, covered by gen_chain_switch_1 / gen_double_spend_in_different_chains).
    //GENERATE_AND_PLAY(DepositTests::TransactionWithDepositUnrolesPartOfAmountAfterSwitchToAlternativeChain);
    //GENERATE_AND_PLAY(DepositTests::TransactionWithDepositUnrolesInterestAfterSwitchToAlternativeChain);
    //GENERATE_AND_PLAY(DepositTests::TransactionWithDepositUnrolesAmountAfterSwitchToAlternativeChain);
    //GENERATE_AND_PLAY(DepositTests::TransactionWithDepositRestorsEmissionOnAlternativeChain);
    // Not run: legacy-deposit term/amount checks in Blockchain::check_tx_outputs
    // apply only from the hardcoded mainnet height 821000, which no test chain
    // reaches; enabling them needs that height as a currency parameter.
    //GENERATE_AND_PLAY(DepositTests::TransactionWithAmountLowerThenMinIsRejected);
    //GENERATE_AND_PLAY(DepositTests::TransactionWithTermLowerThenMinIsRejected);
    //GENERATE_AND_PLAY(DepositTests::TransactionWithTermGreaterThenMaxIsRejected);

    GENERATE_AND_PLAY(gen_simple_chain_001);
    GENERATE_AND_PLAY(gen_simple_chain_split_1);
    GENERATE_AND_PLAY(one_block);
    GENERATE_AND_PLAY(gen_chain_switch_1);
    GENERATE_AND_PLAY(gen_ring_signature_1);
    GENERATE_AND_PLAY(gen_ring_signature_2);
    //GENERATE_AND_PLAY(gen_ring_signature_big); // Takes up to XXX hours (if CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW == 10)

    //// Block verification tests
    GENERATE_AND_PLAY_EX_2VER(TestBlockMajorVersionAccepted);
    GENERATE_AND_PLAY_EX(TestBlockMajorVersionRejected(CryptoNote::BLOCK_MAJOR_VERSION_1, CryptoNote::BLOCK_MAJOR_VERSION_2));
    GENERATE_AND_PLAY_EX(TestBlockMajorVersionRejected(CryptoNote::BLOCK_MAJOR_VERSION_2, CryptoNote::BLOCK_MAJOR_VERSION_1));
    GENERATE_AND_PLAY_EX(TestBlockMajorVersionRejected(CryptoNote::BLOCK_MAJOR_VERSION_2, CryptoNote::BLOCK_MAJOR_VERSION_2 + 1));
    GENERATE_AND_PLAY_EX_2VER(TestBlockBigMinorVersion);
    GENERATE_AND_PLAY_EX_2VER(gen_block_ts_not_checked);
    GENERATE_AND_PLAY_EX_2VER(gen_block_ts_in_past);
    GENERATE_AND_PLAY_EX_2VER(gen_block_ts_in_future_rejected);
    GENERATE_AND_PLAY_EX_2VER(gen_block_ts_in_future_accepted);
    GENERATE_AND_PLAY_EX_2VER(gen_block_invalid_prev_id);
    GENERATE_AND_PLAY_EX_2VER(gen_block_invalid_nonce);
    GENERATE_AND_PLAY_EX_2VER(gen_block_no_miner_tx);
    GENERATE_AND_PLAY_EX_2VER(gen_block_unlock_time_is_low);
    GENERATE_AND_PLAY_EX_2VER(gen_block_unlock_time_is_high);
    GENERATE_AND_PLAY_EX_2VER(gen_block_unlock_time_is_timestamp_in_past);
    GENERATE_AND_PLAY_EX_2VER(gen_block_unlock_time_is_timestamp_in_future);
    GENERATE_AND_PLAY_EX_2VER(gen_block_height_is_low);
    GENERATE_AND_PLAY_EX_2VER(gen_block_height_is_high);
    GENERATE_AND_PLAY_EX_2VER(gen_block_miner_tx_has_2_tx_gen_in);
    GENERATE_AND_PLAY_EX_2VER(gen_block_miner_tx_has_2_in);
    GENERATE_AND_PLAY_EX_2VER(gen_block_miner_tx_with_txin_to_key);
    GENERATE_AND_PLAY_EX_2VER(gen_block_miner_tx_out_is_small);
    GENERATE_AND_PLAY_EX_2VER(gen_block_miner_tx_out_is_big);
    GENERATE_AND_PLAY_EX_2VER(gen_block_miner_tx_has_no_out);
    GENERATE_AND_PLAY_EX_2VER(gen_block_miner_tx_has_out_to_alice);
    GENERATE_AND_PLAY_EX_2VER(gen_block_has_invalid_tx);
    GENERATE_AND_PLAY_EX_2VER(gen_block_is_too_big);
    GENERATE_AND_PLAY_EX_2VER(TestBlockCumulativeSizeExceedsLimit);
    if (includeSlow) {
      // Mines to difficulty >= 1500 so no bit-flipped block passes PoW by
      // chance (a lower target makes it flaky); tens of minutes of CryptoNight.
      GENERATE_AND_PLAY_EX_2VER(gen_block_invalid_binary_format);
    }

    // Transaction verification tests
    GENERATE_AND_PLAY(gen_tx_big_version);
    GENERATE_AND_PLAY(gen_tx_unlock_time);
    GENERATE_AND_PLAY(gen_tx_no_inputs_no_outputs);
    GENERATE_AND_PLAY(gen_tx_no_inputs_has_outputs);
    GENERATE_AND_PLAY(gen_tx_has_inputs_no_outputs);
    GENERATE_AND_PLAY(gen_tx_invalid_input_amount);
    GENERATE_AND_PLAY(gen_tx_in_to_key_wo_key_offsets);
    GENERATE_AND_PLAY(gen_tx_sender_key_offest_not_exist);
    GENERATE_AND_PLAY(gen_tx_key_offest_points_to_foreign_key);
    GENERATE_AND_PLAY(gen_tx_mixed_key_offest_not_exist);
    GENERATE_AND_PLAY(gen_tx_key_image_not_derive_from_tx_key);
    GENERATE_AND_PLAY(gen_tx_key_image_is_invalid);
    GENERATE_AND_PLAY(gen_tx_check_input_unlock_time);
    GENERATE_AND_PLAY(gen_tx_txout_to_key_has_invalid_key);
    GENERATE_AND_PLAY(gen_tx_output_with_zero_amount);
    GENERATE_AND_PLAY(gen_tx_signatures_are_invalid);
    GENERATE_AND_PLAY_EX(GenerateTransactionWithZeroFee(false));
    GENERATE_AND_PLAY_EX(GenerateTransactionWithZeroFee(true));

    // multisignature output
    GENERATE_AND_PLAY_EX(MultiSigTx_OutputSignatures(1, 1, true));
    GENERATE_AND_PLAY_EX(MultiSigTx_OutputSignatures(2, 2, true));
    GENERATE_AND_PLAY_EX(MultiSigTx_OutputSignatures(3, 2, true));
    GENERATE_AND_PLAY_EX(MultiSigTx_OutputSignatures(0, 0, true));
    GENERATE_AND_PLAY_EX(MultiSigTx_OutputSignatures(1, 0, true));
    GENERATE_AND_PLAY_EX(MultiSigTx_OutputSignatures(0, 1, false));
    GENERATE_AND_PLAY_EX(MultiSigTx_OutputSignatures(1, 2, false));
    GENERATE_AND_PLAY_EX(MultiSigTx_OutputSignatures(2, 3, false));
    GENERATE_AND_PLAY_EX(MultiSigTx_InvalidOutputSignature());

    // multisignature input
    GENERATE_AND_PLAY_EX(MultiSigTx_Input(1, 1, 1, true));
    GENERATE_AND_PLAY_EX(MultiSigTx_Input(2, 1, 1, true));
    GENERATE_AND_PLAY_EX(MultiSigTx_Input(3, 2, 2, true));
    GENERATE_AND_PLAY_EX(MultiSigTx_Input(1, 1, 0, false));
    GENERATE_AND_PLAY_EX(MultiSigTx_Input(2, 2, 1, false));
    GENERATE_AND_PLAY_EX(MultiSigTx_Input(3, 2, 1, false));
    GENERATE_AND_PLAY_EX(MultiSigTx_BadInputSignature());

    // Double spend
    GENERATE_AND_PLAY(gen_double_spend_in_tx<false>);
    GENERATE_AND_PLAY(gen_double_spend_in_tx<true>);
    GENERATE_AND_PLAY(gen_double_spend_in_the_same_block<false>);
    GENERATE_AND_PLAY(gen_double_spend_in_the_same_block<true>);
    GENERATE_AND_PLAY(gen_double_spend_in_different_blocks<false>);
    GENERATE_AND_PLAY(gen_double_spend_in_different_blocks<true>);
    GENERATE_AND_PLAY(gen_double_spend_in_different_chains);
    GENERATE_AND_PLAY(gen_double_spend_in_alt_chain_in_the_same_block<false>);
    GENERATE_AND_PLAY(gen_double_spend_in_alt_chain_in_the_same_block<true>);
    GENERATE_AND_PLAY(gen_double_spend_in_alt_chain_in_different_blocks<false>);
    GENERATE_AND_PLAY(gen_double_spend_in_alt_chain_in_different_blocks<true>);

    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendInTx(false));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendInTx(true));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendSameBlock(false));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendSameBlock(true));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendDifferentBlocks(false));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendDifferentBlocks(true));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendAltChainSameBlock(false));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendAltChainSameBlock(true));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendAltChainDifferentBlocks(false));
    GENERATE_AND_PLAY_EX(MultiSigTx_DoubleSpendAltChainDifferentBlocks(true));

    GENERATE_AND_PLAY(gen_uint_overflow_1);
    GENERATE_AND_PLAY(gen_uint_overflow_2);

    GENERATE_AND_PLAY(gen_block_reward);
    GENERATE_AND_PLAY(gen_upgrade);
    GENERATE_AND_PLAY(GetRandomOutputs);

    std::cout << (failed_tests.empty() ? concolor::green : concolor::magenta);
    std::cout << "\nREPORT:\n";
    std::cout << "  Test run: " << tests_count << '\n';
    std::cout << "  Failures: " << failed_tests.size() << '\n';
    if (!failed_tests.empty())
    {
      std::cout << "FAILED TESTS:\n";
      BOOST_FOREACH(auto test_name, failed_tests)
      {
        std::cout << "  " << test_name << '\n';
      }
    }
    std::cout << concolor::normal << std::endl;

    if (tests_count == 0) {
      std::cout << "No scenario matched --filter \"" << chaingenFilter() << "\"" << std::endl;
      return 1;
    }
  }
  else if (command_line::get_arg(vm, arg_test_transactions))
  {
    CALL_TEST("TRANSACTIONS TESTS", test_transactions);
  }
  else
  {
    std::cout << concolor::magenta << "Wrong arguments" << concolor::normal << std::endl;
    std::cout << desc_options << std::endl;
    return 2;
  }

  return failed_tests.empty() ? 0 : 1;

  } catch (std::exception& e) {
    std::cout << "Exception in main(): " << e.what() << std::endl;
    return 1;
  }
}
