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
    m_consoleHandler.setHandler("cold", boost::bind(&testnet_wallet::cold, this, boost::arg<1>()), "cold <amount> <term_code> - Create a Certificate of Ledger Deposit (0.8, 8, 80, 800 TEST with terms 3 (3months) or 12 (1yr)");
    m_consoleHandler.setHandler("elderking_ceremony", boost::bind(&testnet_wallet::elderking_ceremony, this, boost::arg<1>()), "elderking_ceremony - Begin the Staking Ceremony to become a Testifier (interactive, 444.4 TEST req'd).");
    m_consoleHandler.setHandler("unstake", boost::bind(&testnet_wallet::unstake, this, boost::arg<1>()), "unstake - Batch-withdraw all Testifier staking deposits (single tx)");
    m_consoleHandler.setHandler("list_burns", boost::bind(&testnet_wallet::list_burns, this, boost::arg<1>()), "list_burns - List all burn transactions.");

    // @ Alias system commands (inherited from simple_wallet)
    m_consoleHandler.setHandler("register_alias", boost::bind(&testnet_wallet::register_alias, this, boost::arg<1>()), "register_alias <alias> - Register a TEST alias (8 chars ONLY: [A-Z0-9] (CAPS-LOCK) req'd for Elderfiers, [a-z0-9] (lowercase) req'd for regular user wallets)");
    m_consoleHandler.setHandler("lookup_alias", boost::bind(&testnet_wallet::lookup_alias, this, boost::arg<1>()), "lookup_alias <alias_or_address> - Look up a TEST alias by name or wallet address");
    m_consoleHandler.setHandler("list_aliases", boost::bind(&testnet_wallet::list_aliases, this, boost::arg<1>()), "list_aliases - List all registered TEST aliases on the network");
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
      success_msg_writer() << "  Banking Fee: " << m_currency.formatAmount(banking_fee) << " TEST (0.1% of amount to Elderfiers)";
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

      uint32_t min_term = CryptoNote::parameters::TESTNET_COLD_MIN_TERM;
      uint32_t max_term = CryptoNote::parameters::TESTNET_COLD_MAX_TERM;

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
      success_msg_writer() << "  Banking Fee: " << m_currency.formatAmount(banking_fee) << " TEST (0.1% of amount to Elderfiers)";
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
    }
    catch (const std::exception& e)
    {
      fail_msg_writer() << "Error: " << e.what();
      return true;
    }
  }

  //----------------------------------------------------------------------------------------------------
  bool CryptoNote::testnet_wallet::elderking_ceremony(const std::vector<std::string> &args)
  {
    // Interactive Testifier Staking Ceremony (Testnet).
    // Alias chosen interactively — no command-line arg needed.

    // ── PRE-CHECK: detect existing ceremony deposits for resume ────────────
    uint32_t efTerm = CryptoNote::parameters::TESTNET_DEPOSIT_TERM_ELDERFIER_STAKING;
    size_t existingStakes = 0;
    {
      size_t depCount = m_wallet->getDepositCount();
      for (CryptoNote::DepositId di = 0; di < depCount; ++di) {
        CryptoNote::Deposit d;
        if (!m_wallet->getDeposit(di, d)) continue;
        if (d.term == efTerm) ++existingStakes;
      }
    }

    const uint32_t totalDeposits = CryptoNote::parameters::TESTIFIER_TOTAL_DEPOSITS;
    if (existingStakes >= totalDeposits) {
      success_msg_writer() << "";
      success_msg_writer() << "  You already have " << totalDeposits << " Testifier stakes. Use elder_council.";
      success_msg_writer() << "";
      return true;
    }

    std::string alias;
    bool resuming = (existingStakes > 0);

    if (resuming) {
      // ── RESUME FLOW ──────────────────────────────────────────────────────
      success_msg_writer() << "";
      success_msg_writer() << "  ── CEREMONY RESUME ──────────────────────────────────────";
      success_msg_writer() << "";
      success_msg_writer() << "  " << existingStakes << "/" << totalDeposits << " Testifier stakes detected.";
      success_msg_writer() << "  " << (totalDeposits - existingStakes) << " remaining stake(s) to complete the ceremony.";
      success_msg_writer() << "";

      // Try to get alias from the alias index (if first deposit confirmed)
      bool aliasFound = false;
      try {
        HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
        COMMAND_RPC_GET_ALIAS_BY_ADDRESS::request addrReq;
        COMMAND_RPC_GET_ALIAS_BY_ADDRESS::response addrRes;
        addrReq.address = m_wallet->getAddress();
        invokeJsonCommand(httpClient, "/get_alias_by_address", addrReq, addrRes);
        if (addrRes.found && addrRes.alias_type == 0) {
          alias = addrRes.alias;
          aliasFound = true;
          success_msg_writer() << "  Registered alias: @" << alias;
        }
      } catch (...) {}

      if (!aliasFound) {
        success_msg_writer() << "  Alias not yet confirmed on-chain.";
        success_msg_writer() << "  Enter your Ælder King name to resume: ";
        m_consoleHandler.readLine(alias);
        while (!alias.empty() && std::isspace((unsigned char)alias.front())) alias.erase(alias.begin());
        while (!alias.empty() && std::isspace((unsigned char)alias.back()))  alias.pop_back();
        if (alias.empty() || !CryptoNote::AliasIndex::isValidElderfierAlias(alias)) {
          fail_msg_writer() << "  Invalid or empty alias. Resume aborted.";
          return true;
        }
      }

      success_msg_writer() << "";
      success_msg_writer() << "  Resuming ceremony for Ælder King " << alias << "...";
      success_msg_writer() << "";

    } else {

    // ── PART I: THE CALLING ────────────────────────────────────────────────
    success_msg_writer() << "";
    success_msg_writer() << "╔════════════════════════════════════════════════════════════╗";
    success_msg_writer() << "║                                                            ║";
    success_msg_writer() << "║         THE TESTIFIER STAKING CEREMONY  [TESTNET]          ║";
    success_msg_writer() << "║                                                            ║";
    success_msg_writer() << "╚════════════════════════════════════════════════════════════╝";
    success_msg_writer() << "";
    success_msg_writer() << "  You stand at the threshold of the Elder Council.";
    success_msg_writer() << "  Before you lies a path of honour, sacrifice, and purpose.";
    success_msg_writer() << "";
    success_msg_writer() << "  ── WHAT IS AN ΞLDERFIER? ────────────────────────────────";
    success_msg_writer() << "";
    success_msg_writer() << "  Elderfiers are the guardians of the Fuego realm — keepers";
    success_msg_writer() << "  of cross-chain truth. They validate deposit commitments,";
    success_msg_writer() << "  sign merkle roots, and ensure that XFG burned on Fuego is";
    success_msg_writer() << "  faithfully reborn on Ethereum. Without Elderfiers, the";
    success_msg_writer() << "  bridge between worlds cannot hold.";
    success_msg_writer() << "";
    success_msg_writer() << "  On testnet, Testifiers rehearse this sacred duty — testing";
    success_msg_writer() << "  the ceremony, the staking mechanism, and the network before";
    success_msg_writer() << "  the mainnet Elderfiers are crowned.";
    success_msg_writer() << "";
    success_msg_writer() << "  ── WHAT IT TAKES TO BE AN ELDER KING ───────────────────";
    success_msg_writer() << "";
    success_msg_writer() << "  COURAGE   You must be brave in the face of adversity.";
    success_msg_writer() << "            When the network is under attack, when nodes";
    success_msg_writer() << "            fall, when pressure mounts — you do not falter.";
    success_msg_writer() << "";
    success_msg_writer() << "  JUSTICE   You must be just in all your judgments.";
    success_msg_writer() << "            Sign only the roots you believe true. Never";
    success_msg_writer() << "            collude, deceive, or act for gain over the good.";
    success_msg_writer() << "";
    success_msg_writer() << "  PROTECTION  You must protect the weak and innocent.";
    success_msg_writer() << "            The small holders, newcomers, the silent burners";
    success_msg_writer() << "            who trust the bridge — their safety is yours.";
    success_msg_writer() << "";
    success_msg_writer() << "  VIGILANCE   You must guard the Realm without rest.";
    success_msg_writer() << "            Run your Testifier node at all times. A guardian";
    success_msg_writer() << "            who sleeps while the Realm burns is no guardian.";
    success_msg_writer() << "";
    success_msg_writer() << "  HONOUR    Your 444.4 TEST stake is your bond on testnet.";
    success_msg_writer() << "            Betrayal of the Realm means slashing — your";
    success_msg_writer() << "            stake burned and name struck from the registry.";
    success_msg_writer() << "";
    success_msg_writer() << "  ── WHAT THE CEREMONY REQUIRES ───────────────────────────";
    success_msg_writer() << "";
    success_msg_writer() << "    20 deposits across all 4 tiers (444.4 TEST total stake)";
    success_msg_writer() << "    5x 0.08 + 5x 0.8 + 5x 8 + 5x 80 TEST";
    success_msg_writer() << "    Tagged 0xEF — the Elderfier mark — slashable stake";
    success_msg_writer() << "    A unique 8-character Ælder King name (your on-chain ID)";
    success_msg_writer() << "    You MUST run an Elderfier node to sign roots & earn fees";
    success_msg_writer() << "";
    success_msg_writer() << "  Do you accept the calling of the Ælder Council?";
    success_msg_writer() << "  Do you pledge to be brave, be just, protect the innocent,";
    success_msg_writer() << "  and guard the Realm with all your might and honour?";
    success_msg_writer() << "";
    success_msg_writer() << "  Type 'yes' to step forward, or Enter to walk away: ";

    std::string acceptance;
    m_consoleHandler.readLine(acceptance);
    if (acceptance.empty() || (acceptance[0] != 'y' && acceptance[0] != 'Y')) {
      success_msg_writer() << "";
      success_msg_writer() << "  The Ælder Council watches. Return when you are ready.";
      success_msg_writer() << "  The Realm awaits those worthy of its flame.";
      success_msg_writer() << "";
      return true;
    }

    success_msg_writer() << "";
    success_msg_writer() << "  The Ælder Council nods. You have answered the call.";
    success_msg_writer() << "";

    // ── PART II: CHOOSE YOUR ELDER KING NAME ──────────────────────────────
    success_msg_writer() << "  ── CHOOSE YOUR ÆLDER KING NAME ─────────────────────────";
    success_msg_writer() << "";
    success_msg_writer() << "  Your Ælder King name is your identity on the Fuego testnet.";
    success_msg_writer() << "  It will be embedded in all 20 of your stakes and registered";
    success_msg_writer() << "  on-chain when your deposits confirm.";
    success_msg_writer() << "";
    success_msg_writer() << "  Rules:  Exactly 8 characters  |  A-Z  0-9  & only";
    success_msg_writer() << "  Special exemptions: GALAPAGOS  |  WINSLAYER  |  LOUDMINING  (reserved names)";
    success_msg_writer() << "  No two Testifiers may share a name.";
    success_msg_writer() << "";
    success_msg_writer() << "  Examples:";
    success_msg_writer() << "    TESTKING  |  IGNITE88  |  NETGUARD  |  BLAZE&TN";
    success_msg_writer() << "    REALM001  |  VAULT888  |  DRAGON&1  |  XFG&TEST";
    success_msg_writer() << "";

    while (true) {
      success_msg_writer() << "  Enter your Ælder King name: ";
      m_consoleHandler.readLine(alias);
      // Trim whitespace
      while (!alias.empty() && std::isspace((unsigned char)alias.front())) alias.erase(alias.begin());
      while (!alias.empty() && std::isspace((unsigned char)alias.back()))  alias.pop_back();

      if (alias.empty()) {
        success_msg_writer() << "";
        success_msg_writer() << "  The Ælder Council watches. Return when you are ready.";
        return true;
      }
      if (!CryptoNote::AliasIndex::isValidElderfierAlias(alias)) {
        fail_msg_writer() << "  Invalid name '" << alias << "'.";
        fail_msg_writer() << "  Use exactly 8 characters [A-Z 0-9 &], unless, if you own one of the 2 reserved exceptional aliases:";
        fail_msg_writer() << "  GALAPAGOS or WINSLAYER, and LOUDMINING";
        fail_msg_writer() << "  (Press Enter with no input to abort.)";
        continue;
      }
      break;
    }

    // ── PART III: THE OATH ─────────────────────────────────────────────────
    success_msg_writer() << "";
    success_msg_writer() << "  Before the Eternal Flame and the eyes of the Ælder Council,";
    success_msg_writer() << "  hear the oath of Elder King " << alias << ":";
    success_msg_writer() << "";
    success_msg_writer() << "  ════════════════════════════════════════════════════════";
    success_msg_writer() << "";
    success_msg_writer() << "  Ælder King " << alias << ", do you vow:";
    success_msg_writer() << "";
    success_msg_writer() << "    to be BRAVE in the face of all adversity — to stand";
    success_msg_writer() << "    firm when the network is tested, threatened, or besieged?";
    success_msg_writer() << "";
    success_msg_writer() << "    to be JUST in all your judgments — to sign only the";
    success_msg_writer() << "    roots you believe true, and never act for dishonest gain?";
    success_msg_writer() << "";
    success_msg_writer() << "    to PROTECT the weak and innocent — to safeguard those";
    success_msg_writer() << "    who burn their TEST in trust that the bridge will hold?";
    success_msg_writer() << "";
    success_msg_writer() << "    to GUARD the Realm with all your might and honour —";
    success_msg_writer() << "    running your node with vigilance and without deceit?";
    success_msg_writer() << "";
    success_msg_writer() << "    to honour the ETERNAL FLAME — knowing that betrayal";
    success_msg_writer() << "    brings slashing, and your stake is your sacred bond";
    success_msg_writer() << "    to this network, now and for all time?";
    success_msg_writer() << "";
    success_msg_writer() << "  ════════════════════════════════════════════════════════";
    success_msg_writer() << "";
    success_msg_writer() << "  To seal these vows, enter your Ælder King name again: ";

    std::string confirmAlias;
    m_consoleHandler.readLine(confirmAlias);
    while (!confirmAlias.empty() && std::isspace((unsigned char)confirmAlias.front())) confirmAlias.erase(confirmAlias.begin());
    while (!confirmAlias.empty() && std::isspace((unsigned char)confirmAlias.back()))  confirmAlias.pop_back();

    if (confirmAlias != alias) {
      fail_msg_writer() << "";
      fail_msg_writer() << "  The names do not match.";
      fail_msg_writer() << "  Entered '" << confirmAlias << "' — your chosen name was '" << alias << "'.";
      fail_msg_writer() << "  The Realm demands certainty. Return when your resolve is firm.";
      return true;
    }

    success_msg_writer() << "";
    success_msg_writer() << "  So be it.  Ælder King " << alias << " rises.";
    success_msg_writer() << "";

    // ── RPC: check alias availability ──────────────────────────────────────
    try {
      HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
      COMMAND_RPC_GET_ALIAS::request checkReq;
      COMMAND_RPC_GET_ALIAS::response checkRes;
      checkReq.alias = alias;
      invokeJsonCommand(httpClient, "/get_alias", checkReq, checkRes);
      if (checkRes.found) {
        fail_msg_writer() << "  The name @" << alias << " is already claimed by another Testifier.";
        fail_msg_writer() << "  Ceremony aborted. Run elderking_ceremony again to choose a new name.";
        return true;
      }
    } catch (const ConnectException&) {
      printConnectionError();
      return true;
    } catch (const std::exception& e) {
      fail_msg_writer() << "Failed to check alias availability: " << e.what();
      return true;
    }

    // ── RPC: check address not already registered ───────────────────────────
    try {
      HttpClient httpClient(m_dispatcher, m_daemon_host, m_daemon_port);
      COMMAND_RPC_GET_ALIAS_BY_ADDRESS::request addrReq;
      COMMAND_RPC_GET_ALIAS_BY_ADDRESS::response addrRes;
      addrReq.address = m_wallet->getAddress();
      invokeJsonCommand(httpClient, "/get_alias_by_address", addrReq, addrRes);
      if (addrRes.found) {
        fail_msg_writer() << "  Your address already bears the name @" << addrRes.alias;
        fail_msg_writer() << "  A Testifier may not be crowned twice. Ceremony aborted.";
        return true;
      }
    } catch (const ConnectException&) {
      printConnectionError();
      return true;
    } catch (const std::exception& e) {
      fail_msg_writer() << "Failed to check address alias: " << e.what();
      return true;
    }

    } // end of new-ceremony interactive prompts (else block)

    // ══════════════════════════════════════════════════════════════════════
    // COMMON PATH: balance check, dry run, ceremony loop
    // Both new-ceremony and resume paths reach here with 'alias' set.
    // ══════════════════════════════════════════════════════════════════════

    try
    {
      // ── Balance check ──────────────────────────────────────────────────
      uint64_t balance = m_wallet->actualBalance();
      const uint64_t tierAmounts[] = {
        CryptoNote::parameters::TEST_AMOUNT_TIER_0,  // 0.08 TEST
        CryptoNote::parameters::TEST_AMOUNT_TIER_1,  // 0.8 TEST
        CryptoNote::parameters::TEST_AMOUNT_TIER_2,  // 8 TEST
        CryptoNote::parameters::TEST_AMOUNT_TIER_3   // 80 TEST
      };
      const uint32_t depositsPerTier = CryptoNote::parameters::TESTIFIER_DEPOSITS_PER_TIER;
      uint64_t fee = m_currency.minimumFee();

      // Calculate remaining deposits and amount needed
      uint64_t remainingRequired = 0;
      uint32_t remainingCount = 0;
      {
        uint32_t cnt = 0;
        for (uint32_t tier = 0; tier < 4; ++tier) {
          for (uint32_t d = 0; d < depositsPerTier; ++d) {
            if (cnt >= existingStakes) {
              remainingRequired += tierAmounts[tier];
              remainingCount++;
            }
            cnt++;
          }
        }
      }

      success_msg_writer() << "  ── PREPARING THE RITUAL ─────────────────────────────────";
      success_msg_writer() << "";
      success_msg_writer() << "  Wallet balance:       " << m_currency.formatAmount(balance) << " TEST";
      if (resuming) {
        success_msg_writer() << "  Remaining stakes:     " << remainingCount << " deposits ("
                             << m_currency.formatAmount(remainingRequired) << " TEST)";
      } else {
        success_msg_writer() << "  20 stakes (5/tier):   " << m_currency.formatAmount(remainingRequired) << " TEST";
        success_msg_writer() << "    5x 0.08 + 5x 0.8 + 5x 8 + 5x 80 TEST";
      }
      success_msg_writer() << "  Network fees (x" << remainingCount << "): " << m_currency.formatAmount(remainingCount * fee) << " TEST";
      success_msg_writer() << "  Total required:       " << m_currency.formatAmount(remainingRequired + (remainingCount * fee)) << " TEST";
      success_msg_writer() << "";

      if (balance < remainingRequired + (remainingCount * fee)) {
        fail_msg_writer() << "  The flame requires more fuel.";
        fail_msg_writer() << "  You need " << m_currency.formatAmount(remainingRequired + (remainingCount * fee) - balance) << " more TEST.";
        fail_msg_writer() << "  Ceremony aborted. Return when your coffers are ready.";
        return true;
      }

      // ── DRY RUN: verify wallet has enough separate outputs ──────────────
      // After each deposit TX, change outputs are locked (unconfirmed) and
      // cannot be spent by subsequent deposits. Simulate same bucket-based selection 
      // that selectTransfersToSend uses: group outputs into power-of-10 buckets
      // take one from each bucket per round (smallest bucket first) until 
      // neededMoney is covered w/o adding change back.
      {
        auto unspent = m_wallet->getUnspentOutputs();
        std::vector<uint64_t> available;
        for (const auto& out : unspent) {
          if (out.amount > m_currency.defaultDustThreshold())
            available.push_back(out.amount);
        }

        // Build deposit requirements in ceremony order (tier 0→3), skipping done deposits
        struct DepositReq { uint64_t amount; uint32_t count; };
        std::vector<DepositReq> reqs;
        {
          uint32_t cnt = 0;
          for (uint32_t tier = 0; tier < 4; ++tier) {
            uint32_t tierRemaining = 0;
            for (uint32_t d = 0; d < depositsPerTier; ++d) {
              if (cnt >= existingStakes) tierRemaining++;
              cnt++;
            }
            if (tierRemaining > 0)
              reqs.push_back({tierAmounts[tier], tierRemaining});
          }
        }

        // Simulate selectTransfersToSend bucket algorithm for each deposit
        bool dryRunOk = true;
        for (const auto& req : reqs) {
          for (uint32_t d = 0; d < req.count; ++d) {
            uint64_t needed = req.amount + fee;

            // Group remaining outputs into base-10 digit buckets
            std::map<int, std::vector<size_t>> buckets;
            for (size_t i = 0; i < available.size(); ++i) {
              if (available[i] > 0) {
                int digits = static_cast<int>(std::floor(std::log10(static_cast<double>(available[i])))) + 1;
                buckets[digits].push_back(i);
              }
            }

            // Take one output from each bucket per round (smallest bucket first)
            uint64_t found = 0;
            std::vector<size_t> consumed;
            while (found < needed && !buckets.empty()) {
              for (auto it = buckets.begin(); it != buckets.end(); ) {
                if (it->second.empty()) {
                  it = buckets.erase(it);
                } else {
                  if (found < needed) {
                    size_t idx = it->second.back();
                    it->second.pop_back();
                    found += available[idx];
                    consumed.push_back(idx);
                  }
                  ++it;
                }
              }
            }

            if (found < needed) {
              dryRunOk = false;
              break;
            }

            // Remove consumed outputs (mark as 0 so indices stay stable)
            for (size_t idx : consumed) {
              available[idx] = 0;
            }
          }
          if (!dryRunOk) break;
        }

        // Clean up zeroed entries
        available.erase(std::remove(available.begin(), available.end(), uint64_t(0)), available.end());

        if (!dryRunOk) {
          fail_msg_writer() << "";
          fail_msg_writer() << "  Dry run failed: not enough separate outputs for " << remainingCount << " deposits.";
          fail_msg_writer() << "  Each deposit consumes wallet outputs, and change from pending";
          fail_msg_writer() << "  transactions is not immediately spendable.";
          fail_msg_writer() << "";
          fail_msg_writer() << "  Type 'prepare' to auto-split your funds for the ceremony,";
          fail_msg_writer() << "  or press Enter to abort: ";
          std::string choice;
          m_consoleHandler.readLine(choice);
          while (!choice.empty() && std::isspace((unsigned char)choice.front())) choice.erase(choice.begin());
          while (!choice.empty() && std::isspace((unsigned char)choice.back()))  choice.pop_back();

          if (choice == "prepare" || choice == "PREPARE" || choice == "Prepare") {
            // Create a single transaction that sends exact tier amounts to self,
            // creating one output per remaining deposit at the right amount.
            success_msg_writer() << "";
            success_msg_writer() << "  Preparing " << remainingCount << " outputs for the ceremony...";

            std::string selfAddr = m_wallet->getAddress();
            std::vector<CryptoNote::WalletLegacyTransfer> prepTransfers;
            uint32_t cnt = 0;
            for (uint32_t tier = 0; tier < 4; ++tier) {
              for (uint32_t d = 0; d < depositsPerTier; ++d) {
                if (cnt >= existingStakes) {
                  CryptoNote::WalletLegacyTransfer t;
                  t.address = selfAddr;
                  t.amount = tierAmounts[tier] + fee;  // deposit amount + fee per output
                  prepTransfers.push_back(t);
                }
                cnt++;
              }
            }

            Crypto::SecretKey prepTxSK;
            CryptoNote::WalletHelper::SendCompleteResultObserver prepSent;
            WalletHelper::IWalletRemoveObserverGuard prepGuard(*m_wallet, prepSent);

            CryptoNote::TransactionId prepTxId = m_wallet->sendTransaction(
              prepTxSK, prepTransfers, fee, std::string(), 0, 0, {}, 0);

            if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == prepTxId) {
              prepGuard.removeObserver();
              fail_msg_writer() << "  Failed to create preparation transaction.";
              return true;
            }

            std::error_code prepErr = prepSent.wait(prepTxId);
            prepGuard.removeObserver();
            if (prepErr) {
              fail_msg_writer() << "  Preparation failed: " << prepErr.message();
              return true;
            }

            try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
            catch (...) {}

            success_msg_writer() << "  Preparation TX broadcast (TX: " << prepTxId << ").";
            success_msg_writer() << "  Wait for 1 block confirmation, then run elderking_ceremony again.";
            success_msg_writer() << "";
            return true;
          }

          fail_msg_writer() << "  Ceremony aborted. Consolidate your outputs and try again.";
          return true;
        }
        success_msg_writer() << "  Dry run passed: outputs can cover all " << remainingCount << " deposits.";
      }

      success_msg_writer() << "  The balance holds. The Ritual of Five Flames begins.";
      success_msg_writer() << "";
      success_msg_writer() << "╔════════════════════════════════════════════════════════════╗";
      success_msg_writer() << "║            THE RITUAL OF FIVE FLAMES  [TESTNET]            ║";
      success_msg_writer() << "╚════════════════════════════════════════════════════════════╝";
      success_msg_writer() << "";

      static const char* const tierNames[] = { "(0.08 TEST)", "(0.8 TEST)", "(8 TEST)", "(80 TEST)" };

      // Fetch spend public key once — used to build per-deposit commitment
      CryptoNote::AccountKeys walletKeys;
      m_wallet->getAccountKeys(walletKeys);

      // Derive deterministic signing keypair from wallet spend key.
      Crypto::PublicKey signingPubKey;
      Crypto::SecretKey signingSecKey;
      {
        static const char label[] = "fuego_ef_sign___";  // 16 bytes
        uint8_t preimage[48];
        std::memcpy(preimage,      label, 16);
        std::memcpy(preimage + 16, walletKeys.spendSecretKey.data, 32);
        Crypto::hash_to_scalar(preimage, sizeof(preimage),
          reinterpret_cast<Crypto::EllipticCurveScalar&>(signingSecKey));
        Crypto::secret_key_to_public_key(signingSecKey, signingPubKey);
      }

      uint32_t flameCount = 0;
      bool firstDeposit = (existingStakes == 0);  // alias already registered if resuming
      for (uint32_t tier = 0; tier < 4; ++tier) {
        // Skip entire tier if all its deposits are already done
        uint32_t tierEnd = (tier + 1) * depositsPerTier;
        if (tierEnd <= existingStakes) {
          flameCount += depositsPerTier;
          continue;
        }

        success_msg_writer() << "";
        success_msg_writer() << "  ── Tier " << tier << ": " << tierNames[tier] << " ──";

        for (uint32_t d = 0; d < depositsPerTier; ++d) {
          ++flameCount;
          if (flameCount <= (uint32_t)existingStakes) continue;  // skip already-done deposits

          uint64_t depositAmount = tierAmounts[tier];
          success_msg_writer() << "    Flame " << flameCount << "/" << totalDeposits
                               << " — " << m_currency.formatAmount(depositAmount) << " TEST...";

          std::vector<uint8_t> extra;
          Crypto::PublicKey public_key;
          Crypto::SecretKey secret_key;
          Crypto::generate_keys(public_key, secret_key);
          Crypto::Hash commitment_hash = Crypto::cn_fast_hash(public_key.data, sizeof(public_key.data));

          // Build one-way commitment: H(spendPublicKey || ephemeralPublicKey)
          uint8_t commit_preimage[64];
          std::memcpy(commit_preimage,      walletKeys.address.spendPublicKey.data, 32);
          std::memcpy(commit_preimage + 32, public_key.data,                         32);
          Crypto::Hash elderfier_commitment = Crypto::cn_fast_hash(commit_preimage, sizeof(commit_preimage));

          CryptoNote::TransactionExtraElderfierDeposit elderfierDeposit;
          elderfierDeposit.depositHash        = commitment_hash;
          elderfierDeposit.depositAmount      = depositAmount;
          elderfierDeposit.elderfierCommitment = elderfier_commitment;
          elderfierDeposit.securityWindow     = 28800;
          elderfierDeposit.metadata.clear();
          elderfierDeposit.metadata.push_back(0xEA);
          elderfierDeposit.metadata.insert(elderfierDeposit.metadata.end(), alias.begin(), alias.end());
          elderfierDeposit.metadata.insert(elderfierDeposit.metadata.end(),
            signingPubKey.data, signingPubKey.data + 32);
          elderfierDeposit.signature.clear();
          elderfierDeposit.isSlashable        = true;

          if (firstDeposit) {
            CryptoNote::TransactionExtraAliasRegistration aliasReg;
            aliasReg.alias = alias;
            aliasReg.aliasHash = Crypto::cn_fast_hash(alias.data(), alias.size());
            std::string walletAddr = m_wallet->getAddress();
            aliasReg.addressHash = Crypto::cn_fast_hash(walletAddr.data(), walletAddr.size());
            aliasReg.ownerAddress = "";
            aliasReg.aliasType = 0;
            CryptoNote::addAliasToExtra(extra, aliasReg);
            firstDeposit = false;
          }

          CryptoNote::addElderfierDepositToExtra(extra, elderfierDeposit);
          std::string extraString = std::string(extra.begin(), extra.end());

          CryptoNote::WalletHelper::SendCompleteResultObserver sent;
          WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

          CryptoNote::TransactionId txId = m_wallet->deposit(
            CryptoNote::parameters::TESTNET_DEPOSIT_TERM_ELDERFIER_STAKING,
            depositAmount,
            fee,
            extraString,
            0
          );

          if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
            removeGuard.removeObserver();
            fail_msg_writer() << "";
            fail_msg_writer() << "  The ritual faltered at flame " << flameCount << " of " << totalDeposits << ".";
            fail_msg_writer() << "  " << (flameCount - 1) << " stake(s) were forged before it broke.";
            fail_msg_writer() << "  Check your balance and connection, then try again.";
            try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
            catch (...) {}
            return true;
          }

          std::error_code sendError = sent.wait(txId);
          removeGuard.removeObserver();
          if (sendError) {
            fail_msg_writer() << "";
            fail_msg_writer() << "  Flame " << flameCount << " failed: " << sendError.message();
            fail_msg_writer() << "  " << (flameCount - 1) << " stake(s) were forged before this flame broke.";
            try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
            catch (...) {}
            return true;
          }

          success_msg_writer() << "      Sealed.  TX: " << txId;
        }

        // Auto-save after each completed tier
        try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
        catch (...) {}
      }

      // ── Completion ─────────────────────────────────────────────────────
      try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
      catch (...) {}

      success_msg_writer() << "";
      success_msg_writer() << "╔════════════════════════════════════════════════════════════╗";
      success_msg_writer() << "║          TESTNET CEREMONY COMPLETE — TESTIFIER IGNITED     ║";
      success_msg_writer() << "╚════════════════════════════════════════════════════════════╝";
      success_msg_writer() << "";
      success_msg_writer() << "  Ælder King " << alias << " — all 20 Testifier stakes have been";
      success_msg_writer() << "  broadcast to the testnet. Your name is embedded in each.";
      success_msg_writer() << "";
      success_msg_writer() << "  Once your stake (all 20 deposits) confirms on-chain, the testnet will";
      success_msg_writer() << "  register you as Ælder King " << alias;
      success_msg_writer() << "  and add you to the active Testifiers registry.";
      success_msg_writer() << "";
      success_msg_writer() << "  ── YOUR TESTIFIER SIGNING KEY ───────────────────────────";
      success_msg_writer() << "";
      success_msg_writer() << "  Signing key:  " << Common::podToHex(signingSecKey);
      success_msg_writer() << "";
      success_msg_writer() << "  To run your Testifier signing node:";
      success_msg_writer() << "";
      success_msg_writer() << "    testnetd --testifier-key " << Common::podToHex(signingSecKey);
      success_msg_writer() << "";
      success_msg_writer() << "  Derived from your wallet — view again via  elder_council";
      success_msg_writer() << "";
      success_msg_writer() << "  Your Elderfire burns bright.";
      success_msg_writer() << "  Guard the Realm well, " << alias << ".";
      success_msg_writer() << "";
      success_msg_writer() << "  Commands:  list_cold  |  lookup_alias " << alias;
      success_msg_writer() << "";

      return true;
    }
    catch (const std::exception& e)
    {
      try { CryptoNote::WalletHelper::storeWallet(*m_wallet, m_wallet_file); }
      catch (...) {}
      fail_msg_writer() << "Error during ceremony: " << e.what();
      return true;
    }

  return true;
  }

  //----------------------------------------------------------------------------------------------------
  bool CryptoNote::testnet_wallet::unstake(const std::vector<std::string> &args)
  {
    try
    {
      size_t deposit_count = m_wallet->getDepositCount();
      if (deposit_count == 0) {
        fail_msg_writer() << "No deposits found in this wallet.";
        return true;
      }

      uint32_t efTerm = CryptoNote::parameters::TESTNET_DEPOSIT_TERM_ELDERFIER_STAKING;
      std::vector<CryptoNote::DepositId> efierIds;
      uint64_t totalAmount = 0;

      for (size_t i = 0; i < deposit_count; ++i) {
        CryptoNote::Deposit deposit;
        if (!m_wallet->getDeposit(i, deposit)) continue;

        bool isEfier = (deposit.term == efTerm)
                    || (deposit.depositType == CryptoNote::Deposit::Type::ELDERFIER);
        if (!isEfier) continue;
        if (deposit.spendingTransactionId != CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID) continue;

        if (deposit.locked) {
          success_msg_writer() << "  Deposit #" << i << " (" << m_currency.formatAmount(deposit.amount)
                               << " TEST) still locked until block " << deposit.unlockHeight;
          continue;
        }

        efierIds.push_back(i);
        totalAmount += deposit.amount;
      }

      if (efierIds.empty()) {
        fail_msg_writer() << "No withdrawable Testifier staking deposits found.";
        return true;
      }

      success_msg_writer() << "";
      success_msg_writer() << "  ── TESTIFIER UNSTAKING ───────────────────────────────────";
      success_msg_writer() << "";
      success_msg_writer() << "  Found " << efierIds.size() << " Testifier staking deposits";
      success_msg_writer() << "  Total stake: " << m_currency.formatAmount(totalAmount) << " TEST";
      success_msg_writer() << "";
      success_msg_writer() << "  Type 'confirm' to proceed, or Enter to cancel: ";

      std::string confirmation;
      m_consoleHandler.readLine(confirmation);
      while (!confirmation.empty() && std::isspace((unsigned char)confirmation.front())) confirmation.erase(confirmation.begin());
      while (!confirmation.empty() && std::isspace((unsigned char)confirmation.back()))  confirmation.pop_back();

      if (confirmation != "confirm") {
        success_msg_writer() << "  Unstaking cancelled.";
        return true;
      }

      uint64_t fee = m_currency.minimumFee();

      CryptoNote::WalletHelper::SendCompleteResultObserver sent;
      WalletHelper::IWalletRemoveObserverGuard removeGuard(*m_wallet, sent);

      CryptoNote::TransactionId txId = m_wallet->withdrawDeposits(efierIds, fee);
      if (CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID == txId) {
        removeGuard.removeObserver();
        fail_msg_writer() << "  Failed to create batch withdrawal transaction.";
        return true;
      }

      std::error_code sendError = sent.wait(txId);
      removeGuard.removeObserver();
      if (sendError) {
        fail_msg_writer() << "  Unstaking failed: " << sendError.message();
        return true;
      }

      success_msg_writer() << "";
      success_msg_writer() << "  Unstaking transaction sent!";
      success_msg_writer() << "  Transaction ID: " << txId;
      success_msg_writer() << "  Deposits withdrawn: " << efierIds.size();
      success_msg_writer() << "  Total returned: " << m_currency.formatAmount(totalAmount) << " TEST";
      success_msg_writer() << "";
    }
    catch (std::exception &e)
    {
      fail_msg_writer() << "Failed to unstake: " << e.what();
    }

    return true;
  }

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
