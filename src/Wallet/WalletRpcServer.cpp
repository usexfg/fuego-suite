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

#include "WalletRpcServer.h"
#include "Wallet/WalletGreen.h"
#include "IWallet.h"

#include <fstream>
#include <ctime>
#include <cstring>
#include "Common/CommandLine.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Account.h"
#include "crypto/hash.h"
#include "crypto/crypto.h"
#include "SwapDaemon/SwapTypes.h"
#include "SwapDaemon/AdaptorSwap.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "CryptoNoteCore/CryptoNoteBasicImpl.h"
#include "CryptoNoteCore/DepositCommitment.h"
#include "WalletLegacy/WalletHelper.h"
#include "Common/Base58.h"
#include "Common/CommandLine.h"
#include "Common/SignalHandler.h"
#include "Common/StringTools.h"
#include "Common/PathTools.h"
#include "Common/Util.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolHandler.h"
#include "Common/CommandLine.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Account.h"
#include "crypto/hash.h"
#include "WalletLegacy/WalletHelper.h"
#include "CryptoNoteConfig.h"
// #include "wallet_errors.h"

#include "Rpc/JsonRpc.h"

using namespace Logging;
using namespace CryptoNote;

namespace Tools {

const command_line::arg_descriptor<uint16_t> wallet_rpc_server::arg_rpc_bind_port = { "rpc-bind-port", "Starts wallet as rpc server for wallet operations, sets bind port for server", 0, true };
const command_line::arg_descriptor<std::string> wallet_rpc_server::arg_rpc_bind_ip = { "rpc-bind-ip", "Specify ip to bind rpc server", "127.0.0.1" };
const command_line::arg_descriptor<std::string> wallet_rpc_server::arg_rpc_user = { "rpc-user", "Username to use the rpc server. If authorization is not required, leave it empty", "" };
const command_line::arg_descriptor<std::string> wallet_rpc_server::arg_rpc_password = { "rpc-password", "Password to use the rpc server. If authorization is not required, leave it empty", "" };

void wallet_rpc_server::init_options(boost::program_options::options_description& desc) {
  command_line::add_arg(desc, arg_rpc_bind_ip);
  command_line::add_arg(desc, arg_rpc_bind_port);
  command_line::add_arg(desc, arg_rpc_user);
  command_line::add_arg(desc, arg_rpc_password);
}
//------------------------------------------------------------------------------------------------------------------------------
wallet_rpc_server::wallet_rpc_server(
  System::Dispatcher& dispatcher,
  Logging::ILogger& log,
  CryptoNote::IWalletLegacy&w,
  CryptoNote::INode& n,
  CryptoNote::Currency& currency,
  const std::string& walletFile)
  :
  HttpServer(dispatcher, log),
  logger(log, "WalletRpc"),
  m_dispatcher(dispatcher),
  m_stopComplete(dispatcher),
  m_wallet(w),
  m_node(n),
  m_currency(currency),
  m_walletFilename(walletFile) {
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::run() {
  start(m_bind_ip, m_port, m_rpcUser, m_rpcPassword);
  m_stopComplete.wait();
  return true;
}

void wallet_rpc_server::send_stop_signal() {
  m_dispatcher.remoteSpawn([this] {
    std::cout << "wallet_rpc_server::send_stop_signal()" << std::endl;
    stop();
    m_stopComplete.set();
  });
}

//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::handle_command_line(const boost::program_options::variables_map& vm) {
  m_bind_ip = command_line::get_arg(vm, arg_rpc_bind_ip);
  m_port = command_line::get_arg(vm, arg_rpc_bind_port);
  m_rpcUser = command_line::get_arg(vm, arg_rpc_user);
  m_rpcPassword = command_line::get_arg(vm, arg_rpc_password);
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::init(const boost::program_options::variables_map& vm) {
  if (!handle_command_line(vm)) {
    logger(ERROR) << "Failed to process command line in wallet_rpc_server";
    return false;
  }

  return true;
}

void wallet_rpc_server::processRequest(const CryptoNote::HttpRequest& request, CryptoNote::HttpResponse& response) {

  using namespace CryptoNote::JsonRpc;

  JsonRpcRequest jsonRequest;
  JsonRpcResponse jsonResponse;

  try {
    jsonRequest.parseRequest(request.getBody());
    jsonResponse.setId(jsonRequest.getId());

    static std::unordered_map<std::string, JsonMemberMethod> s_methods = {
      { "create_integrated", makeMemberMethod(&wallet_rpc_server::on_create_integrated) },  
      { "getbalance", makeMemberMethod(&wallet_rpc_server::on_getbalance) },
      { "get_address", makeMemberMethod(&wallet_rpc_server::on_get_address) },
      { "sign_offer",    makeMemberMethod(&wallet_rpc_server::on_sign_offer)    },
      { "initiate_swap", makeMemberMethod(&wallet_rpc_server::on_initiate_swap) },
      { "complete_swap", makeMemberMethod(&wallet_rpc_server::on_complete_swap) },
      { "refund_swap",   makeMemberMethod(&wallet_rpc_server::on_refund_swap)   },
      { "transfer", makeMemberMethod(&wallet_rpc_server::on_transfer) },
      { "store", makeMemberMethod(&wallet_rpc_server::on_store) },
      { "get_messages", makeMemberMethod(&wallet_rpc_server::on_get_messages) },
      { "get_payments", makeMemberMethod(&wallet_rpc_server::on_get_payments) },
      { "get_transfers", makeMemberMethod(&wallet_rpc_server::on_get_transfers) },
      { "get_height", makeMemberMethod(&wallet_rpc_server::on_get_height) },
      { "get_outputs", makeMemberMethod(&wallet_rpc_server::on_get_outputs) },
      { "get_tx_proof"     , makeMemberMethod(&wallet_rpc_server::on_get_tx_proof)      },
      { "get_reserve_proof", makeMemberMethod(&wallet_rpc_server::on_get_reserve_proof) },      
      { "optimize", makeMemberMethod(&wallet_rpc_server::on_optimize) },
      { "estimate_fusion"  , makeMemberMethod(&wallet_rpc_server::on_estimate_fusion) },
      { "send_fusion"      , makeMemberMethod(&wallet_rpc_server::on_send_fusion) },
      { "reset", makeMemberMethod(&wallet_rpc_server::on_reset) },
      // Phase 7: CD / COLD wallet RPC bridges
      { "list_cds",           makeMemberMethod(&wallet_rpc_server::on_list_cds) },
      { "create_cd",          makeMemberMethod(&wallet_rpc_server::on_create_cd) },
      { "withdraw_cd",        makeMemberMethod(&wallet_rpc_server::on_withdraw_cd) },
      { "rollover_cd",        makeMemberMethod(&wallet_rpc_server::on_rollover_cd) },
      { "estimate_cd_yield",  makeMemberMethod(&wallet_rpc_server::on_estimate_cd_yield) }
    };

    auto it = s_methods.find(jsonRequest.getMethod());
    if (it == s_methods.end()) {
      throw JsonRpcError(errMethodNotFound);
    }

    it->second(this, jsonRequest, jsonResponse);

  } catch (const JsonRpcError& err) {
    jsonResponse.setError(err);
  } catch (const std::exception& e) {
    jsonResponse.setError(JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, e.what()));
  }

  response.setBody(jsonResponse.getBody());
}

//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_getbalance(const wallet_rpc::COMMAND_RPC_GET_BALANCE::request& req, wallet_rpc::COMMAND_RPC_GET_BALANCE::response& res) {
  res.locked_amount = m_wallet.pendingBalance();
  res.available_balance = m_wallet.actualBalance();
  res.balance = res.locked_amount + res.available_balance;
  res.unlocked_balance = res.available_balance;
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_get_address(const wallet_rpc::COMMAND_RPC_GET_ADDRESS::request& req, wallet_rpc::COMMAND_RPC_GET_ADDRESS::response& res) {
  res.address = m_wallet.getAddress();
  res.status = WALLET_RPC_STATUS_OK;
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_sign_offer(const wallet_rpc::COMMAND_RPC_SIGN_OFFER::request& req, wallet_rpc::COMMAND_RPC_SIGN_OFFER::response& res) {
  // Get wallet keys
  CryptoNote::AccountKeys keys;
  m_wallet.getAccountKeys(keys);

  // Build offer ID: SHA-256 of (spendPubKey || pair || xfgAmount || rateNum || timestamp)
  uint64_t ts = static_cast<uint64_t>(std::time(nullptr));

  struct OfferIdInput {
    uint8_t  spendPubKey[32];
    uint8_t  pair;
    uint8_t  isSell;
    uint64_t xfgAmount;
    uint64_t rateNum;
    uint64_t timestamp;
  } offerIdInput;

  std::memcpy(offerIdInput.spendPubKey, &keys.address.spendPublicKey, 32);
  offerIdInput.pair      = req.pair;
  offerIdInput.isSell    = req.isSell ? 1 : 0;
  offerIdInput.xfgAmount = req.xfgAmount;
  offerIdInput.rateNum   = req.rateNum;
  offerIdInput.timestamp = ts;

  Crypto::Hash offerIdHash;
  Crypto::cn_fast_hash(&offerIdInput, sizeof(offerIdInput), offerIdHash);

  // Sign the offer ID hash with spend key
  Crypto::Signature sig;
  Crypto::generate_signature(offerIdHash, keys.address.spendPublicKey, keys.spendSecretKey, sig);

  res.offerId     = Common::podToHex(offerIdHash);
  res.makerPubKey = Common::podToHex(keys.address.spendPublicKey);
  res.signature   = Common::podToHex(sig);
  res.timestamp   = ts;
  res.status      = WALLET_RPC_STATUS_OK;
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_initiate_swap(const wallet_rpc::COMMAND_RPC_INITIATE_SWAP::request& req, wallet_rpc::COMMAND_RPC_INITIATE_SWAP::response& res) {
  try {
    XfgSwap::SwapParams params;

    // Parse pair
    std::string pairUpper = req.pair;
    std::transform(pairUpper.begin(), pairUpper.end(), pairUpper.begin(), ::toupper);
    params.pair = XfgSwap::swapPairFromString(pairUpper);

    // Parse role
    std::string roleLower = req.role.empty() ? "alice" : req.role;
    std::transform(roleLower.begin(), roleLower.end(), roleLower.begin(), ::tolower);
    params.role = (roleLower == "bob") ? XfgSwap::SwapRole::BOB : XfgSwap::SwapRole::ALICE;

    params.xfgAmount = req.xfgAmount;

    // Parse peer pubkey
    if (!Common::podFromHex(req.peerPubKey, params.peerSwapPubKey)) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Invalid peerPubKey hex");
    }

    // Step 1: Generate our swap keypair
    XfgSwap::adaptor_generate_keys(params);

    // Step 2: Musig2 key aggregation
    if (!XfgSwap::adaptor_key_aggregate(params)) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Key aggregation failed (invalid peer pubkey?)");
    }

    // Step 3: Generate nonces
    XfgSwap::adaptor_nonce_generate(params);

    // Step 4 (Bob only): Generate adaptor secret + DLEQ proof
    if (params.role == XfgSwap::SwapRole::BOB) {
      Crypto::PublicKey dleqBase;
      Crypto::hash_data_to_ec(reinterpret_cast<const uint8_t*>(&params.escrowPubKey), 32, dleqBase);
      if (!XfgSwap::adaptor_generate_adaptor(params, dleqBase)) {
        throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Failed to generate adaptor point");
      }
    }

    // Derive swap ID from escrow key hash
    Crypto::Hash swapIdHash;
    Crypto::cn_fast_hash(&params.escrowPubKey, sizeof(params.escrowPubKey), swapIdHash);
    params.swapId = Common::podToHex(swapIdHash).substr(0, 16);

    res.swapId    = params.swapId;
    res.ourPubKey = Common::podToHex(params.ourSwapPubKey);
    res.nonce0    = Common::podToHex(params.musig2.ourPubNonce.R[0]);
    res.nonce1    = Common::podToHex(params.musig2.ourPubNonce.R[1]);
    res.escrowKey = Common::podToHex(params.escrowPubKey);
    if (params.role == XfgSwap::SwapRole::BOB) {
      res.adaptorPoint  = Common::podToHex(params.adaptorPoint);
      res.dleqChallenge = Common::podToHex(params.adaptorDleqProof.challenge);
      res.dleqResponse  = Common::podToHex(params.adaptorDleqProof.response);
    }
    res.status = WALLET_RPC_STATUS_OK;
  } catch (const JsonRpc::JsonRpcError&) {
    throw;
  } catch (const std::exception& e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, e.what());
  }
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_complete_swap(const wallet_rpc::COMMAND_RPC_COMPLETE_SWAP::request& req, wallet_rpc::COMMAND_RPC_COMPLETE_SWAP::response& res) {
  // TODO: requires SwapDaemon IPC channel
  // complete_swap needs full MuSig2 session state that lives in SwapDaemon (M5).
  // The wallet RPC layer does not have a wired IPC path to SwapDaemon yet.
  // Use /processswap via fuegod RPC until the IPC channel is implemented.
  (void)req;
  res.txHash = "";
  res.status = "not yet implemented — SwapDaemon IPC not wired";
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_refund_swap(const wallet_rpc::COMMAND_RPC_REFUND_SWAP::request& req, wallet_rpc::COMMAND_RPC_REFUND_SWAP::response& res) {
  // TODO: requires SwapDaemon IPC channel
  // refund_swap needs full MuSig2 session state that lives in SwapDaemon (M5).
  // The wallet RPC layer does not have a wired IPC path to SwapDaemon yet.
  // Use /refundswap via fuegod RPC until the IPC channel is implemented.
  (void)req;
  res.txHash = "";
  res.status = "not yet implemented — SwapDaemon IPC not wired";
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_transfer(const wallet_rpc::COMMAND_RPC_TRANSFER::request& req, wallet_rpc::COMMAND_RPC_TRANSFER::response& res) {
  std::vector<CryptoNote::WalletLegacyTransfer> transfers;
  std::vector<CryptoNote::TransactionMessage> messages;
  for (auto it = req.destinations.begin(); it != req.destinations.end(); it++) {
    CryptoNote::WalletLegacyTransfer transfer;
    transfer.address = it->address;
    transfer.amount = it->amount;
    transfers.push_back(transfer);

    if (!it->message.empty()) {
      messages.emplace_back(CryptoNote::TransactionMessage{ it->message, it->address });
    }
  }

  std::vector<uint8_t> extra;
  if (!req.payment_id.empty()) {
    std::string payment_id_str = req.payment_id;

    Crypto::Hash payment_id;
    if (!CryptoNote::parsePaymentId(payment_id_str, payment_id)) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID,
        "Payment id has invalid format: \"" + payment_id_str + "\", expected 64-character string");
    }

    BinaryArray extra_nonce;
    CryptoNote::setPaymentIdToTransactionExtraNonce(extra_nonce, payment_id);
    if (!CryptoNote::addExtraNonceToTransactionExtra(extra, extra_nonce)) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID,
        "Something went wrong with payment_id. Please check its format: \"" + payment_id_str + "\", expected 64-character string");
    }
  }

  for (auto& rpc_message : req.messages) {
     messages.emplace_back(CryptoNote::TransactionMessage{ rpc_message.message, rpc_message.address });
  }

  uint64_t ttl = 0;
  if (req.ttl != 0) {
    ttl = static_cast<uint64_t>(time(nullptr)) + req.ttl;
  }

  uint64_t actualFee = m_currency.minimumFee();

  std::string extraString;
  std::copy(extra.begin(), extra.end(), std::back_inserter(extraString));
  try {
    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(m_wallet, sent);

    Crypto::SecretKey transactionSK;
    CryptoNote::TransactionId tx = m_wallet.sendTransaction(transactionSK, transfers, actualFee, extraString, req.mixin, req.unlock_time, messages, ttl);
    if (tx == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      throw std::runtime_error("Couldn't send transaction");
    }

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();

    if (sendError) {
      throw std::system_error(sendError);
    }

    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet.getTransaction(tx, txInfo);
    res.tx_hash = Common::podToHex(txInfo.hash);
    res.tx_secret_key = Common::podToHex(transactionSK);

  } catch (const std::exception& e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR, e.what());
  }
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------

