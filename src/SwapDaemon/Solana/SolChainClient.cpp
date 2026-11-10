#include "SolChainClient.h"
#include <array>
#include <cstring>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdexcept>
#include "Common/StringTools.h"
#include "../Crypto/Base58Std.h"
#include "../Crypto/Ed25519Verify.h"
#include "../SwapHashLock.h"
#include "../SwapTimelock.h"
#include "SolPointClaim.h"

namespace XfgSwap {

static bool isZeroPub(const Crypto::PublicKey& k);
static std::string lowerHex(std::string s);

static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

static bool hasMatchingSolanaPublicKey(const std::string& keypairBase58) {
  try {
    const std::vector<uint8_t> keypair = Base58Std::decode(keypairBase58);
    if (keypair.size() != 64) return false;
    EVP_PKEY* privateKey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr, keypair.data(), 32);
    if (!privateKey) return false;
    std::array<uint8_t, 32> derivedPublicKey{};
    size_t derivedLength = derivedPublicKey.size();
    const bool derived = EVP_PKEY_get_raw_public_key(
        privateKey, derivedPublicKey.data(), &derivedLength) == 1;
    EVP_PKEY_free(privateKey);
    return derived && derivedLength == derivedPublicKey.size() &&
        CRYPTO_memcmp(derivedPublicKey.data(), keypair.data() + 32,
                      derivedPublicKey.size()) == 0;
  } catch (const std::exception&) {
    return false;
  }
}

SolChainClient::SolChainClient(std::unique_ptr<SolRpcClient> rpc, const std::string& keypairBase58)
  : m_rpc(std::move(rpc)), m_keypairBase58(keypairBase58) {}

std::string SolChainClient::readinessError() {
  if (!m_rpc) return "Solana RPC client is not configured";
  if (!m_rpc->hasValidProgramId()) return "Solana HTLC program ID is not configured";
  if (!hasMatchingSolanaPublicKey(m_keypairBase58)) {
    return "Solana keypair does not contain a matching public key";
  }
  SolAccountInfo programAccount{};
  if (!m_rpc->getAccountInfo(m_rpc->programId(), programAccount) ||
      !programAccount.executable) {
    return "Solana HTLC program cannot be verified as deployed";
  }
  if (getReceiveAddress().empty()) return "Solana keypair is not configured";
  return "";
}

std::string SolChainClient::getReceiveAddress() const {
  // The keypair (64 bytes base58) holds the secret key first, then the
  // public key. Return the public key as a base58 address.
  try {
    std::vector<uint8_t> raw = Base58Std::decode(m_keypairBase58);
    if (raw.size() != 64 || !hasMatchingSolanaPublicKey(m_keypairBase58)) return "";
    std::vector<uint8_t> pub(raw.begin() + 32, raw.begin() + 64);
    return Base58Std::encode(pub);
  } catch (const std::exception&) {
    return "";
  }
}

ChainClientResult SolChainClient::lock(const SwapParams& params) {
  if (params.lockType == SwapLockType::PTLC) return lockPtlc(params);
  // The Solana xfg_htlc program verifies keccak256(preimage) == hash_lock, and
  // claim() reveals the adaptor secret t as the preimage. So the hashlock MUST
  // be keccak256(t) — NOT the adaptor point T = t*G. Committing T (the old bug)
  // made every claim fail with InvalidPreimage; funds could only be refunded.
  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) {
    hashHex = solHashLockHex(params.adaptorSecret);
  } else {
    bool nz = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nz = true; break; }
    if (!nz)
      return ChainClientResult::fail("SOL lock: need adaptor secret or hashLock H(t) from Bob");
    hashHex = Common::podToHex(params.hashLock);
  }

  SolTxResult solResult;
  bool ok = m_rpc->lock(
      m_keypairBase58,
      params.ctrAddress,
      hashHex,
      params.ctrTimeoutBlock,
      params.ctrAmount64(),
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL lock failed: " + solResult.error);
  // The canonical reference for verifyLock/claim is the HTLC state PDA, NOT
  // the lock-tx signature. claim() base58-decodes this and requires 32 bytes.
  if (solResult.htlcAddress.empty())
    return ChainClientResult::fail("SOL lock: could not derive HTLC account address");
  return ChainClientResult::ok(solResult.htlcAddress);
}

