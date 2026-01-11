// Copyright (c) 2017-2022 Fuego Developers
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

#include "PaymentServiceJsonRpcServer.h"

#include <functional>

#include "PaymentServiceJsonRpcMessages.h"
#include "WalletService.h"
#include "Common/CommandLine.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Account.h"
#include "crypto/hash.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "CryptoNoteCore/CryptoNoteBasicImpl.h"
#include "CryptoNoteCore/DepositCommitment.h"
#include "CryptoNoteCore/BurnTransactionHandler.h"
#include "WalletLegacy/WalletHelper.h"
#include "Wallet/WalletErrors.h"
#include "Common/Base58.h"
#include "Common/CommandLine.h"
#include "Common/SignalHandler.h"
#include "Common/StringTools.h"
#include "Common/PathTools.h"
#include "Common/Util.h"
#include "Common/FileSystem.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolHandler.h"
#include "Serialization/JsonInputValueSerializer.h"
#include "Serialization/JsonOutputStreamSerializer.h"

namespace PaymentService {

PaymentServiceJsonRpcServer::PaymentServiceJsonRpcServer(System::Dispatcher& sys, System::Event& stopEvent, WalletService& service, Logging::ILogger& loggerGroup)
  : JsonRpcServer(sys, stopEvent, loggerGroup)
  , service(service)
  , logger(loggerGroup, "PaymentServiceJsonRpcServer")
{
  handlers.emplace("save", jsonHandler<Save::Request, Save::Response>(std::bind(&PaymentServiceJsonRpcServer::handleSave, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createIntegrated", jsonHandler<CreateIntegrated::Request, CreateIntegrated::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateIntegrated, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("splitIntegrated", jsonHandler<SplitIntegrated::Request, SplitIntegrated::Response>(std::bind(&PaymentServiceJsonRpcServer::handleSplitIntegrated, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("reset", jsonHandler<Reset::Request, Reset::Response>(std::bind(&PaymentServiceJsonRpcServer::handleReset, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("exportWallet", jsonHandler<ExportWallet::Request, ExportWallet::Response>(std::bind(&PaymentServiceJsonRpcServer::handleExportWallet, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("exportWalletKeys", jsonHandler<ExportWalletKeys::Request, ExportWalletKeys::Response>(std::bind(&PaymentServiceJsonRpcServer::handleExportWalletKeys, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createAddress", jsonHandler<CreateAddress::Request, CreateAddress::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateAddress, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createAddressList", jsonHandler<CreateAddressList::Request, CreateAddressList::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateAddressList, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("deleteAddress", jsonHandler<DeleteAddress::Request, DeleteAddress::Response>(std::bind(&PaymentServiceJsonRpcServer::handleDeleteAddress, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getSpendKeys", jsonHandler<GetSpendKeys::Request, GetSpendKeys::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetSpendKeys, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getBalance", jsonHandler<GetBalance::Request, GetBalance::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetBalance, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getBlockHashes", jsonHandler<GetBlockHashes::Request, GetBlockHashes::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetBlockHashes, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getTransactionHashes", jsonHandler<GetTransactionHashes::Request, GetTransactionHashes::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetTransactionHashes, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getTransactions", jsonHandler<GetTransactions::Request, GetTransactions::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetTransactions, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getUnconfirmedTransactionHashes", jsonHandler<GetUnconfirmedTransactionHashes::Request, GetUnconfirmedTransactionHashes::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetUnconfirmedTransactionHashes, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getTransaction", jsonHandler<GetTransaction::Request, GetTransaction::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetTransaction, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("sendTransaction", jsonHandler<SendTransaction::Request, SendTransaction::Response>(std::bind(&PaymentServiceJsonRpcServer::handleSendTransaction, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createDelayedTransaction", jsonHandler<CreateDelayedTransaction::Request, CreateDelayedTransaction::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateDelayedTransaction, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getDelayedTransactionHashes", jsonHandler<GetDelayedTransactionHashes::Request, GetDelayedTransactionHashes::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetDelayedTransactionHashes, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("deleteDelayedTransaction", jsonHandler<DeleteDelayedTransaction::Request, DeleteDelayedTransaction::Response>(std::bind(&PaymentServiceJsonRpcServer::handleDeleteDelayedTransaction, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("sendDelayedTransaction", jsonHandler<SendDelayedTransaction::Request, SendDelayedTransaction::Response>(std::bind(&PaymentServiceJsonRpcServer::handleSendDelayedTransaction, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getViewKey", jsonHandler<GetViewKey::Request, GetViewKey::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetViewKey, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getStatus", jsonHandler<GetStatus::Request, GetStatus::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetStatus, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getAddresses", jsonHandler<GetAddresses::Request, GetAddresses::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetAddresses, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createDeposit", jsonHandler<CreateDeposit::Request, CreateDeposit::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateDeposit, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createBurnDeposit", jsonHandler<CreateBurnDeposit::Request, CreateBurnDeposit::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateBurnDeposit, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createBurnDepositWithProof", jsonHandler<CreateBurnDepositWithProof::Request, CreateBurnDepositWithProof::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateBurnDepositWithProof, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createBurnDeposit8000", jsonHandler<CreateBurnDeposit8000::Request, CreateBurnDeposit8000::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateBurnDeposit8000, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("createBurnDeposit8000WithProof", jsonHandler<CreateBurnDeposit8000WithProof::Request, CreateBurnDeposit8000WithProof::Response>(std::bind(&PaymentServiceJsonRpcServer::handleCreateBurnDeposit8000WithProof, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("generateBurnProofDataFile", jsonHandler<GenerateBurnProofDataFile::Request, GenerateBurnProofDataFile::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGenerateBurnProofDataFile, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("generateBurnProofDataFileAuto", jsonHandler<GenerateBurnProofDataFileAuto::Request, GenerateBurnProofDataFileAuto::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGenerateBurnProofDataFileAuto, this, std::placeholders::_1, std::placeholders::_2)));

  handlers.emplace("giftDeposit", jsonHandler<GiftDeposit::Request, GiftDeposit::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGiftDeposit, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("withdrawDeposit", jsonHandler<WithdrawDeposit::Request, WithdrawDeposit::Response>(std::bind(&PaymentServiceJsonRpcServer::handleWithdrawDeposit, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getMessagesFromExtra", jsonHandler<GetMessagesFromExtra::Request, GetMessagesFromExtra::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetMessagesFromExtra, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getDeposit", jsonHandler<GetDeposit::Request, GetDeposit::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetDeposit, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("getEternalFlame", jsonHandler<GetEthernalXFG::Request, GetEthernalXFG::Response>(std::bind(&PaymentServiceJsonRpcServer::handleGetEthernalXFG, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("estimateFusion", jsonHandler<EstimateFusion::Request, EstimateFusion::Response>(std::bind(&PaymentServiceJsonRpcServer::handleEstimateFusion, this, std::placeholders::_1, std::placeholders::_2)));
  handlers.emplace("sendFusionTransaction", jsonHandler<SendFusionTransaction::Request, SendFusionTransaction::Response>(std::bind(&PaymentServiceJsonRpcServer::handleSendFusionTransaction, this, std::placeholders::_1, std::placeholders::_2)));
}

void PaymentServiceJsonRpcServer::processJsonRpcRequest(const Common::JsonValue& req, Common::JsonValue& resp) {
  try {
    prepareJsonResponse(req, resp);

    if (!req.contains("method")) {
      logger(Logging::WARNING) << "Field \"method\" is not found in json request: " << req;
      makeGenericErrorReponse(resp, "Invalid Request", -3600);
      return;
    }

    if (!req("method").isString()) {
      logger(Logging::WARNING) << "Field \"method\" is not a string type: " << req;
      makeGenericErrorReponse(resp, "Invalid Request", -3600);
      return;
    }

    std::string method = req("method").getString();

    auto it = handlers.find(method);
    if (it == handlers.end()) {
      logger(Logging::WARNING) << "Requested method not found: " << method;
      makeMethodNotFoundResponse(resp);
      return;
    }

    logger(Logging::DEBUGGING) << method << " request came";

    Common::JsonValue params(Common::JsonValue::OBJECT);
    if (req.contains("params")) {
      params = req("params");
    }

    it->second(params, resp);
  } catch (std::exception& e) {
    logger(Logging::WARNING) << "Error occurred while processing JsonRpc request: " << e.what();
    makeGenericErrorReponse(resp, e.what());
  }
}

std::error_code PaymentServiceJsonRpcServer::handleReset(const Reset::Request& request, Reset::Response& response) {
  if (request.viewSecretKey.empty()) {
    if (request.scanHeight != std::numeric_limits<uint32_t>::max()) {
      return service.resetWallet(request.scanHeight);
    } else {
      return service.resetWallet();
    }
  } else {
    if (request.scanHeight != std::numeric_limits<uint32_t>::max()) {
      return service.replaceWithNewWallet(request.viewSecretKey);
    } else {
      return service.replaceWithNewWallet(request.viewSecretKey);
    }
  }
}

std::error_code PaymentServiceJsonRpcServer::handleCreateAddress(const CreateAddress::Request& request, CreateAddress::Response& response) {
  if (request.spendSecretKey.empty() && request.spendPublicKey.empty()) {
    return service.createAddress(response.address);
  } else if (!request.spendSecretKey.empty()) {
    return service.createAddress(request.spendSecretKey, response.address);
  } else {
    return service.createTrackingAddress(request.spendPublicKey, response.address);
  }
}

std::error_code PaymentServiceJsonRpcServer::handleExportWallet(const ExportWallet::Request &request, ExportWallet::Response &response)
{
  return service.exportWallet(request.exportFilename);
}

std::error_code PaymentServiceJsonRpcServer::handleExportWalletKeys(const ExportWalletKeys::Request &request, ExportWalletKeys::Response &response)
{
  return service.exportWalletKeys(request.exportFilename);
}

std::error_code PaymentServiceJsonRpcServer::handleCreateAddressList(const CreateAddressList::Request& request, CreateAddressList::Response& response) {
  return service.createAddressList(request.spendSecretKeys, request.reset, response.addresses);
}

std::error_code PaymentServiceJsonRpcServer::handleSave(const Save::Request& /*request*/, Save::Response& /*response*/)
{
  return service.saveWalletNoThrow();
}


std::error_code PaymentServiceJsonRpcServer::handleCreateIntegrated(const CreateIntegrated::Request& request, CreateIntegrated::Response& response)
{
  return service.createIntegratedAddress(request, response.integrated_address);
}

std::error_code PaymentServiceJsonRpcServer::handleSplitIntegrated(const SplitIntegrated::Request& request, SplitIntegrated::Response& response)
{
  return service.splitIntegratedAddress(request, response.address, response.payment_id);
}
std::error_code PaymentServiceJsonRpcServer::handleDeleteAddress(const DeleteAddress::Request& request, DeleteAddress::Response& response) {
  return service.deleteAddress(request.address);
}

std::error_code PaymentServiceJsonRpcServer::handleGetSpendKeys(const GetSpendKeys::Request& request, GetSpendKeys::Response& response) {
  return service.getSpendkeys(request.address, response.spendPublicKey, response.spendSecretKey);
}

std::error_code PaymentServiceJsonRpcServer::handleGetBalance(const GetBalance::Request& request, GetBalance::Response& response) {
  if (!request.address.empty()) {
    return service.getBalance(request.address, response.availableBalance, response.lockedAmount, response.lockedDepositBalance, response.unlockedDepositBalance);
  } else {
    return service.getBalance(response.availableBalance, response.lockedAmount, response.lockedDepositBalance, response.unlockedDepositBalance);
  }
}

std::error_code PaymentServiceJsonRpcServer::handleGetBlockHashes(const GetBlockHashes::Request& request, GetBlockHashes::Response& response) {
  return service.getBlockHashes(request.firstBlockIndex, request.blockCount, response.blockHashes);
}

std::error_code PaymentServiceJsonRpcServer::handleGetTransactionHashes(const GetTransactionHashes::Request& request, GetTransactionHashes::Response& response) {
  if (!request.blockHash.empty()) {
    return service.getTransactionHashes(request.addresses, request.blockHash, request.blockCount, request.paymentId, response.items);
  } else {
    return service.getTransactionHashes(request.addresses, request.firstBlockIndex, request.blockCount, request.paymentId, response.items);
  }
}

std::error_code PaymentServiceJsonRpcServer::handleGetTransactions(const GetTransactions::Request& request, GetTransactions::Response& response) {
  if (!request.blockHash.empty()) {
    return service.getTransactions(request.addresses, request.blockHash, request.blockCount, request.paymentId, response.items);
  } else {
    return service.getTransactions(request.addresses, request.firstBlockIndex, request.blockCount, request.paymentId, response.items);
  }
}

std::error_code PaymentServiceJsonRpcServer::handleGetUnconfirmedTransactionHashes(const GetUnconfirmedTransactionHashes::Request& request, GetUnconfirmedTransactionHashes::Response& response) {
  return service.getUnconfirmedTransactionHashes(request.addresses, response.transactionHashes);
}

std::error_code PaymentServiceJsonRpcServer::handleGetTransaction(const GetTransaction::Request& request, GetTransaction::Response& response) {
  return service.getTransaction(request.transactionHash, response.transaction);
}

std::error_code PaymentServiceJsonRpcServer::handleSendTransaction(const SendTransaction::Request& request, SendTransaction::Response& response) {
  return service.sendTransaction(request, response.transactionHash, response.transactionSecretKey);
}

std::error_code PaymentServiceJsonRpcServer::handleCreateDelayedTransaction(const CreateDelayedTransaction::Request& request, CreateDelayedTransaction::Response& response) {
  return service.createDelayedTransaction(request, response.transactionHash);
}

std::error_code PaymentServiceJsonRpcServer::handleGetDelayedTransactionHashes(const GetDelayedTransactionHashes::Request& request, GetDelayedTransactionHashes::Response& response) {
  return service.getDelayedTransactionHashes(response.transactionHashes);
}

std::error_code PaymentServiceJsonRpcServer::handleDeleteDelayedTransaction(const DeleteDelayedTransaction::Request& request, DeleteDelayedTransaction::Response& response) {
  return service.deleteDelayedTransaction(request.transactionHash);
}

std::error_code PaymentServiceJsonRpcServer::handleSendDelayedTransaction(const SendDelayedTransaction::Request& request, SendDelayedTransaction::Response& response) {
  return service.sendDelayedTransaction(request.transactionHash);
}

std::error_code PaymentServiceJsonRpcServer::handleGetViewKey(const GetViewKey::Request& request, GetViewKey::Response& response) {
  return service.getViewKey(response.viewSecretKey);
}

std::error_code PaymentServiceJsonRpcServer::handleGetStatus(const GetStatus::Request& request, GetStatus::Response& response) {
  return service.getStatus(response.blockCount, response.knownBlockCount, response.lastBlockHash, response.peerCount, response.depositCount, response.transactionCount, response.addressCount, response.networkId);
}

std::error_code PaymentServiceJsonRpcServer::handleCreateDeposit(const CreateDeposit::Request& request, CreateDeposit::Response& response) {
  // Generate appropriate commitment based on deposit term
  CryptoNote::DepositCommitment commitment;

  if (!request.metadata.empty()) {
    std::vector<uint8_t> metadata;
    if (!Common::fromHex(request.metadata, metadata)) {
      return make_error_code(CryptoNote::error::INTERNAL_WALLET_ERROR);
    }
    commitment.metadata = metadata;
  }

  // Check if this is a burn deposit (FOREVER term)
  bool isBurnDeposit = (request.term == CryptoNote::parameters::DEPOSIT_TERM_FOREVER);
  response.isBurnDeposit = isBurnDeposit;
  // response.useStagedUnlock = request.useStagedUnlock;

  // Generate commitment based on deposit type
  if (isBurnDeposit) {
    commitment = CryptoNote::DepositCommitmentGenerator::generateHeatCommitment(
      request.amount, commitment.metadata);
  } else {
    commitment = CryptoNote::DepositCommitmentGenerator::generateYieldCommitment(
      request.term, request.amount, commitment.metadata);
  }

  // Calculate transaction fees
  uint64_t baseFee = 800000; // 0.008 XFG base transaction fee
  response.transactionFee = baseFee;
  // response.totalFees = request.useStagedUnlock ? (baseFee * 5) : baseFee; // 5 transactions for staged unlock

  return service.createDeposit(request.amount, request.term, request.sourceAddress, response.transactionHash, commitment/*, request.useStagedUnlock*/);
}

std::error_code PaymentServiceJsonRpcServer::handleCreateBurnDeposit(const CreateBurnDeposit::Request& request, CreateBurnDeposit::Response& response) {
  // Create burn deposit with FOREVER term
  uint64_t term = CryptoNote::parameters::DEPOSIT_TERM_BURN;  // 4294967295 (FOREVER)

  // Enforce standard 0.8 XFG burn amount
  uint64_t amount = (request.amount == 0) ? CryptoNote::parameters::BURN_DEPOSIT_STANDARD_AMOUNT : request.amount;
  if (amount != CryptoNote::parameters::BURN_DEPOSIT_STANDARD_AMOUNT) {
    logger(Logging::WARNING) << "Invalid standard burn amount: " << amount;
    return make_error_code(CryptoNote::error::INTERNAL_WALLET_ERROR);
  }

  // 🔥 ADD: Include network ID in metadata for STARK validation
  std::string networkId = "93385046440755750514194170694064996624";
  std::string enhancedMetadata = request.metadata.empty() ?
      "network_id:" + networkId :
      request.metadata + "|network_id:" + networkId;

  // Generate HEAT commitment with secret for local storage
  auto [commitment, secret] = CryptoNote::DepositCommitmentGenerator::generateHeatCommitmentWithSecret(
    amount, std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()));

  std::error_code result = service.createDeposit(amount, term, request.sourceAddress, response.transactionHash, commitment);

  if (!result) {
    response.term = term;  // Always 4294967295
    response.heatAmount = CryptoNote::DepositCommitmentGenerator::convertXfgToHeat(amount);  // 0.8 XFG = 8M HEAT

    // 🔥 ADD: Store secret locally (never on blockchain)
    service.storeBurnDepositSecret(response.transactionHash, secret, amount, std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()));

    // 🔥 ADD: Automatically generate BPDF for backup
    try {
      std::string bpdfDir = service.getDefaultWalletPath() + "/bpdf";
      std::string bpdfPath = bpdfDir + "/" + response.transactionHash + ".json";

      // Create BPDF directory if it doesn't exist
      Common::createDirectory(bpdfDir);

      std::string ethAddress = CryptoNote::BurnTransactionHandler::extractEthereumAddress(std::string(enhancedMetadata.begin(), enhancedMetadata.end()));
      std::string networkId = "93385046440755750514194170694064996624";

      // Only generate BPDF if we have an Ethereum address
      if (!ethAddress.empty()) {
        std::error_code bpdfResult = service.generateBurnProofDataFile(
          response.transactionHash,
          ethAddress,
          bpdfPath,
          secret,
          amount,
          std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()),
          networkId);

        if (bpdfResult) {
          logger(Logging::WARNING) << "Failed to automatically generate BPDF for burn transaction " << response.transactionHash << ": " << bpdfResult.message();
        } else {
          logger(Logging::INFO) << "Successfully generated BPDF for burn transaction " << response.transactionHash;
        }
      } else {
        logger(Logging::DEBUGGING) << "No Ethereum address found in metadata for burn transaction " << response.transactionHash << ", skipping BPDF generation";
      }
    } catch (const std::exception& e) {
      logger(Logging::WARNING) << "Exception while generating BPDF for burn transaction " << response.transactionHash << ": " << e.what();
    }
  }

  return result;
}

std::error_code PaymentServiceJsonRpcServer::handleCreateBurnDepositWithProof(const CreateBurnDepositWithProof::Request& request, CreateBurnDepositWithProof::Response& response) {
  // Create burn deposit with FOREVER term
  uint64_t term = CryptoNote::parameters::DEPOSIT_TERM_BURN;  // 4294967295 (FOREVER)

  // Enforce standard 0.8 XFG burn amount
  uint64_t amount = (request.amount == 0) ? CryptoNote::parameters::BURN_DEPOSIT_STANDARD_AMOUNT : request.amount;
  if (amount != CryptoNote::parameters::BURN_DEPOSIT_STANDARD_AMOUNT) {
    logger(Logging::WARNING) << "Invalid standard burn amount: " << amount;
    return make_error_code(CryptoNote::error::INTERNAL_WALLET_ERROR);
  }

  //  ADD: Include network ID in metadata for STARK validation
  std::string networkId = "93385046440755750514194170694064996624";
  std::string enhancedMetadata = request.metadata.empty() ?
      "network_id:" + networkId :
      request.metadata + "|network_id:" + networkId;

  // Generate HEAT commitment with enhanced metadata and also get secret
  auto [commitment, secret] = CryptoNote::DepositCommitmentGenerator::generateHeatCommitmentWithSecret(
    amount, std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()));

  std::error_code result = service.createDeposit(amount, term, request.sourceAddress, response.transactionHash, commitment);

  if (!result) {
    response.term = term;  // Always 4294967295
    response.heatAmount = CryptoNote::DepositCommitmentGenerator::convertXfgToHeat(amount);  // 0.8 XFG = 8M HEAT

    //  ADD: Generate BPDF with network ID
    std::string outputPath = service.getDefaultWalletPath() + "/bpdf/" + response.transactionHash + ".json";
    std::error_code bpdfResult = service.generateBurnProofDataFile(
      response.transactionHash,
      request.recipientAddress,
      outputPath,
      secret,
      amount,
      std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()),
      networkId
    );

    if (!bpdfResult) {
      response.burnProofDataFile = outputPath;
    }
    response.networkId = networkId;
  }

  return result;
}

std::error_code PaymentServiceJsonRpcServer::handleCreateBurnDeposit8000(const CreateBurnDeposit8000::Request& request, CreateBurnDeposit8000::Response& response) {
  // Create burn deposit with FOREVER term and fixed 800 XFG amount
  uint64_t term = CryptoNote::parameters::DEPOSIT_TERM_BURN;  // 4294967295 (FOREVER)
  uint64_t amount = CryptoNote::parameters::BURN_DEPOSIT_LARGE_AMOUNT;  // 800 XFG

  //  ADD: Include network ID in metadata for STARK validation
  std::string networkId = "93385046440755750514194170694064996624";
  std::string enhancedMetadata = request.metadata.empty() ?
      "network_id:" + networkId :
      request.metadata + "|network_id:" + networkId;

  // Generate HEAT commitment with secret for local storage
  auto [commitment, secret] = CryptoNote::DepositCommitmentGenerator::generateHeatCommitmentWithSecret(
    amount, std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()));

  std::error_code result = service.createDeposit(amount, term, request.sourceAddress, response.transactionHash, commitment);

  if (!result) {
    response.term = term;  // Always 4294967295
    response.heatAmount = CryptoNote::DepositCommitmentGenerator::convertXfgToHeat(amount);  // 800 XFG = 8,000,000,000 HEAT

    // 🔥 ADD: Store secret locally (never on blockchain)
    service.storeBurnDepositSecret(response.transactionHash, secret, amount, std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()));

    // 🔥 ADD: Automatically generate BPDF for backup
    try {
      std::string bpdfDir = service.getDefaultWalletPath() + "/bpdf";
      std::string bpdfPath = bpdfDir + "/" + response.transactionHash + ".json";

      // Create BPDF directory if it doesn't exist
      Common::createDirectory(bpdfDir);

      std::string ethAddress = CryptoNote::BurnTransactionHandler::extractEthereumAddress(std::string(enhancedMetadata.begin(), enhancedMetadata.end()));
      std::string networkId = "93385046440755750514194170694064996624";

      // Only generate BPDF if we have an Ethereum address
      if (!ethAddress.empty()) {
        std::error_code bpdfResult = service.generateBurnProofDataFile(
          response.transactionHash,
          ethAddress,
          bpdfPath,
          secret,
          amount,
          std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()),
          networkId);

        if (bpdfResult) {
          logger(Logging::WARNING) << "Failed to automatically generate BPDF for burn transaction " << response.transactionHash << ": " << bpdfResult.message();
        } else {
          logger(Logging::INFO) << "Successfully generated BPDF for burn transaction " << response.transactionHash;
        }
      } else {
        logger(Logging::DEBUGGING) << "No Ethereum address found in metadata for burn transaction " << response.transactionHash << ", skipping BPDF generation";
      }
    } catch (const std::exception& e) {
      logger(Logging::WARNING) << "Exception while generating BPDF for burn transaction " << response.transactionHash << ": " << e.what();
    }
  }

  return result;
}

std::error_code PaymentServiceJsonRpcServer::handleCreateBurnDeposit8000WithProof(const CreateBurnDeposit8000WithProof::Request& request, CreateBurnDeposit8000WithProof::Response& response) {
  // Create burn deposit with FOREVER term and fixed 800 XFG amount
  uint64_t term = CryptoNote::parameters::DEPOSIT_TERM_BURN;  // 4294967295 (FOREVER)
  uint64_t amount = CryptoNote::parameters::BURN_DEPOSIT_LARGE_AMOUNT;  // 800 XFG

  //  ADD: Include network ID in metadata for STARK validation
  std::string networkId = "93385046440755750514194170694064996624";
  std::string enhancedMetadata = request.metadata.empty() ?
      "network_id:" + networkId :
      request.metadata + "|network_id:" + networkId;

  // Generate HEAT commitment with enhanced metadata and also get secret
  auto [commitment, secret] = CryptoNote::DepositCommitmentGenerator::generateHeatCommitmentWithSecret(
    amount, std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()));

  std::error_code result = service.createDeposit(amount, term, request.sourceAddress, response.transactionHash, commitment);

  if (!result) {
    response.term = term;  // Always 4294967295
    response.heatAmount = CryptoNote::DepositCommitmentGenerator::convertXfgToHeat(amount);  // 800 XFG = 8,000,000,000 HEAT

    // 🔥 ADD: Generate BPDF with network ID
    std::string outputPath = service.getDefaultWalletPath() + "/bpdf/" + response.transactionHash + ".json";
    std::error_code bpdfResult = service.generateBurnProofDataFile(
      response.transactionHash,
      request.recipientAddress,
      outputPath,
      secret,
      amount,
      std::vector<uint8_t>(enhancedMetadata.begin(), enhancedMetadata.end()),
      networkId
    );

    if (!bpdfResult) {
      response.burnProofDataFile = outputPath;
    }
    response.networkId = networkId;
  }

  return result;
}

std::error_code PaymentServiceJsonRpcServer::handleGenerateBurnProofDataFile(const GenerateBurnProofDataFile::Request& request, GenerateBurnProofDataFile::Response& response) {
  try {
    //  MANUAL: Generate BPDF manually (user provides secret separately)
    std::string networkId = "93385046440755750514194170694064996624";

    // 🔥 ADD: Use default wallet path if outputPath is empty
    std::string outputPath = request.outputPath;
    if (outputPath.empty()) {
      outputPath = service.getDefaultWalletPath() + "/bpdf/" + request.transactionHash + ".json";
    }

    // For manual mode, we need to get transaction data and extract commitment
    // User will provide secret separately (not through RPC for security)
    std::error_code bpdfResult = service.generateBurnProofDataFile(
      request.transactionHash,
      request.recipientAddress,
      outputPath,
      networkId
    );

    if (!bpdfResult) {
      response.burnProofDataFile = outputPath;
    }
    response.success = true;
    response.networkId = networkId;

    return std::error_code();
  } catch (std::exception& e) {
    response.success = false;
    response.errorMessage = "Error generating BPDF manually: " + std::string(e.what());
    return std::error_code();
  }
}

std::error_code PaymentServiceJsonRpcServer::handleGenerateBurnProofDataFileAuto(const GenerateBurnProofDataFileAuto::Request& request, GenerateBurnProofDataFileAuto::Response& response) {
  try {
    // 🔥 AUTO: Generate BPDF automatically with local secret retrieval
    std::string networkId = "93385046440755750514194170694064996624";

    // Retrieve secret from local storage
    Crypto::SecretKey secret;
    uint64_t amount;
    std::vector<uint8_t> metadata;

    if (!service.getBurnDepositSecret(request.transactionHash, secret, amount, metadata)) {
      response.success = false;
      response.errorMessage = "Burn deposit secret not found for transaction: " + request.transactionHash;
      return std::error_code();
    }

    // 🔥 ADD: Use default wallet path if outputPath is empty
    std::string outputPath = request.outputPath;
    if (outputPath.empty()) {
      outputPath = service.getDefaultWalletPath() + "/bpdf/" + request.transactionHash + ".json";
    }

    // Generate BPDF using local secret
    std::error_code bpdfResult = service.generateBurnProofDataFile(
      request.transactionHash,
      request.recipientAddress,
      outputPath,
      secret,
      amount,
      metadata,
      networkId
    );

    if (!bpdfResult) {
      response.burnProofDataFile = outputPath;
    }
    response.success = true;
    response.networkId = networkId;

    // Mark BPDF as generated
    service.markBurnDepositBPDFGenerated(request.transactionHash);

    return std::error_code();
  } catch (std::exception& e) {
    response.success = false;
    response.errorMessage = "Error generating BPDF: " + std::string(e.what());
    return std::error_code();
  }
}





std::error_code PaymentServiceJsonRpcServer::handleWithdrawDeposit(const WithdrawDeposit::Request &request, WithdrawDeposit::Response &response)
{
  return service.withdrawDeposit(request.depositId, response.transactionHash);
}

std::error_code PaymentServiceJsonRpcServer::handleGiftDeposit(const GiftDeposit::Request& request, GiftDeposit::Response& response) {
  return service.giftDeposit(request.amount, request.term, request.sourceAddress, request.destinationAddress, response.transactionHash);
}

std::error_code PaymentServiceJsonRpcServer::handleGetDeposit(const GetDeposit::Request& request, GetDeposit::Response& response) {
  std::error_code result = service.getDeposit(request.depositId, response.amount, response.term, response.interest, response.creatingTransactionHash, response.spendingTransactionHash, response.locked, response.height, response.unlockHeight, response.address);

  if (!result) {
    // Calculate transaction fees
    uint64_t baseFee = 800000; // 0.008 XFG base transaction fee
    response.transactionFee = baseFee;
    // response.totalFees = response.useStagedUnlock ? (baseFee * 4) : baseFee;
  }

  return result;
}

std::error_code PaymentServiceJsonRpcServer::handleGetAddresses(const GetAddresses::Request& request, GetAddresses::Response& response) {
  return service.getAddresses(response.addresses);
}

std::error_code PaymentServiceJsonRpcServer::handleGetMessagesFromExtra(const GetMessagesFromExtra::Request& request, GetMessagesFromExtra::Response& response) {
  return service.getMessagesFromExtra(request.extra, response.messages);
}

std::error_code PaymentServiceJsonRpcServer::handleEstimateFusion(const EstimateFusion::Request& request, EstimateFusion::Response& response) {
  return service.estimateFusion(request.threshold, request.addresses, response.fusionReadyCount, response.totalOutputCount);
}


std::error_code PaymentServiceJsonRpcServer::handleSendFusionTransaction(const SendFusionTransaction::Request& request, SendFusionTransaction::Response& response) {
  return service.sendFusionTransaction(request.threshold, request.anonymity, request.addresses, request.destinationAddress, response.transactionHash);
}



std::error_code PaymentServiceJsonRpcServer::handleGetEthernalXFG(const GetEthernalXFG::Request& request, GetEthernalXFG::Response& response)
{
  uint64_t eternal;
  service.getEternalFlame(eternal);
  response.ethernalXFG = eternal;
  return std::error_code();
}




}