bool wallet_rpc_server::on_get_tx_proof(const wallet_rpc::COMMAND_RPC_GET_TX_PROOF::request& req,
	wallet_rpc::COMMAND_RPC_GET_TX_PROOF::response& res) {
	Crypto::Hash txid;
	if (!parse_hash256(req.tx_hash, txid)) {
		throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, std::string("Failed to parse tx_hash"));
	}
	CryptoNote::AccountPublicAddress dest_address;
	if (!m_currency.parseAccountAddressString(req.dest_address, dest_address)) {
		throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_WRONG_ADDRESS, std::string("Failed to parse address"));
	}

	Crypto::SecretKey tx_key, tx_key2;
	bool r = m_wallet.get_tx_key(txid, tx_key);

	if (!req.tx_key.empty()) {
		Crypto::Hash tx_key_hash;
		size_t size;
		if (!Common::fromHex(req.tx_key, &tx_key_hash, sizeof(tx_key_hash), size) || size != sizeof(tx_key_hash)) {
			throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, std::string("Failed to parse tx_key"));
		}
		tx_key2 = *(struct Crypto::SecretKey *) &tx_key_hash;

		if (r) {
			if (tx_key != tx_key2) {
				throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, 
					std::string("Tx secret key was found for the given txid, but you've also provided another tx secret key which doesn't match the found one."));
			}
		}
		tx_key = tx_key2;
	}
	else {
		if (!r) {
			throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR,
				std::string("Tx secret key wasn't found in the wallet file. Provide it as the optional <tx_key> parameter if you have it elsewhere."));
		}
	}
	
	std::string sig_str;
	if (m_wallet.getTxProof(txid, dest_address, tx_key, sig_str)) {
		res.signature = sig_str;
	}
	else {
		throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, std::string("Failed to get transaction proof"));
	}

	return true;
}