ChainClientResult SolChainClient::lockPtlc(const SwapParams& params) {
  // PURE PTLC (plan P3): fund the SOL leg against the adaptor POINT T = t*G
  // via the on-chain `lock_ptlc` instruction (lock_type = 1). There is NO
  // hash commitment anywhere — the secret is only revealed by adaptor-sig
  // completion (scriptless scripts), not by preimage disclosure.
  // The escrow opens with the t behind the XFG adaptor point itself: SOL is
  // ed25519, so binding the lock to T needs no cross-curve proof.
  const Crypto::PublicKey& pt = params.adaptorPoint;
  if (isZeroPub(pt)) return ChainClientResult::fail("SOL lockPtlc: no adaptor point");
  std::string ptHex = Common::podToHex(pt);

  const std::string fundedEscrow = openPointEscrowFor(params, ptHex);
  if (!fundedEscrow.empty()) return ChainClientResult::okWithState(fundedEscrow, "ptlc:" + ptHex);

  SolTxResult solResult;
  bool ok = m_rpc->lockPtlc(
      m_keypairBase58,
      params.ctrAddress,
      ptHex,
      params.ctrTimeoutBlock,
      params.ctrAmount64(),
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL lockPtlc failed: " + solResult.error);
  // txId stays the HTLC state PDA — the canonical reference for
  // verifyLock/claim, same contract as lock().
  if (solResult.htlcAddress.empty())
    return ChainClientResult::fail("SOL lockPtlc: could not derive HTLC account address");
  // chainState "ptlc:<ptlcPointHex>" — pure marker, no | bridge suffix.
  return ChainClientResult::okWithState(solResult.htlcAddress, "ptlc:" + ptHex);
}

// Hex case normalization shared by both verifyLock paths.
static std::string lowerHex(std::string s) {
  for (char& c : s) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

static bool isZeroPub(const Crypto::PublicKey& k) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&k);
  for (size_t i = 0; i < sizeof(Crypto::PublicKey); ++i) if (p[i]) return false;
  return true;
}

static bool isPointLockState(const std::string& chainState) { return chainState.rfind("ptlc:", 0) == 0; }

// A swap negotiated as a point lock never takes the hash paths, whatever the
// lock notice says.
static bool usesPointLock(const SwapParams& params) {
  return params.lockType == SwapLockType::PTLC || isPointLockState(params.chainState);
}

// The escrow address is fixed by our key and T. If a lock whose confirmation
// was lost already funded it with these terms, reuse it: a second lock_ptlc
// would fail on the existing account and strand the first.
std::string SolChainClient::openPointEscrowFor(const SwapParams& params, const std::string& pointHex) {
  const std::string ownAddress = getReceiveAddress();
  const std::string escrow = m_rpc->deriveHtlcAddress(ownAddress, pointHex);
  SolHtlcInfo info;
  const bool ours = !escrow.empty() && m_rpc->getHtlcState(escrow, info) && info.lockType == 1 &&
                    !info.claimed && !info.refunded && info.sender == ownAddress &&
                    info.recipient == params.ctrAddress && lowerHex(info.ptlcPoint) == lowerHex(pointHex) &&
                    info.timeoutSlot == params.ctrTimeoutBlock && info.amount == params.ctrAmount64();
  return ours ? escrow : std::string();
}

