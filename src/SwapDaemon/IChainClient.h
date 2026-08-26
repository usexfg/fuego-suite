#pragma once

#include "SwapTypes.h"
#include "ChainClientResult.h"
#include <string>
#include <memory>

namespace XfgSwap {

class IChainClient {
public:
  virtual ~IChainClient() = default;
  virtual std::string chainName() const = 0;
  virtual ChainClientResult lock(const SwapParams& params) = 0;
  virtual ChainClientResult verifyLock(const SwapParams& params) = 0;
  virtual ChainClientResult claim(const SwapParams& params) = 0;
  virtual ChainClientResult refund(const SwapParams& params) = 0;

  // ── PTLC capability + PTLC lock path ──
  // Override in PTLC-capable clients (BTC Taproot, SOL ed25519, XMR/ZANO).
  // For HTLC-only chains (most EVMs, TON, SIA) keep default false → negotiate yields BRIDGE.
  virtual bool supportsPtlc() const { return false; }

  // True when this client implements PURE point locks (P2TR key-path / native
  // adaptor verify) — i.e. lockType == SwapLockType::PTLC is funded on-chain
  // with no H(t) hashlock component (PTLC_PURE_PLAN P2.2/P2.3).
  // Clients that only do the PTLC_HTLC_BRIDGE hybrid keep the default false.
  virtual bool supportsPurePtlc() const { return false; }
  virtual ChainClientResult lockPtlc(const SwapParams& params) {
    (void)params;
    return ChainClientResult::fail("PTLC not supported on " + chainName());
  }
  virtual ChainClientResult verifyPtlcLock(const SwapParams& params) {
    return verifyLock(params);
  }

  // The local counterparty-chain receive address for this client (the
  // address HTLC claims/refunds pay into). Advertised in AFK fill results
  // so the taker locks CTR to the maker's address. Empty when not
  // configured or not derivable.
  virtual std::string getReceiveAddress() const { return ""; }

  // Verify a counterparty reserve proof.
  // expectedMessage: the proof's signed message must equal this (binds the proof
  //   to a specific swap/offer — pass the offerId). Empty disables the binding check.
  // minAmount: minimum counterparty-chain balance the proof must demonstrate.
  virtual ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                               uint64_t minAmount,
                                               const std::string& proof) = 0;

  // Get transaction details including SPV confirmation status.
  // Populates ChainClientResult with confirmed/spvVerified/confirmations fields.
  virtual ChainClientResult getTransactionDetails(const std::string& txId,
                                                  ChainClientResult& result) {    (void)txId;
    result = ChainClientResult::fail("getTransactionDetails not implemented");
    return result;
  }

  // If the counterparty HTLC has been claimed on-chain, recover the preimage
  // (adaptor secret) from the claim path. Returns lowercase hex of 32-byte
  // secret, or empty if not yet claimed / not supported.
  virtual std::string tryExtractClaimedSecret(const SwapParams& params) {
    (void)params;
    return {};
  }

  virtual bool getCurrentHeight(uint64_t& height) { (void)height; return false; }
};

} // namespace XfgSwap