bool wallet_rpc_server::on_get_reserve_proof(const wallet_rpc::COMMAND_RPC_GET_BALANCE_PROOF::request& req,
	wallet_rpc::COMMAND_RPC_GET_BALANCE_PROOF::response& res) {

	try {
		res.signature = m_wallet.getReserveProof(req.amount != 0 ? req.amount : m_wallet.actualBalance(), !req.message.empty() ? req.message : "");
	}
	catch (const std::exception &e) {
		throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, e.what());
	}

	return true;
}

bool wallet_rpc_server::on_optimize(const wallet_rpc::COMMAND_RPC_OPTIMIZE::request& req, wallet_rpc::COMMAND_RPC_OPTIMIZE::response& res) {
  std::vector<CryptoNote::WalletLegacyTransfer> transfers;
  std::vector<CryptoNote::TransactionMessage> messages;
  std::string extraString;
  uint64_t fee = m_currency.minimumFee();
  uint64_t mixIn = 0;
  uint64_t unlockTimestamp = 0;
  uint64_t ttl = 0;

  try {
    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(m_wallet, sent);

    Crypto::SecretKey transactionSK;
    CryptoNote::TransactionId tx = m_wallet.sendTransaction(transactionSK, transfers, fee, extraString, mixIn, unlockTimestamp, messages, ttl);
    if (tx == WALLET_LEGACY_INVALID_TRANSACTION_ID) {
      throw std::runtime_error("Couldn't send transaction");
    }

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();

    if (sendError) {
      throw std::system_error(sendError);
    }

    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet.getTransaction(tx, txInfo);
    res.tx_hash = Common::podToHex(txInfo.hash);
    res.tx_secret_key = Common::podToHex(transactionSK);

  } catch (const std::exception& e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR, e.what());
  }
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_estimate_fusion(const wallet_rpc::COMMAND_RPC_ESTIMATE_FUSION::request& req, wallet_rpc::COMMAND_RPC_ESTIMATE_FUSION::response& res)
{
  if (req.threshold <= m_currency.defaultDustThreshold()) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, std::string("Fusion transaction threshold is too small. Threshold: " + 
      m_currency.formatAmount(req.threshold)) + ", minimum threshold " + m_currency.formatAmount(m_currency.defaultDustThreshold() + 1));
  }
  try {
    res.fusion_ready_count = m_wallet.estimateFusion(req.threshold);
  }
  catch (std::exception &e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, std::string("Failed to estimate fusion ready count: ") + e.what());
  }
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_send_fusion(const wallet_rpc::COMMAND_RPC_SEND_FUSION::request& req, wallet_rpc::COMMAND_RPC_SEND_FUSION::response& res)
{
  const size_t MAX_FUSION_OUTPUT_COUNT = 8;

  if (req.threshold <= m_currency.defaultDustThreshold()) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, std::string("Fusion transaction threshold is too small. Threshold: " +
      m_currency.formatAmount(req.threshold)) + ", minimum threshold " + m_currency.formatAmount(m_currency.defaultDustThreshold() + 1));
  }

  size_t estimatedFusionInputsCount = m_currency.getApproximateMaximumInputCount(m_currency.fusionTxMaxSize(), MAX_FUSION_OUTPUT_COUNT, req.mixin);
  if (estimatedFusionInputsCount < m_currency.fusionTxMinInputCount()) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR,
      std::string("Fusion transaction mixin is too big " + std::to_string(req.mixin)));
  }

  try {
    std::list<TransactionOutputInformation> fusionInputs = m_wallet.selectFusionTransfersToSend(req.threshold, m_currency.fusionTxMinInputCount(), estimatedFusionInputsCount);
    if (fusionInputs.size() < m_currency.fusionTxMinInputCount()) {
      //nothing to optimize
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR,
        std::string("Fusion transaction not created: nothing to optimize for threshold " + std::to_string(req.threshold)));
    }

    std::string extraString;
    CryptoNote::WalletHelper::SendCompleteResultObserver sent;
    WalletHelper::IWalletRemoveObserverGuard removeGuard(m_wallet, sent);

    CryptoNote::TransactionId tx = m_wallet.sendFusionTransaction(fusionInputs, 50, extraString, 0, req.unlock_time);
    if (tx == WALLET_LEGACY_INVALID_TRANSACTION_ID)
      throw std::runtime_error("Couldn't send fusion transaction");

    std::error_code sendError = sent.wait(tx);
    removeGuard.removeObserver();

    if (sendError)
       throw std::system_error(sendError);

    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet.getTransaction(tx, txInfo);
    res.tx_hash = Common::podToHex(txInfo.hash);
  }
  catch (const std::exception& e)
  {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR, e.what());
  }
  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_store(const wallet_rpc::COMMAND_RPC_STORE::request& req, wallet_rpc::COMMAND_RPC_STORE::response& res) {
  try {
    WalletHelper::storeWallet(m_wallet, m_walletFilename);
  } catch (std::exception& e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, std::string("Couldn't save wallet: ") + e.what());
  }

  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_get_messages(const wallet_rpc::COMMAND_RPC_GET_MESSAGES::request& req, wallet_rpc::COMMAND_RPC_GET_MESSAGES::response& res) {
  res.total_tx_count = m_wallet.getTransactionCount();

  for (uint64_t i = req.first_tx_id; i < res.total_tx_count && res.tx_messages.size() < req.tx_limit; ++i) {
    WalletLegacyTransaction tx;
    if (!m_wallet.getTransaction(static_cast<TransactionId>(i), tx)) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Failed to get transaction");
    }

    if (!tx.messages.empty()) {
      wallet_rpc::transaction_messages tx_messages;
      tx_messages.tx_hash = Common::podToHex(tx.hash);
      tx_messages.tx_id = i;
      tx_messages.block_height = tx.blockHeight;
      tx_messages.timestamp = tx.timestamp;
      std::copy(tx.messages.begin(), tx.messages.end(), std::back_inserter(tx_messages.messages));

      // replace newlines in messages with \n to avoid JSON error
      for (auto it = tx_messages.messages.begin(); it != tx_messages.messages.end(); it++) {
        for (std::string::size_type n = 0; ( ( n = (*it).find("\n", n ) ) != std::string::npos ); ) {
          (*it).replace(n, 1, "\\n");
          n += 2;
        }
      }

      res.tx_messages.emplace_back(std::move(tx_messages));
    }
  }

  return true;
}
//------------------------------------------------------------------------------------------------------------------------------
bool wallet_rpc_server::on_get_payments(const wallet_rpc::COMMAND_RPC_GET_PAYMENTS::request& req, wallet_rpc::COMMAND_RPC_GET_PAYMENTS::response& res) {
  PaymentId expectedPaymentId;
  CryptoNote::BinaryArray payment_id_blob;

  if (!Common::fromHex(req.payment_id, payment_id_blob)) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID, "Payment ID has invald format");
  }

  if (sizeof(expectedPaymentId) != payment_id_blob.size()) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID, "Payment ID has invalid size");
  }

  std::copy(std::begin(payment_id_blob), std::end(payment_id_blob), reinterpret_cast<char*>(&expectedPaymentId)); // no UB, char can alias any type
  auto payments = m_wallet.getTransactionsByPaymentIds({expectedPaymentId});
  assert(payments.size() == 1);
  for (auto& transaction : payments[0].transactions) {
    wallet_rpc::payment_details rpc_payment;
    rpc_payment.tx_hash = Common::podToHex(transaction.hash);
    rpc_payment.amount = transaction.totalAmount;
    rpc_payment.block_height = transaction.blockHeight;
    rpc_payment.unlock_time = transaction.unlockTime;
    res.payments.push_back(rpc_payment);
  }

  return true;
}

