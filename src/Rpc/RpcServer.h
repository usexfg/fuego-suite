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
#pragma once

#include <functional>
#include <unordered_map>
#include <thread>
#include <memory>

#include "HttpServer.h"
#include <Logging/LoggerRef.h>
#include "Common/MathUtils.h"
#include "CoreRpcServerCommandsDefinitions.h"
#include "../SwapDaemon/SwapDatabase.h"
#include "../SwapDaemon/SwapDaemon.h"

namespace CryptoNote {

class core;
class NodeServer;
class ICryptoNoteProtocolQuery;
class SwapOfferRelay;
class CdOfferRelay;

class RpcServer : public HttpServer {
public:
  RpcServer(System::Dispatcher& dispatcher, Logging::ILogger& log, core& c, NodeServer& p2p, const ICryptoNoteProtocolQuery& protocolQuery);
  ~RpcServer();

  typedef std::function<bool(RpcServer*, const HttpRequest& request, HttpResponse& response)> HandlerFunction;
  bool setFeeAddress(const std::string& fee_address, const AccountPublicAddress& fee_acc);
  bool setViewKey(const std::string& view_key);
  bool restrictRPC(const bool is_resctricted);
  bool k_on_check_tx_proof(const K_COMMAND_RPC_CHECK_TX_PROOF::request& req, K_COMMAND_RPC_CHECK_TX_PROOF::response& res);
  bool k_on_check_reserve_proof(const K_COMMAND_RPC_CHECK_RESERVE_PROOF::request& req, K_COMMAND_RPC_CHECK_RESERVE_PROOF::response& res);
  bool enableCors(const std::string domain);
  bool remotenode_check_incoming_tx(const BinaryArray& tx_blob);
  void setSwapRelay(SwapOfferRelay* relay);
  void setCdRelay(CdOfferRelay* relay);
  void setSwapDb(XfgSwap::SwapDatabase* db);
  void setSwapDaemon(XfgSwap::SwapDaemon* daemon);

  // Start the HTTP server
  void start(const std::string& address, uint16_t port);
  // Stop the HTTP server
  void stop();

private:

  template <class Handler>
  struct RpcHandler {
    const Handler handler;
    const bool allowBusyCore;
  };

  typedef void (RpcServer::*HandlerPtr)(const HttpRequest& request, HttpResponse& response);
  static std::unordered_map<std::string, RpcHandler<HandlerFunction>> s_handlers;

  void processRequest(const HttpRequest& request, HttpResponse& response);
  bool processJsonRpcRequest(const HttpRequest& request, HttpResponse& response);
  bool isCoreReady();

  // binary handlers
  bool on_get_blocks(const COMMAND_RPC_GET_BLOCKS_FAST::request& req, COMMAND_RPC_GET_BLOCKS_FAST::response& res);
  bool on_query_blocks(const COMMAND_RPC_QUERY_BLOCKS::request& req, COMMAND_RPC_QUERY_BLOCKS::response& res);
  bool on_query_blocks_lite(const COMMAND_RPC_QUERY_BLOCKS_LITE::request& req, COMMAND_RPC_QUERY_BLOCKS_LITE::response& res);
  bool on_get_indexes(const COMMAND_RPC_GET_TX_GLOBAL_OUTPUTS_INDEXES::request& req, COMMAND_RPC_GET_TX_GLOBAL_OUTPUTS_INDEXES::response& res);
  bool on_get_random_outs(const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::request& req, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::response& res);
  bool on_get_random_commitment_outs(const COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::request& req, COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::response& res);
  bool on_get_random_outs_json(const COMMAND_RPC_GET_RANDOM_OUTPUTS_JSON::request& req, COMMAND_RPC_GET_RANDOM_OUTPUTS_JSON::response& res);

  bool onGetPoolChanges(const COMMAND_RPC_GET_POOL_CHANGES::request& req, COMMAND_RPC_GET_POOL_CHANGES::response& rsp);
  bool onGetPoolChangesLite(const COMMAND_RPC_GET_POOL_CHANGES_LITE::request& req, COMMAND_RPC_GET_POOL_CHANGES_LITE::response& rsp);

  // json handlers
  bool on_get_info(const COMMAND_RPC_GET_INFO::request& req, COMMAND_RPC_GET_INFO::response& res);
  bool on_get_height(const COMMAND_RPC_GET_HEIGHT::request& req, COMMAND_RPC_GET_HEIGHT::response& res);
  bool on_get_ethereal_flame(const COMMAND_RPC_GET_ETHERNAL_FLAME::request& req, COMMAND_RPC_GET_ETHERNAL_FLAME::response& res);

  // Swap state persistence RPC endpoints
  bool on_list_swaps(const COMMAND_RPC_LIST_SWAPS::request& req, COMMAND_RPC_LIST_SWAPS::response& res);
  bool on_get_swap_status(const COMMAND_RPC_GET_SWAP_STATUS::request& req, COMMAND_RPC_GET_SWAP_STATUS::response& res);

