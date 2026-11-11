// Copyright (c) 2017-2026 Fuego Developers
//
// Phase 3C seam: EVM light client (Helios-style sync-committee verification).
// Separate from ISpvClient (UTXO headers/Merkle). Full Helios FFI is deferred;
// this interface is the integration point for future C bindings.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Fuego {
namespace Evm {

struct EvmLightClientResult {
  bool ok = false;
  std::string error;
  uint64_t blockNumber = 0;
  std::string blockHashHex; // 0x-prefixed optional
};

// Verifies Ethereum (and L1-compatible) consensus state without trusting RPC.
// Expected future backend: Helios (Rust) via C ABI.
class IEvmLightClient {
public:
  virtual ~IEvmLightClient() = default;

  // Sync from checkpoint / trusted period; returns tip after sync.
  virtual EvmLightClientResult sync() = 0;

  // Best known verified tip.
  virtual EvmLightClientResult tip() const = 0;

  // Verify that txHash is included in a verified block (receipt/log proof).
  virtual EvmLightClientResult verifyTxInclusion(const std::string& txHashHex) = 0;

  // Verify account storage or contract storage slot (optional for HTLC getContract).
  virtual EvmLightClientResult verifyStorage(
      const std::string& addressHex,
      const std::string& slotHex,
      const std::string& expectedValueHex) {
    (void)addressHex; (void)slotHex; (void)expectedValueHex;
    EvmLightClientResult r;
    r.error = "verifyStorage not implemented";
    return r;
  }
};

// Placeholder that always fails closed — swap for Helios binding when ready.
class NullEvmLightClient : public IEvmLightClient {
public:
  EvmLightClientResult sync() override {
    return {false, "Helios EVM light client not linked", 0, {}};
  }
  EvmLightClientResult tip() const override {
    return {false, "Helios EVM light client not linked", 0, {}};
  }
  EvmLightClientResult verifyTxInclusion(const std::string&) override {
    return {false, "Helios EVM light client not linked", 0, {}};
  }
};

} // namespace Evm
} // namespace Fuego