/* ----------------------------------------------------------------------------------------------------------- */

/* CREATE INTEGRATED */
/* takes an address and payment ID and returns an integrated address */

bool wallet_rpc_server::on_create_integrated(const wallet_rpc::COMMAND_RPC_CREATE_INTEGRATED::request& req, wallet_rpc::COMMAND_RPC_CREATE_INTEGRATED::response& res) 
{

  if (!req.payment_id.empty() && !req.address.empty()) 
  {

    std::string payment_id_str = req.payment_id;
    std::string address_str = req.address;

    uint64_t prefix;
    CryptoNote::AccountPublicAddress addr;

    /* get the spend and view public keys from the address */
    const bool valid = CryptoNote::parseAccountAddressString(prefix, 
                                                            addr,
                                                            address_str);

    CryptoNote::BinaryArray ba;
    CryptoNote::toBinaryArray(addr, ba);
    std::string keys = Common::asString(ba);

    /* create the integrated address the same way you make a public address */
    std::string integratedAddress = Tools::Base58::encode_addr (
        CryptoNote::parameters::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX,
        payment_id_str + keys
    );

    res.integrated_address = integratedAddress;
  }
  return true;
}

/* --------------------------------------------------------------------------------- */

bool wallet_rpc_server::on_get_transfers(const wallet_rpc::COMMAND_RPC_GET_TRANSFERS::request& req, wallet_rpc::COMMAND_RPC_GET_TRANSFERS::response& res) {
  res.transfers.clear();
  size_t transactionsCount = m_wallet.getTransactionCount();
  for (size_t trantransactionNumber = 0; trantransactionNumber < transactionsCount; ++trantransactionNumber) {
    WalletLegacyTransaction txInfo;
    m_wallet.getTransaction(trantransactionNumber, txInfo);
    if (txInfo.state != WalletLegacyTransactionState::Active || txInfo.blockHeight == WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT) {
      continue;
    }

    std::string address = "";
    if (txInfo.totalAmount < 0) {
      if (txInfo.transferCount > 0) {
        WalletLegacyTransfer tr;
        m_wallet.getTransfer(txInfo.firstTransferId, tr);
        address = tr.address;
      }
    }

    wallet_rpc::Transfer transfer;
    transfer.time = txInfo.timestamp;
    transfer.output = txInfo.totalAmount < 0;
    transfer.transactionHash = Common::podToHex(txInfo.hash);
    transfer.amount = std::abs(txInfo.totalAmount);
    transfer.fee = txInfo.fee;
    transfer.address = address;
    transfer.blockIndex = txInfo.blockHeight;
    transfer.unlockTime = txInfo.unlockTime;
    transfer.paymentId = "";

    std::vector<uint8_t> extraVec;
    extraVec.reserve(txInfo.extra.size());
    std::for_each(txInfo.extra.begin(), txInfo.extra.end(), [&extraVec](const char el) { extraVec.push_back(el); });

    Crypto::Hash paymentId;
    transfer.paymentId = (getPaymentIdFromTxExtra(extraVec, paymentId) && paymentId != NULL_HASH ? Common::podToHex(paymentId) : "");

    res.transfers.push_back(transfer);
  }

  return true;
}

