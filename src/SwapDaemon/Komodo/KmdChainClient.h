#pragma once

#include "../IChainClient.h"
#include "../Spv/ISpvClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

// Komodo (KMD) chain client — SPV-only mode.
//
// KMD is a Zcash/Bitcoin fork with standard UTXO model. Uses the same
// Electrum protocol as BCH for SPV verification. The HTLC script structure
// is identical to BTC/BCH (OP_SHA256 hash lock + OP_CHECKLOCKTIMEVERIFY refund).
//
// KMD-specific differences from BCH:
//   - Address prefixes: P2PKH (0x3C), P2SH (0x55), WIF (0xBC)
//   - Uses KmdHtlcScript instead of BchHtlcScript
//   - SPV-only (no full-node RPC client)
class KmdChainClient : public IChainClient {
public:
  // SPV-only mode (no RPC client)
  explicit KmdChainClient(std::shared_ptr<ISpvClient> spvClient);

  std::string chainName() const override { return "KMD"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;

  // Extract the HTLC claim preimage from a spending transaction.
  // Fetches the raw tx via the SPV client and parses the scriptSig.
  // Returns empty string on failure.
  std::string extractSecret(const std::string& spendingTxid,
                            const std::string& htlcRedeemScriptHex);

private:
  // SPV-mode verifyLock: fetch raw tx, parse outputs, verify amount and inclusion
  ChainClientResult verifyLockSpv(const SwapParams& params);

  // SPV-mode extractSecret: fetch raw spending tx, parse scriptSig
  std::string extractSecretSpv(const std::string& spendingTxid,
                               const std::vector<uint8_t>& htlcP2shScriptPubKey);

  std::shared_ptr<ISpvClient> m_spvClient;
};

} // namespace XfgSwap