bool SolChainClient::escrowOpenThroughRunway(const SolHtlcInfo& info, const SwapParams& params,
                                             std::string& error) {
  uint64_t tip = 0;
  uint64_t window = 0;
  if (info.claimed || info.refunded) {
    error = "the escrow is already settled";
    return false;
  }
  if (!m_rpc->getSlot(tip)) {
    error = "current slot unavailable";
    return false;
  }
  if (!minimumCounterpartyWindowBlocks(SwapPair::SOL, params.requiredConfirmations, window) ||
      tip > UINT64_MAX - window || info.timeoutSlot < tip + window) {
    error = "timeout slot " + std::to_string(info.timeoutSlot) + " leaves fewer than " +
            std::to_string(window) + " slots to claim from slot " + std::to_string(tip);
    return false;
  }
  return true;
}

// The escrow is a point lock to our swap's XFG adaptor point, pays the
// address we claim to, holds the amount and leaves the claim runway.
ChainClientResult SolChainClient::verifyPointLock(const SwapParams& params) {
  SolHtlcInfo info;
  std::string error;
  if (!m_rpc->getHtlcState(params.ctrLockTxId, info))
    return ChainClientResult::fail("SOL verifyLock: cannot read HTLC account state");
  if (info.lockType != 1)
    return ChainClientResult::fail("SOL verifyLock: state is not a PTLC lock (lock_type != 1)");
  if (info.amount < params.ctrAmount64()) return ChainClientResult::fail("SOL verifyLock: amount too low");
  if (isZeroPub(params.adaptorPoint) || lowerHex(info.ptlcPoint) != lowerHex(Common::podToHex(params.adaptorPoint)))
    return ChainClientResult::fail("SOL verifyLock: the escrow is not locked to this swap's adaptor point");
  if (params.ctrAddress.empty() || info.recipient != params.ctrAddress)
    return ChainClientResult::fail("SOL verifyLock: the escrow does not pay our claim address");
  if (params.ctrTimeoutBlock > 0 && info.timeoutSlot != params.ctrTimeoutBlock)
    return ChainClientResult::fail("SOL verifyLock: timeout_slot mismatch");
  if (!escrowOpenThroughRunway(info, params, error)) return ChainClientResult::fail("SOL verifyLock: " + error);
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult SolChainClient::verifyLock(const SwapParams& params) {
  // Pure PTLC locks announce themselves via chainState prefix "ptlc:" and are
  // verified against the bound adaptor point; legacy HTLC keeps the hash path.
  if (usesPointLock(params)) return verifyPointLock(params);

  SolHtlcInfo info;
  if (!m_rpc->getHtlcState(params.ctrLockTxId, info))
    return ChainClientResult::fail("SOL verifyLock: cannot read HTLC account state");
  if (info.claimed || info.refunded)
    return ChainClientResult::fail("SOL verifyLock: HTLC already claimed/refunded");
  if (info.amount < params.ctrAmount64())
    return ChainClientResult::fail("SOL verifyLock: amount too low");

  // Expected hashlock: H(t) from secret or published hashLock.
  std::string expectedHash;
  if (!isZeroSecret(params.adaptorSecret)) {
    expectedHash = solHashLockHex(params.adaptorSecret);
  } else {
    bool nz = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nz = true; break; }
    if (!nz)
      return ChainClientResult::fail("SOL verifyLock: no hashLock or adaptorSecret");
    expectedHash = Common::podToHex(params.hashLock);
  }
  // Normalize hex case for compare
  auto lower = [](std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return s;
  };
  if (lowerHex(info.hashLock) != lowerHex(expectedHash))
    return ChainClientResult::fail("SOL verifyLock: hash_lock mismatch");

  if (params.ctrTimeoutBlock > 0 && info.timeoutSlot != 0 &&
      info.timeoutSlot != params.ctrTimeoutBlock) {
    return ChainClientResult::fail("SOL verifyLock: timeout_slot mismatch");
  }
  if (!params.ctrAddress.empty() && !info.recipient.empty() &&
      lowerHex(info.recipient) != lowerHex(params.ctrAddress)) {
    // Recipient may be base58; case-sensitive compare preferred for base58
    if (info.recipient != params.ctrAddress)
      return ChainClientResult::fail("SOL verifyLock: recipient mismatch");
  }
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult SolChainClient::claim(const SwapParams& params) {
  if (usesPointLock(params)) return claimPoint(params);
  // Legacy HTLC fallback ONLY (plan task: keep untouched). The on-chain
  // program rejects this for lock_type == 1 — pure PTLC claims go through
  // SolRpcClient::claimPtlc (completed adaptor signature, no preimage).
  // Alice-locks: Bob's claim reveals adaptor secret t as the preimage.
  SolTxResult solResult;
  bool ok = m_rpc->claim(
      m_keypairBase58,
      params.ctrLockTxId,
      Common::podToHex(params.adaptorSecret),
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL claim failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

std::string SolChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (!m_rpc || params.ctrLockTxId.empty()) return {};

  // Pure PTLC: Bob's claim_ptlc completion is verified on-chain (N == s*G +
  // e*P, t*G == T enforced) and the recovered secret t is stored in
  // state.preimage exactly like an HTLC preimage — same read-back contract,
  // different verification upstream.
  if (usesPointLock(params)) {
    SolHtlcInfo ptlcInfo;
    if (!m_rpc->getHtlcState(params.ctrLockTxId, ptlcInfo)) return {};
    if (!ptlcInfo.claimed || ptlcInfo.lockType != 1) return {};
    if (ptlcInfo.preimage.size() != 64) return {};
    bool any = false;
    for (char c : ptlcInfo.preimage) {
      if (c != '0') { any = true; break; }
    }
    if (!any) return {};
    return ptlcInfo.preimage;
  }

  // Alice-locks: Bob's claim writes preimage t into the HTLC state account.
  // ctrLockTxId is the HTLC PDA (not the lock signature) after a successful lock().
  SolHtlcInfo info;
  if (!m_rpc->getHtlcState(params.ctrLockTxId, info)) return {};
  if (!info.claimed) return {};
  // Reject all-zero / empty preimage
  if (info.preimage.empty() || info.preimage.size() != 64) return {};
  bool any = false;
  for (char c : info.preimage) {
    if (c != '0') { any = true; break; }
  }
  if (!any) return {};
  return info.preimage;
}

bool SolChainClient::hasConfirmedClaim(const SwapParams& params) {
  // getHtlcState reads a finalized account owned by our HTLC program.
  return !tryExtractClaimedSecret(params).empty();
}

ChainClientResult SolChainClient::refund(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->refund(
      m_keypairBase58,
      params.ctrLockTxId,
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL refund failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

ChainClientResult SolChainClient::getTransactionDetails(
    const std::string& txId, ChainClientResult& result) {
  result = ChainClientResult::fail("SOL finalized transaction unavailable");
  uint64_t slot = 0, tip = 0;
  if (!m_rpc || !m_rpc->getFinalizedSignatureSlot(txId, slot) ||
      !m_rpc->getSlot(tip) || tip < slot ||
      tip - slot >= UINT32_MAX) return result;
  result = ChainClientResult::ok(txId);
  result.confirmed = true;
  result.blockHeight = slot;
  result.confirmations = static_cast<uint32_t>(tip - slot + 1);
  return result;
}

ChainClientResult SolChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("SOL reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string sigB58    = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  std::vector<uint8_t> pubkeyBytes = Base58Std::decode(address);
  if (pubkeyBytes.size() != 32)
    return ChainClientResult::fail("SOL reserve proof: invalid pubkey length (" +
                                   std::to_string(pubkeyBytes.size()) + ")");

  std::vector<uint8_t> sigBytes = Base58Std::decode(sigB58);
  if (sigBytes.size() != 64)
    return ChainClientResult::fail("SOL reserve proof: invalid signature length (" +
                                   std::to_string(sigBytes.size()) + ")");

  if (!Ed25519Verify::verify(pubkeyBytes.data(), message, sigBytes.data()))
    return ChainClientResult::fail("SOL reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("SOL reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("SOL reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("SOL reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool SolChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc->getSlot(height);
}


static bool decodeAccountKey(const std::string& base58, std::array<uint8_t, 32>& key) {
  try {
    const std::vector<uint8_t> bytes = Base58Std::decode(base58);
    if (bytes.size() != key.size()) return false;
    std::copy(bytes.begin(), bytes.end(), key.begin());
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// Bob claims by proving knowledge of t for this escrow; the program stores t
// for Alice. Nothing is sent unless the escrow is ours, open through the
// claim runway, and the proof passes the program's own rule.
ChainClientResult SolChainClient::claimPoint(const SwapParams& params) {
  SolHtlcInfo info;
  SolEscrowBinding escrow;
  SolPointClaimProof proof;
  std::string error;
  if (!m_rpc->getHtlcState(params.ctrLockTxId, info))
    return ChainClientResult::notSent("SOL claim: cannot read the escrow");
  if (info.lockType != 1 || lowerHex(info.ptlcPoint) != lowerHex(Common::podToHex(params.adaptorPoint)))
    return ChainClientResult::notSent("SOL claim: the escrow is not locked to this swap's adaptor point");
  if (!escrowOpenThroughRunway(info, params, error))
    return ChainClientResult::notSent("SOL claim: " + error + "; publishing t now could let the locker refund");
  if (info.recipient != getReceiveAddress())
    return ChainClientResult::notSent("SOL claim: the escrow pays " + info.recipient +
                                      ", which this keypair cannot claim to");
  escrow.amount = info.amount;
  if (!decodeAccountKey(params.ctrLockTxId, escrow.htlc) || !decodeAccountKey(info.recipient, escrow.recipient))
    return ChainClientResult::notSent("SOL claim: malformed escrow or recipient key");
  if (isZeroSecret(params.adaptorSecret) ||
      !proveSolPointClaim(params.adaptorSecret, params.adaptorPoint, escrow, proof) ||
      !solPointClaimAccepted(params.adaptorPoint, escrow, proof))
    return ChainClientResult::notSent("SOL claim: our secret does not open the adaptor point");
  SolTxResult solResult;
  const bool sent = m_rpc->claimPtlc(
      m_keypairBase58, params.ctrLockTxId,
      Common::toHex(proof.completedSignature.data(), proof.completedSignature.size()),
      Common::toHex(proof.nonce.data(), proof.nonce.size()),
      Common::toHex(proof.secretOffset.data(), proof.secretOffset.size()), solResult);
  if (!sent || !solResult.confirmed) return ChainClientResult::fail("SOL claim_ptlc failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

std::string SolChainClient::claimDeadlineError(const SwapParams& params) {
  SolHtlcInfo info;
  std::string error;
  if (!m_rpc || params.ctrLockTxId.empty()) return "SOL claim check: no escrow";
  if (!m_rpc->getHtlcState(params.ctrLockTxId, info)) return "SOL claim check: cannot read the escrow";
  if (!escrowOpenThroughRunway(info, params, error))
    return "SOL claim check: " + error + "; publishing t now could let the locker refund and claim";
  return "";
}

IChainClient::PointLockSupport SolChainClient::pointLockSupport() const {
  std::lock_guard<std::mutex> guard(m_pointLockSupportMutex);
  if (m_pointLockSupportKnown) return m_pointLockSupport;
  if (!m_rpc) return PointLockSupport::UNKNOWN;
  switch (m_rpc->programPointLocks()) {
    case SolRpcClient::ProgramPointLocks::PRESENT: m_pointLockSupport = PointLockSupport::AVAILABLE; break;
    case SolRpcClient::ProgramPointLocks::ABSENT: m_pointLockSupport = PointLockSupport::NONE; break;
    case SolRpcClient::ProgramPointLocks::UNREADABLE: return PointLockSupport::UNKNOWN;
  }
  m_pointLockSupportKnown = true;
  return m_pointLockSupport;
}

} // namespace XfgSwap