bool wallet_rpc_server::on_get_height(const wallet_rpc::COMMAND_RPC_GET_HEIGHT::request& req, wallet_rpc::COMMAND_RPC_GET_HEIGHT::response& res) {
  res.height = m_node.getLastLocalBlockHeight();
  return true;
}

bool wallet_rpc_server::on_get_outputs(const wallet_rpc::COMMAND_RPC_GET_OUTPUTS::request& req, wallet_rpc::COMMAND_RPC_GET_OUTPUTS::response& res) {
  res.num_unlocked_outputs = m_wallet.getNumUnlockedOutputs();
  return true;
}

bool wallet_rpc_server::on_reset(const wallet_rpc::COMMAND_RPC_RESET::request& req, wallet_rpc::COMMAND_RPC_RESET::response& res) {
  m_wallet.reset();
  return true;
}

// ── Phase 7: CD / COLD wallet RPC bridges ────────────────────────────────────

bool wallet_rpc_server::on_list_cds(const wallet_rpc::COMMAND_RPC_LIST_CDS::request& req, wallet_rpc::COMMAND_RPC_LIST_CDS::response& res) {
  size_t count = m_wallet.getDepositCount();
  for (size_t i = 0; i < count; ++i) {
    CryptoNote::Deposit dep;
    if (!m_wallet.getDeposit(static_cast<CryptoNote::DepositId>(i), dep)) continue;

    wallet_rpc::COMMAND_RPC_LIST_CDS::deposit_entry entry;
    entry.deposit_id     = static_cast<uint64_t>(i);
    entry.amount         = dep.amount;
    entry.term           = dep.term;
    entry.unlock_height  = dep.unlockHeight;
    entry.creation_height = dep.height;
    entry.locked         = dep.locked;

    switch (dep.depositType) {
      case CryptoNote::Deposit::Type::HEAT:
        entry.deposit_type = "HEAT"; break;
      default:
        entry.deposit_type = "COLD"; break;
    }

    res.deposits.push_back(std::move(entry));
  }
  res.status = WALLET_RPC_STATUS_OK;
  return true;
}

