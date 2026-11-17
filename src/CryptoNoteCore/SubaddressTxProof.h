// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#pragma once

#include <string>
#include <vector>

#include "CryptoNote.h"
#include "crypto/crypto.h"

namespace CryptoNote {

// "ProofV2": one (tx key index, r*X, DLEQ signature) item per tx key that pays the
// address. Index MAIN_TX_KEY_INDEX is the main tx key; any other index n is the
// per-output key R_n of output n. X is the address view key (C for a funsa subaddress).
const uint8_t MAIN_TX_KEY_INDEX = 0xFF;

struct PaymentProofResult {
  uint64_t receivedAmount = 0;
  std::vector<TransactionOutput> outputs;
};

bool isPaymentProofV2(const std::string& proof);

bool generatePaymentProof(
    const Transaction& tx,
    const Crypto::Hash& txid,
    const AccountPublicAddress& address,
    bool isSubaddress,
    const Crypto::SecretKey& txSecret,
    std::string& proof);

bool checkPaymentProof(
    const Transaction& tx,
    const Crypto::Hash& txid,
    const AccountPublicAddress& address,
    bool isSubaddress,
    const std::string& proof,
    PaymentProofResult& result);

} // namespace CryptoNote
