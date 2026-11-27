// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// An INode with no chain: every query succeeds with empty results, so a wallet
// can be created, saved, loaded and asked for addresses without a daemon.

#pragma once

#include <memory>
#include <system_error>
#include <vector>

#include "INode.h"

namespace CryptoNote {

class NullNode : public INode {
public:
  bool addObserver(INodeObserver*) override { return true; }
  bool removeObserver(INodeObserver*) override { return true; }
  void init(const Callback& cb) override { cb(std::error_code()); }
  bool shutdown() override { return true; }
  size_t getPeerCount() const override { return 0; }
  uint32_t getLastLocalBlockHeight() const override { return 0; }
  uint32_t getLastKnownBlockHeight() const override { return 0; }
  uint32_t getLocalBlockCount() const override { return 0; }
  uint32_t getKnownBlockCount() const override { return 0; }
  uint64_t getLastLocalBlockTimestamp() const override { return 0; }
  void relayTransaction(const Transaction&, const Callback& cb) override { cb(std::error_code()); }
  void getRandomOutsByAmounts(std::vector<uint64_t>&&, uint64_t, std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount>&, const Callback& cb) override { cb(std::error_code()); }
  void getRandomCommitmentOutsForAmount(uint64_t, uint64_t, uint32_t, uint32_t, std::vector<COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry>&, const Callback& cb) override { cb(std::error_code()); }
  void getNewBlocks(std::vector<Crypto::Hash>&&, std::vector<block_complete_entry>& blocks, uint32_t& startHeight, const Callback& cb) override { blocks.clear(); startHeight = 0; cb(std::error_code()); }
  void getTransactionOutsGlobalIndices(const Crypto::Hash&, std::vector<uint32_t>&, const Callback& cb) override { cb(std::error_code()); }
  void queryBlocks(std::vector<Crypto::Hash>&&, uint64_t, std::vector<BlockShortEntry>& blocks, uint32_t& startHeight, const Callback& cb) override { blocks.clear(); startHeight = 0; cb(std::error_code()); }
  void getPoolSymmetricDifference(std::vector<Crypto::Hash>&&, Crypto::Hash, bool& actual, std::vector<std::unique_ptr<ITransactionReader>>&, std::vector<Crypto::Hash>&, const Callback& cb) override { actual = true; cb(std::error_code()); }
  void getMultisignatureOutputByGlobalIndex(uint64_t, uint32_t, MultisignatureOutput&, const Callback& cb) override { cb(std::error_code()); }
  void getTransaction(const Crypto::Hash&, Transaction&, const Callback& cb) override { cb(std::error_code()); }
  void getBlocks(const std::vector<uint32_t>&, std::vector<std::vector<BlockDetails>>&, const Callback& cb) override { cb(std::error_code()); }
  void getBlocks(const std::vector<Crypto::Hash>&, std::vector<BlockDetails>&, const Callback& cb) override { cb(std::error_code()); }
  void getBlocks(uint64_t, uint64_t, uint32_t, std::vector<BlockDetails>&, uint32_t&, const Callback& cb) override { cb(std::error_code()); }
  void getTransactions(const std::vector<Crypto::Hash>&, std::vector<TransactionDetails>&, const Callback& cb) override { cb(std::error_code()); }
  void getTransactionsByPaymentId(const Crypto::Hash&, std::vector<TransactionDetails>&, const Callback& cb) override { cb(std::error_code()); }
  void getPoolTransactions(uint64_t, uint64_t, uint32_t, std::vector<TransactionDetails>&, uint64_t&, const Callback& cb) override { cb(std::error_code()); }
  void isSynchronized(bool& s, const Callback& cb) override { s = true; cb(std::error_code()); }
};

} // namespace CryptoNote