bool wallet_rpc_server::on_create_cd(const wallet_rpc::COMMAND_RPC_CREATE_CD::request& req, wallet_rpc::COMMAND_RPC_CREATE_CD::response& res) {
  try {
    CryptoNote::WalletGreen* wg = dynamic_cast<CryptoNote::WalletGreen*>(&m_wallet);
    if (!wg) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "create_cd requires WalletGreen");
    }
    if (req.amount == 0) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "amount must be > 0");
    }
    if (req.term == 0) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "term must be > 0");
    }

    // Validate deposit_type: only HEAT=0x08, xCD=0xCD, YIELD=0x07 are permitted
    static const std::initializer_list<uint32_t> validDepositTypes = {0x07, 0x08, 0xCD};
    bool typeValid = false;
    for (uint32_t v : validDepositTypes) {
      if (req.deposit_type == v) { typeValid = true; break; }
    }
    if (!typeValid) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR,
          "deposit_type must be one of: 0x07 (YIELD), 0x08 (HEAT), 0xCD (xCD)");
    }

    // Map wire deposit_type to internal CommitmentType
    CryptoNote::CommitmentType commitType =
        (req.deposit_type == 0x08) ? CryptoNote::CommitmentType::HEAT :
        (req.deposit_type == 0x07) ? CryptoNote::CommitmentType::YIELD :
                                     CryptoNote::CommitmentType::COLD;
    CryptoNote::DepositCommitment commitment(commitType, Crypto::Hash{});

    std::string txHash;
    CryptoNote::IWallet* iwallet = static_cast<CryptoNote::IWallet*>(wg);
    std::string addr = iwallet->getAddress(0);
    iwallet->createDeposit(req.amount, static_cast<uint64_t>(req.term), addr, addr, txHash, commitment);

    // Deposit ID is the new last index after creation
    size_t depositId = iwallet->getWalletDepositCount() - 1;
    CryptoNote::Deposit dep = iwallet->getDeposit(depositId);

    res.tx_hash       = txHash;
    res.deposit_id    = static_cast<uint64_t>(depositId);
    res.unlock_height = dep.unlockHeight;
    res.status        = WALLET_RPC_STATUS_OK;
  } catch (const JsonRpc::JsonRpcError&) {
    throw;
  } catch (const std::exception& e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR, e.what());
  }
  return true;
}

