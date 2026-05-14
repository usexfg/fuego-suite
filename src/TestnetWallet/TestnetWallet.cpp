// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "../SimpleWallet/SimpleWallet.h"
#include "TestnetWallet.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <thread>
#include <set>
#include <sstream>
#include <regex>
#include <limits>

#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include "Common/Base58.h"
#include "Common/CommandLine.h"
#include "Common/SignalHandler.h"
#include "Common/StringTools.h"
#include "Common/PathTools.h"
#include "Common/Util.h"
#include "Common/DnsTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolHandler.h"
#include "NodeRpcProxy/NodeRpcProxy.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"
#include "Rpc/HttpClient.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/AliasIndex.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/DepositCommitment.h"

namespace CryptoNote
{
  //----------------------------------------------------------------------------------------------------
  // testnet_wallet constructor - extends simple_wallet with testnet-specific commands
  //----------------------------------------------------------------------------------------------------
  CryptoNote::testnet_wallet::testnet_wallet(System::Dispatcher& dispatcher, const CryptoNote::Currency& currency, Logging::LoggerManager& log) :
    simple_wallet(dispatcher, currency, log)
  {
    // Register testnet-specific deposit commands
    register_testnet_commands();
  }

  //----------------------------------------------------------------------------------------------------
  void CryptoNote::testnet_wallet::register_testnet_commands()
  {
    // Add testnet-specific deposit commands (in addition to inherited ones)
    m_consoleHandler.setHandler("burn", boost::bind(&testnet_wallet::burn, this, boost::arg<1>()), "burn <amount> - Create a HEAT burn (0.8, 8, 80, 800 TEST)");
    // m_consoleHandler.setHandler("cold", boost::bind(&testnet_wallet::cold, this, boost::arg<1>()), "cold <amount> <term_code> - Create a Certificate of Ledger Deposit (0.8, 8, 80, 800 TEST with terms 3 (3months) or 12 (1yr)");
    // m_consoleHandler.setHandler("elderking_ceremony", boost::bind(&testnet_wallet::elderking_ceremony, this, boost::arg<1>()), "elderking_ceremony - (DEPRECATED)");
    // m_consoleHandler.setHandler("unstake", boost::bind(&testnet_wallet::unstake, this, boost::arg<1>()), "unstake - (DEPRECATED)");
    m_consoleHandler.setHandler("list_burns", boost::bind(&testnet_wallet::list_burns, this, boost::arg<1>()), "list_burns - List all burn transactions.");

    // @ Alias system commands (inherited from simple_wallet)
    m_consoleHandler.setHandler("register_alias", boost::bind(&testnet_wallet::register_alias, this, boost::arg<1>()), "register_alias <alias> - Register a TEST alias (8 chars ONLY: [A-Z0-9] (CAPS-LOCK) req'd, [a-z0-9] (lowercase) for regular user wallets)");
    m_consoleHandler.setHandler("lookup_alias", boost::bind(&testnet_wallet::lookup_alias, this, boost::arg<1>()), "lookup_alias <alias_or_address> - Look up a TEST alias by name or wallet address");
    m_consoleHandler.setHandler("list_aliases", boost::bind(&testnet_wallet::list_aliases, this, boost::arg<1>()), "list_aliases - List all registered TEST aliases on the network");

    // Override swap command to pass --testnet flag to swapxfg
    m_consoleHandler.setHandler("swap", boost::bind(&testnet_wallet::swap_tui, this, boost::arg<1>()), "swap - Launch swapxfg testnet swap terminal");
  }

  //----------------------------------------------------------------------------------------------------
  // Testnet-specific command implementations
  //----------------------------------------------------------------------------------------------------

  bool CryptoNote::testnet_wallet::swap_tui(const std::vector<std::string>& /*args*/) {
    launchSwapxfg(true);
    return true;
  }

  //----------------------------------------------------------------------------------------------------
  // Testnet-specific deposit command implementations
  //----------------------------------------------------------------------------------------------------