  // Swap execution RPC endpoints (SwapDaemon bridge)
  bool on_get_active_swaps(const COMMAND_RPC_GET_ACTIVE_SWAPS::request& req, COMMAND_RPC_GET_ACTIVE_SWAPS::response& res);
  bool on_initiate_swap(const COMMAND_RPC_INITIATE_SWAP::request& req, COMMAND_RPC_INITIATE_SWAP::response& res);
  bool on_accept_swap(const COMMAND_RPC_ACCEPT_SWAP::request& req, COMMAND_RPC_ACCEPT_SWAP::response& res);
  bool on_process_swap(const COMMAND_RPC_PROCESS_SWAP::request& req, COMMAND_RPC_PROCESS_SWAP::response& res);
  bool on_refund_swap(const COMMAND_RPC_REFUND_SWAP::request& req, COMMAND_RPC_REFUND_SWAP::response& res);

  // Swap orderbook RPC endpoints
  bool on_get_swap_offers(const COMMAND_RPC_GET_SWAP_OFFERS::request& req, COMMAND_RPC_GET_SWAP_OFFERS::response& res);
  bool on_get_swap_price(const COMMAND_RPC_GET_SWAP_PRICE::request& req, COMMAND_RPC_GET_SWAP_PRICE::response& res);
  bool on_get_swap_trades(const COMMAND_RPC_GET_SWAP_TRADES::request& req, COMMAND_RPC_GET_SWAP_TRADES::response& res);
  bool on_submit_swap_offer(const COMMAND_RPC_SUBMIT_SWAP_OFFER::request& req, COMMAND_RPC_SUBMIT_SWAP_OFFER::response& res);
  bool on_cancel_swap_offer(const COMMAND_RPC_CANCEL_SWAP_OFFER::request& req, COMMAND_RPC_CANCEL_SWAP_OFFER::response& res);
  bool on_request_swap(const COMMAND_RPC_REQUEST_SWAP::request& req, COMMAND_RPC_REQUEST_SWAP::response& res);

  bool on_get_deposits(const COMMAND_RPC_GET_DEPOSITS::request& req, COMMAND_RPC_GET_DEPOSITS::response& res);
  bool on_get_transactions(const COMMAND_RPC_GET_TRANSACTIONS::request& req, COMMAND_RPC_GET_TRANSACTIONS::response& res);
  // ZK prover data endpoints
  bool on_get_block_range(const COMMAND_RPC_GET_BLOCK_RANGE::request& req, COMMAND_RPC_GET_BLOCK_RANGE::response& res);

  // Fee pool analytics + treasury
  bool on_get_fee_pool_info(const COMMAND_RPC_GET_FEE_POOL_INFO::request& req, COMMAND_RPC_GET_FEE_POOL_INFO::response& res);
  bool on_get_heat_metrics(const COMMAND_RPC_GET_HEAT_METRICS::request& req, COMMAND_RPC_GET_HEAT_METRICS::response& res);
  bool on_amm_quote(const COMMAND_RPC_AMM_QUOTE::request& req, COMMAND_RPC_AMM_QUOTE::response& res);
  bool on_amm_pool_info(const COMMAND_RPC_AMM_POOL_INFO::request& req, COMMAND_RPC_AMM_POOL_INFO::response& res);
  bool on_add_swap_fee(const COMMAND_RPC_ADD_SWAP_FEE::request& req, COMMAND_RPC_ADD_SWAP_FEE::response& res);
  bool on_get_epoch_history(const COMMAND_RPC_GET_EPOCH_HISTORY::request& req, COMMAND_RPC_GET_EPOCH_HISTORY::response& res);
  bool on_estimate_cd_yield(const COMMAND_RPC_ESTIMATE_CD_YIELD::request& req, COMMAND_RPC_ESTIMATE_CD_YIELD::response& res);
  bool on_get_treasury_info(const COMMAND_RPC_GET_TREASURY_INFO::request& req, COMMAND_RPC_GET_TREASURY_INFO::response& res);
  
  bool on_get_cd_offers(const COMMAND_RPC_GET_CD_OFFERS::request& req, COMMAND_RPC_GET_CD_OFFERS::response& res);
  bool on_submit_cd_offer(const COMMAND_RPC_SUBMIT_CD_OFFER::request& req, COMMAND_RPC_SUBMIT_CD_OFFER::response& res);
  bool on_cancel_cd_offer(const COMMAND_RPC_CANCEL_CD_OFFER::request& req, COMMAND_RPC_CANCEL_CD_OFFER::response& res);
  bool on_get_maturing_deposits(const COMMAND_RPC_GET_MATURING_DEPOSITS::request& req, COMMAND_RPC_GET_MATURING_DEPOSITS::response& res);
  bool on_rollover_deposit(const COMMAND_RPC_ROLLOVER_DEPOSIT::request& req, COMMAND_RPC_ROLLOVER_DEPOSIT::response& res);