bool wallet_rpc_server::on_withdraw_cd(const wallet_rpc::COMMAND_RPC_WITHDRAW_CD::request& req, wallet_rpc::COMMAND_RPC_WITHDRAW_CD::response& res) {
  try {
    CryptoNote::DepositId depId = static_cast<CryptoNote::DepositId>(req.deposit_id);
    CryptoNote::Deposit dep;
    if (!m_wallet.getDeposit(depId, dep)) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Deposit not found");
    }
    if (dep.locked) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Deposit is not yet mature");
    }

    CryptoNote::TransactionId txId = m_wallet.withdrawDeposits({depId}, 0);
    CryptoNote::WalletLegacyTransaction txInfo;
    m_wallet.getTransaction(txId, txInfo);
    res.tx_hash = Common::podToHex(txInfo.hash);
    res.status  = WALLET_RPC_STATUS_OK;
  } catch (const JsonRpc::JsonRpcError&) {
    throw;
  } catch (const std::exception& e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR, e.what());
  }
  return true;
}

bool wallet_rpc_server::on_rollover_cd(const wallet_rpc::COMMAND_RPC_ROLLOVER_CD::request& req, wallet_rpc::COMMAND_RPC_ROLLOVER_CD::response& res) {
  try {
    CryptoNote::WalletGreen* wg = dynamic_cast<CryptoNote::WalletGreen*>(&m_wallet);
    if (!wg) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Rollover requires WalletGreen");
    }

    CryptoNote::DepositId depId = static_cast<CryptoNote::DepositId>(req.deposit_id);
    CryptoNote::Deposit dep;
    if (!m_wallet.getDeposit(depId, dep)) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Deposit not found");
    }
    if (dep.locked) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "Deposit is not yet mature");
    }

    uint32_t newTerm = (req.new_term == 0) ? dep.term : req.new_term;
    std::string txHash;

    // Obtain accumulated CD interest via INode (delegates to CommitmentIndex in the Core).
    uint32_t currentHeight = m_node.getLastLocalBlockHeight();
    uint64_t interest = 0;
    std::error_code ec = m_node.getCdInterest(dep.amount, dep.height, currentHeight, interest);
    if (ec) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR,
                                  "getCdInterest failed: " + ec.message());
    }

    if (!wg->rolloverDeposit(depId, newTerm, interest, txHash)) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR,
                                  "rolloverDeposit failed");
    }

    // Get new deposit info (last created)
    size_t newDepId = m_wallet.getDepositCount() - 1;
    CryptoNote::Deposit newDep;
    m_wallet.getDeposit(static_cast<CryptoNote::DepositId>(newDepId), newDep);

    res.tx_hash    = txHash;
    res.new_amount = newDep.amount;
    res.status     = WALLET_RPC_STATUS_OK;
  } catch (const JsonRpc::JsonRpcError&) {
    throw;
  } catch (const std::exception& e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR, e.what());
  }
  return true;
}