  bool CryptoNote::testnet_wallet::burn(const std::vector<std::string> &args)
  {
    // HEAT burn deposit - testnet version
    if (args.size() != 1)
    {
      fail_msg_writer() << "Usage: burn <amount>";
      fail_msg_writer() << "Valid amounts: 0.08, 0.8, 8, 80 TEST";
      return true;
    }

    try
    {
      uint64_t burn_amount = 0;
      bool ok = m_currency.parseAmount(args[0], burn_amount);

      if (!ok || 0 == burn_amount)
      {
        fail_msg_writer() << "Invalid amount format: " << args[0];
        return true;
      }

      std::vector<uint64_t> valid_amounts = {
        CryptoNote::parameters::TEST_AMOUNT_TIER_0,
        CryptoNote::parameters::TEST_AMOUNT_TIER_1,
        CryptoNote::parameters::TEST_AMOUNT_TIER_2,
        CryptoNote::parameters::TEST_AMOUNT_TIER_3
      };

      auto it = std::find(valid_amounts.begin(), valid_amounts.end(), burn_amount);
      if (it == valid_amounts.end()) {
        fail_msg_writer() << "Invalid amount. Valid tiers: 0.08, 0.8, 8, 80 TEST";
        return true;
      }

      uint32_t burn_term = CryptoNote::parameters::DEPOSIT_TERM_FOREVER;

      // Determine banking fee based on amount tier (testnet rates: 0.1% of TEST_AMOUNT_TIER)
      uint64_t banking_fee = 0;
      if (burn_amount == CryptoNote::parameters::TEST_AMOUNT_TIER_0) {
        banking_fee = CryptoNote::parameters::TEST_BANK_FEE_TIER_0;
      } else if (burn_amount == CryptoNote::parameters::TEST_AMOUNT_TIER_1) {
        banking_fee = CryptoNote::parameters::TEST_BANK_FEE_TIER_1;
      } else if (burn_amount == CryptoNote::parameters::TEST_AMOUNT_TIER_2) {
        banking_fee = CryptoNote::parameters::TEST_BANK_FEE_TIER_2;
      } else if (burn_amount == CryptoNote::parameters::TEST_AMOUNT_TIER_3) {
        banking_fee = CryptoNote::parameters::TEST_BANK_FEE_TIER_3;
      }
      uint64_t fee = m_currency.minimumFee();

      // Confirmation
      success_msg_writer() << "";
      success_msg_writer() << "TESTNET Burn Transaction Summary:";
      success_msg_writer() << "  Amount: " << m_currency.formatAmount(burn_amount) << " TEST (PERMANENT)";
      success_msg_writer() << "  Banking Fee: " << m_currency.formatAmount(banking_fee) << " TEST (0.1% of amount to stakers)";
      success_msg_writer() << "  Network Fee: " << m_currency.formatAmount(fee) << " TEST (minimum txn fee to miners)";
      success_msg_writer() << "  Commitment Type: 〘HEAT〙 ✺ These funds will be BURNED (to mint HEAT)";
      success_msg_writer() << "";
      success_msg_writer() << "Confirm? (1) OK  (2) No ";

      std::string confirm;
      m_consoleHandler.readLine(confirm);

      if (confirm != "1" && confirm != "OK" && confirm != "Ok" && confirm != "ok") {
        success_msg_writer() << "Cancelled.";
        return true;
      }

      // Generate unified STARK commitment (v3) for testnet burn
      auto starkResult = CryptoNote::StarkCommitmentGenerator::generate(
          burn_amount,
          CryptoNote::parameters::DEPOSIT_TERM_FOREVER,
          CryptoNote::parameters::STARK_NETWORK_ID_TESTNET,
          CryptoNote::parameters::STARK_TARGET_CHAIN_ETH,
          CryptoNote::parameters::STARK_COMMITMENT_VERSION);

      success_msg_writer() << "";
      success_msg_writer() << "STARK Commitment Data (SAVE THIS — needed to claim HEAT):";
      success_msg_writer() << "  Secret:     " << Common::podToHex(starkResult.secret);
      success_msg_writer() << "  Commitment: " << Common::podToHex(starkResult.commitment);
      success_msg_writer() << "  Nullifier:  " << Common::podToHex(starkResult.nullifier);
      success_msg_writer() << "";

      std::vector<uint8_t> extra;
      CryptoNote::TransactionExtraHeatCommitment heatCommitment;
      heatCommitment.commitment = starkResult.commitment;
      heatCommitment.amount = burn_amount;
      heatCommitment.metadata = {0x08};

      CryptoNote::addHeatCommitmentToExtra(extra, heatCommitment);

      // Encrypt STARK secret into tx extra (0xD5)
      CryptoNote::AccountKeys walletKeys;
      m_wallet->getAccountKeys(walletKeys);
      CryptoNote::DepositSecretPayload secretPayload;
      secretPayload.depositType = 0x08;
      secretPayload.amount = burn_amount;
      secretPayload.term = CryptoNote::parameters::DEPOSIT_TERM_FOREVER;
      memcpy(secretPayload.depositSecret, &starkResult.secret, 32);
      CryptoNote::TransactionExtraDepositSecret encSecret;
      if (CryptoNote::encryptDepositSecret(secretPayload, walletKeys.address.viewPublicKey, encSecret)) {
        CryptoNote::addDepositSecretToExtra(extra, encSecret);
      }

      std::string extraString = std::string(extra.begin(), extra.end());

      success_msg_writer() << "Creating TEST burn (HEAT): " << m_currency.formatAmount(burn_amount) << " TEST";

      CryptoNote::WalletHelper::SendCompleteResultObserver sent;
      WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

      CryptoNote::TransactionId txId = m_wallet->deposit(burn_term, burn_amount, fee + banking_fee, extraString, 0);

      if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
        fail_msg_writer() << "Sending deposit transaction failed";
        return true;
      }

      std::error_code sendError = sent.wait(txId);
      removeGuard.removeObserver();

      if (sendError) {
        fail_msg_writer() << "Burn transaction failed: " << sendError.message();
        return true;
      }

      success_msg_writer() << "TEST burn transaction created. TX ID: " << txId;
      return true;
    }
    catch (const std::exception& e)
    {
      fail_msg_writer() << "Error: " << e.what();
      return true;
    }
  }

  //----------------------------------------------------------------------------------------------------
  bool CryptoNote::testnet_wallet::cold(const std::vector<std::string> &args)
  {
    // COLD deposit - testnet version with term code validation
    if (args.size() != 2)
    {
      fail_msg_writer() << "Usage: cold <amount> <term_code>";
      fail_msg_writer() << "Valid amounts: 0.08, 0.8, 8, 80 TEST";
      fail_msg_writer() << "Valid term codes: 3 (3 months), 12 (1 year)";
      return true;
    }

    try
    {
      uint64_t cold_amount = 0;
      bool ok = m_currency.parseAmount(args[0], cold_amount);

      if (!ok || 0 == cold_amount)
      {
        fail_msg_writer() << "Invalid amount format: " << args[0];
        return true;
      }

      std::vector<uint64_t> valid_amounts = {
        CryptoNote::parameters::TEST_AMOUNT_TIER_0,
        CryptoNote::parameters::TEST_AMOUNT_TIER_1,
        CryptoNote::parameters::TEST_AMOUNT_TIER_2,
        CryptoNote::parameters::TEST_AMOUNT_TIER_3
      };

      auto it = std::find(valid_amounts.begin(), valid_amounts.end(), cold_amount);
      if (it == valid_amounts.end()) {
        fail_msg_writer() << "Invalid amount. Valid tiers: 0.08, 0.8, 8, 80 TEST";
        return true;
      }

      uint32_t term_code = boost::lexical_cast<uint32_t>(args[1]);
      uint32_t cold_term = 0;
      std::string term_label = "";

      uint32_t min_term = CryptoNote::parameters::TESTNET_DEPOSIT_MIN_TERM;
      uint32_t max_term = CryptoNote::parameters::TESTNET_DEPOSIT_MAX_TERM;

      // Map term codes to valid testnet terms
      if (term_code == 3) {
        // For testnet, use a term that's within valid range
        cold_term = min_term;  // 16 blocks for shortest term
        term_label = "3 months (testnet: 16 blocks)";
      } else if (term_code == 12) {
        // For testnet, use a term that's within valid range
        cold_term = max_term;  // 65 blocks for longest term
        term_label = "1 year (testnet: 65 blocks)";
      } else {
        fail_msg_writer() << "Invalid term code. Use: 3 (3 months) or 12 (1 year)";
        return true;
      }

      // Determine banking fee based on amount tier (testnet rates: 0.1% of TEST_AMOUNT_TIER)
      uint64_t banking_fee = 0;
      if (cold_amount == CryptoNote::parameters::TEST_AMOUNT_TIER_0) {
        banking_fee = CryptoNote::parameters::TEST_BANK_FEE_TIER_0;
      } else if (cold_amount == CryptoNote::parameters::TEST_AMOUNT_TIER_1) {
        banking_fee = CryptoNote::parameters::TEST_BANK_FEE_TIER_1;
      } else if (cold_amount == CryptoNote::parameters::TEST_AMOUNT_TIER_2) {
        banking_fee = CryptoNote::parameters::TEST_BANK_FEE_TIER_2;
      } else if (cold_amount == CryptoNote::parameters::TEST_AMOUNT_TIER_3) {
        banking_fee = CryptoNote::parameters::TEST_BANK_FEE_TIER_3;
      }
      // Fee = minimum fee
      uint64_t fee = m_currency.minimumFee();

      // Confirmation
      success_msg_writer() << "";
      success_msg_writer() << "TESTNET Certificate Of Ledger Deposit Summary:";
      success_msg_writer() << "  Amount: " << m_currency.formatAmount(cold_amount) << " TEST";
      success_msg_writer() << "  Term: " << term_label << " (" << cold_term << " blocks)";
      success_msg_writer() << "  Banking Fee: " << m_currency.formatAmount(banking_fee) << " TEST (0.1% of amount to stakers)";
      success_msg_writer() << "  Network Fee: " << m_currency.formatAmount(fee) << " TEST (minimum txn fee to miners)";
      success_msg_writer() << "  Commitment Type:【COLD】 ▋ Off-chain (CD) interest yield";
      success_msg_writer() << "";
      success_msg_writer() << "Confirm? (1) OK  (2) NO  ";

      std::string confirm;
      m_consoleHandler.readLine(confirm);

      if (confirm != "1" && confirm != "OK" && confirm != "Ok" && confirm != "ok") {
        success_msg_writer() << "Cancelled.";
        return true;
      }

      // Generate unified STARK commitment (v3) for testnet COLD deposit
      auto starkResult = CryptoNote::StarkCommitmentGenerator::generate(
          cold_amount,
          cold_term,
          CryptoNote::parameters::STARK_NETWORK_ID_TESTNET,
          CryptoNote::parameters::STARK_TARGET_CHAIN_ETH,
          CryptoNote::parameters::STARK_COMMITMENT_VERSION);

      success_msg_writer() << "";
      success_msg_writer() << "STARK Commitment Data (SAVE THIS — needed to claim CD interest):";
      success_msg_writer() << "  Secret:     " << Common::podToHex(starkResult.secret);
      success_msg_writer() << "  Commitment: " << Common::podToHex(starkResult.commitment);
      success_msg_writer() << "  Nullifier:  " << Common::podToHex(starkResult.nullifier);
      success_msg_writer() << "";

      /*
      std::vector<uint8_t> extra;
      CryptoNote::TransactionExtraColdCommitment coldCommitment;
      coldCommitment.commitment = starkResult.commitment;
      coldCommitment.amount = cold_amount;
      coldCommitment.term = cold_term;
      coldCommitment.claimChainCode = 1;  // Default to ETH chain

      CryptoNote::addColdCommitmentToExtra(extra, coldCommitment);

      // Encrypt STARK secret into tx extra (0xD5)
      CryptoNote::AccountKeys walletKeys;
      m_wallet->getAccountKeys(walletKeys);
      CryptoNote::DepositSecretPayload secretPayload;
      secretPayload.depositType = 0xCD;
      secretPayload.amount = cold_amount;
      secretPayload.term = cold_term;
      memcpy(secretPayload.depositSecret, &starkResult.secret, 32);
      CryptoNote::TransactionExtraDepositSecret encSecret;
      if (CryptoNote::encryptDepositSecret(secretPayload, walletKeys.address.viewPublicKey, encSecret)) {
        CryptoNote::addDepositSecretToExtra(extra, encSecret);
      }

      std::string extraString = std::string(extra.begin(), extra.end());

      success_msg_writer() << "Creating COLD transaction: " << m_currency.formatAmount(cold_amount) << " TEST for " << term_label;

      CryptoNote::WalletHelper::SendCompleteResultObserver sent;
      WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

      CryptoNote::TransactionId txId = m_wallet->deposit(cold_term, cold_amount, fee + banking_fee, extraString, 0);

      if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
        fail_msg_writer() << "Sending deposit transaction failed";
        return true;
      }

      std::error_code sendError = sent.wait(txId);
      removeGuard.removeObserver();

      if (sendError) {
        fail_msg_writer() << "COLD transaction failed: " << sendError.message();
        return true;
      }

      success_msg_writer() << "COLD txn created. TX ID: " << txId;
      return true;
      */
    }
    catch (const std::exception& e)
    {
      fail_msg_writer() << "Error: " << e.what();
      return true;
    }
  }

  //----------------------------------------------------------------------------------------------------
  // bool CryptoNote::testnet_wallet::elderking_ceremony(const std::vector<std::string> &args)
  // {
  //   fail_msg_writer() << "The elderking_ceremony command is no longer available.";
  //   return true;
  // }

  //----------------------------------------------------------------------------------------------------
  // bool CryptoNote::testnet_wallet::unstake(const std::vector<std::string> &args)
  // {
  //   fail_msg_writer() << "The unstake command is no longer available.";
  //   return true;
  // }

  //----------------------------------------------------------------------------------------------------
  bool CryptoNote::testnet_wallet::list_burns(const std::vector<std::string> &args)
  {
    // List all HEAT burn deposits
    try
    {
      success_msg_writer() << "";
      success_msg_writer() << "=== TESTNET Burn Transactions ===";
      success_msg_writer() << "";

      size_t depositCount = m_wallet->getDepositCount();
      size_t burnCount = 0;

      for (size_t i = 0; i < depositCount; ++i) {
        Deposit deposit;
        if (m_wallet->getDeposit(i, deposit)) {
          // Check if this is a HEAT/burn deposit (FOREVER term)
          if (deposit.term == CryptoNote::parameters::DEPOSIT_TERM_FOREVER) {
            burnCount++;
            success_msg_writer() << "  [" << i << "] Amount: " << m_currency.formatAmount(deposit.amount)
                                 << " TEST | Status: Burned"
                                 << " | Type: HEAT (0x08)";
          }
        }
      }

      if (burnCount == 0) {
        success_msg_writer() << "  No burn transactions found.";
      } else {
        success_msg_writer() << "";
        success_msg_writer() << "Total burn transactions: " << burnCount;
      }

      success_msg_writer() << "";
      return true;
    }
    catch (const std::exception& e)
    {
      fail_msg_writer() << "Error listing burns: " << e.what();
      return true;
    }
  }
}