  bool on_get_alias(const COMMAND_RPC_GET_ALIAS::request& req,
                     COMMAND_RPC_GET_ALIAS::response& res);
  bool on_get_alias_by_address(const COMMAND_RPC_GET_ALIAS_BY_ADDRESS::request& req,
                                COMMAND_RPC_GET_ALIAS_BY_ADDRESS::response& res);
  bool on_get_all_aliases(const COMMAND_RPC_GET_ALL_ALIASES::request& req,
                           COMMAND_RPC_GET_ALL_ALIASES::response& res);

  bool on_get_peer_list(const COMMAND_RPC_GET_PEER_LIST::request& req, COMMAND_RPC_GET_PEER_LIST::response& res);
  bool on_prove_collateral(const COMMAND_RPC_PROVE_COLLATERAL::request& req, COMMAND_RPC_PROVE_COLLATERAL::response& res);
  bool on_send_raw_tx(const COMMAND_RPC_SEND_RAW_TX::request& req, COMMAND_RPC_SEND_RAW_TX::response& res);
  bool on_start_mining(const COMMAND_RPC_START_MINING::request& req, COMMAND_RPC_START_MINING::response& res);
  bool on_stop_mining(const COMMAND_RPC_STOP_MINING::request& req, COMMAND_RPC_STOP_MINING::response& res);
  bool on_stop_daemon(const COMMAND_RPC_STOP_DAEMON::request& req, COMMAND_RPC_STOP_DAEMON::response& res);
  bool on_get_fee_address(const COMMAND_RPC_GET_FEE_ADDRESS::request& req, COMMAND_RPC_GET_FEE_ADDRESS::response& res);
  bool on_alt_blocks_list_json(const COMMAND_RPC_GET_ALT_BLOCKS_LIST::request &req, COMMAND_RPC_GET_ALT_BLOCKS_LIST::response &res);
  bool on_get_payment_id(const COMMAND_RPC_GEN_PAYMENT_ID::request& req, COMMAND_RPC_GEN_PAYMENT_ID::response& res);

  // json rpc
  bool on_getblockcount(const COMMAND_RPC_GETBLOCKCOUNT::request& req, COMMAND_RPC_GETBLOCKCOUNT::response& res);
  bool on_getblockhash(const COMMAND_RPC_GETBLOCKHASH::request& req, COMMAND_RPC_GETBLOCKHASH::response& res);
  bool on_getblocktemplate(const COMMAND_RPC_GETBLOCKTEMPLATE::request& req, COMMAND_RPC_GETBLOCKTEMPLATE::response& res);
  bool on_get_currency_id(const COMMAND_RPC_GET_CURRENCY_ID::request& req, COMMAND_RPC_GET_CURRENCY_ID::response& res);
  bool on_submitblock(const COMMAND_RPC_SUBMITBLOCK::request& req, COMMAND_RPC_SUBMITBLOCK::response& res);
  bool on_get_last_block_header(const COMMAND_RPC_GET_LAST_BLOCK_HEADER::request& req, COMMAND_RPC_GET_LAST_BLOCK_HEADER::response& res);
  bool on_get_block_header_by_hash(const COMMAND_RPC_GET_BLOCK_HEADER_BY_HASH::request& req, COMMAND_RPC_GET_BLOCK_HEADER_BY_HASH::response& res);
  bool on_get_block_header_by_height(const COMMAND_RPC_GET_BLOCK_HEADER_BY_HEIGHT::request& req, COMMAND_RPC_GET_BLOCK_HEADER_BY_HEIGHT::response& res);

  void fill_block_header_response(const Block& blk, bool orphan_status, uint64_t height, const Crypto::Hash& hash, block_header_response& responce);

  bool f_on_blocks_list_json(const F_COMMAND_RPC_GET_BLOCKS_LIST::request& req, F_COMMAND_RPC_GET_BLOCKS_LIST::response& res);
  bool f_on_block_json(const F_COMMAND_RPC_GET_BLOCK_DETAILS::request& req, F_COMMAND_RPC_GET_BLOCK_DETAILS::response& res);
  bool f_on_transaction_json(const F_COMMAND_RPC_GET_TRANSACTION_DETAILS::request& req, F_COMMAND_RPC_GET_TRANSACTION_DETAILS::response& res);
  bool f_on_transactions_pool_json(const F_COMMAND_RPC_GET_POOL::request& req, F_COMMAND_RPC_GET_POOL::response& res);
  bool f_getMixin(const Transaction& transaction, uint64_t& mixin);

  Logging::LoggerRef logger;
  core& m_core;
  NodeServer& m_p2p;
  const ICryptoNoteProtocolQuery& m_protocolQuery;
  bool m_restricted_rpc;
  std::string m_cors_domain;
  std::string m_fee_address;
  Crypto::SecretKey m_view_key = NULL_SECRET_KEY;
  AccountPublicAddress m_fee_acc;
  SwapOfferRelay* m_swapRelay = nullptr;
  CdOfferRelay* m_cdRelay = nullptr;
  XfgSwap::SwapDatabase* m_swapDb = nullptr;
  XfgSwap::SwapDaemon* m_swapDaemon = nullptr;

};

}
