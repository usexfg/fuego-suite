// Copyright (c) 2017-2026 Fuego Developers
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

#include "SwapTypes.h"
#include "SwapStateMachine.h"
#include "SwapDatabase.h"
#include "FuegoRpcClient.h"
#include "PriceOracle.h"
#include "Logging/LoggerRef.h"

#include <string>
#include <memory>

namespace XfgSwap {

class SwapDaemon {
public:
  SwapDaemon(const std::string& fuegodHost, uint16_t fuegodPort,
             const std::string& dataDir, Logging::ILogger& logger);

  // Load persisted non-terminal swaps and log recovery summary.
  // Call once after construction before issuing any commands.
  void start();

  // Configure wallet RPC endpoint for escrow funding.
  // Must be called before processSwap() can fund escrow.
  void setWalletRpc(const std::string& host, uint16_t port);

  // Start a new swap as initiator (Bob: has XFG, wants counterparty coin).
  bool initiate(SwapParams params);

  // Accept an incoming swap proposal.
  bool accept(const std::string& swapId);

  // Scan active swaps and refund any that have timed out.
  bool checkTimeouts();

  // Advance a specific swap to its next state based on chain observations.
  bool processSwap(const std::string& swapId);

  // Print a summary of all swaps.
  void listSwaps();

  // Print detailed info about a specific swap.
  void showSwap(const std::string& swapId);

  // Attempt to refund a specific swap (if timeout has elapsed).
  bool refund(const std::string& swapId);

  // Access the price oracle for configuration.
  PriceOracle& priceOracle();

private:
  // Scan non-terminal swaps and warn about any stuck longer than threshold.
  // Called from checkTimeouts().
  void checkStuckSwaps();

  static constexpr int SWAP_STUCK_THRESHOLD_ESCROW_SECS = 1800;  // 30 min
  static constexpr int SWAP_STUCK_THRESHOLD_KEYS_SECS   = 600;   // 10 min

  // Generate a unique swap ID from the current time and random data.
  std::string generateSwapId();

  // Fund the XFG escrow by sending to the Musig2 joint key address.
  // Computes escrow address from params.escrowPubKey, sends XFG via
  // wallet RPC, and stores the resulting tx hash in params.
  // Returns true on success.
  bool fundEscrow(SwapParams& params);

  // Verify that the escrow funding tx exists and contains an output
  // with the expected amount to the joint escrow key.
  // Returns true if the escrow is confirmed on chain.
  bool verifyEscrowFunding(const SwapParams& params);

  // Returns the resolved XFG address. If input is an alias (@name or short name),
  // resolves via RPC. If already an address, returns as-is. Returns "" on failure.
  std::string resolveAddressOrAlias(const std::string& input);

  FuegoRpcClient m_rpc;
  SwapDatabase m_db;
  PriceOracle m_oracle;
  Logging::LoggerRef m_logger;
};

} // namespace XfgSwap