bool wallet_rpc_server::on_estimate_cd_yield(const wallet_rpc::COMMAND_RPC_ESTIMATE_CD_YIELD::request& req, wallet_rpc::COMMAND_RPC_ESTIMATE_CD_YIELD::response& res) {
  try {
    if (req.amount == 0) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, "amount must be > 0");
    }

    uint32_t currentHeight = (req.current_height == 0)
      ? m_node.getLastLocalBlockHeight()
      : req.current_height;

    uint32_t heightDiff = (currentHeight > req.creation_height)
      ? (currentHeight - req.creation_height) : 0;

    // Epoch duration constants
    const uint32_t epochDuration = m_currency.isTestnet()
      ? static_cast<uint32_t>(CryptoNote::parameters::TESTNET_EPOCH_DURATION_BLOCKS)
      : static_cast<uint32_t>(CryptoNote::parameters::EPOCH_DURATION_BLOCKS);

    res.effective_epochs = heightDiff / epochDuration;

    // Delegate to INode which in turn calls CommitmentIndex via Core.
    uint64_t interest = 0;
    std::error_code ec = m_node.getCdInterest(req.amount, req.creation_height,
                                               currentHeight, interest);
    if (ec) {
      throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR,
                                  "getCdInterest failed: " + ec.message());
    }
    res.estimated_interest = interest;
    res.status = WALLET_RPC_STATUS_OK;
  } catch (const JsonRpc::JsonRpcError&) {
    throw;
  } catch (const std::exception& e) {
    throw JsonRpc::JsonRpcError(WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR, e.what());
  }
  return true;
}

}
