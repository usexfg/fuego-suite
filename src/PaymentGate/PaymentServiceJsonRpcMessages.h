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

#pragma once

#include <exception>
#include <limits>
#include <vector>
#include "IWallet.h"
#include "Serialization/ISerializer.h"

namespace PaymentService
{

const uint32_t DEFAULT_ANONYMITY_LEVEL = 4;

class RequestSerializationError : public std::exception
{
public:
  virtual const char *what() const throw() override { return "Request error"; }
};

struct Save
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct Reset {
  struct Request {
    std::string viewSecretKey;
    uint32_t scanHeight = std::numeric_limits<uint32_t>::max();

    void serialize(CryptoNote::ISerializer& serializer);
  };

  struct Response {
    void serialize(CryptoNote::ISerializer& serializer);
  };
};

struct ExportWallet
{
  struct Request
  {
    std::string exportFilename;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct ExportWalletKeys
{
  struct Request
  {
    std::string exportFilename;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetViewKey
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string viewSecretKey;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetStatus
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint32_t blockCount;
    uint32_t knownBlockCount;
    std::string lastBlockHash;
    uint32_t peerCount;
    uint32_t depositCount;
    uint32_t transactionCount;
    uint32_t addressCount;
    std::string networkId;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct CreateDeposit
{
  struct Request
  {
    uint64_t amount;
    uint64_t term;
    std::string sourceAddress;
    std::string heatCommitment;  // Hex string of HEAT commitment (optional)
    std::string metadata;        // Hex string of metadata (optional)
    bool useStagedUnlock;        // Optional staged unlock (default: false)

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;
    bool isBurnDeposit;          // Indicates if this is a burn deposit
    bool useStagedUnlock;        // Indicates if staged unlock was used
    uint64_t transactionFee;     // Transaction fee for this deposit
    uint64_t totalFees;          // Total fees (1x for traditional, 4x for staged)

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct CreateBurnDeposit
{
  struct Request
  {
    uint64_t amount;
    std::string sourceAddress;
    std::string metadata;        // Hex string of metadata (optional) - should include network_id for validation

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;
    uint64_t term;               // Always 4294967295 (FOREVER)
    uint64_t heatAmount;         // HEAT amount that will be minted

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct CreateBurnDepositWithProof
{
  struct Request
  {
    uint64_t amount;
    std::string sourceAddress;
    std::string recipientAddress;  // 🔥 ADD: Arbitrum recipient address
    std::string metadata;          // Hex string of metadata (optional) - can include network_id

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;
    uint64_t term;                 // Always 4294967295 (FOREVER)
    uint64_t heatAmount;           // HEAT amount that will be minted
    std::string burnProofDataFile; // 🔥 ADD: Path to generated BPDF
    std::string networkId;         // 🔥 ADD: Network ID for validation

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct CreateBurnDepositLarge
{
  struct Request
  {
    std::string sourceAddress;
    std::string metadata;        // Hex string of metadata (optional) - should include network_id for validation

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;
    uint64_t term;               // Always 4294967295 (FOREVER)
    uint64_t heatAmount;         // HEAT amount that will be minted (800 XFG = 8,000,000,000 HEAT)

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct CreateBurnDepositLargeWithProof
{
  struct Request
  {
    std::string sourceAddress;
    std::string recipientAddress;  // 🔥 ADD: Arbitrum recipient address
    std::string metadata;          // Hex string of metadata (optional) - can include network_id

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;
    uint64_t term;                 // Always 4294967295 (FOREVER)
    uint64_t heatAmount;           // HEAT amount that will be minted (800 XFG = 8,000,000,000 HEAT)
    std::string burnProofDataFile; // 🔥 ADD: Path to generated BPDF
    std::string networkId;         // 🔥 ADD: Network ID for validation

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GenerateBurnProofDataFile
{
  struct Request
  {
    std::string transactionHash;
    std::string recipientAddress;  // 🔥 ADD: Arbitrum recipient address
    std::string outputPath;        // 🔥 ADD: Where to save BPDF

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string burnProofDataFile; // 🔥 ADD: Path to generated BPDF
    bool success;
    std::string errorMessage;
    std::string networkId;         // 🔥 ADD: Network ID for validation

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GenerateBurnProofDataFileAuto
{
  struct Request
  {
    std::string transactionHash;
    std::string recipientAddress;  // 🔥 ADD: Arbitrum recipient address
    std::string outputPath;        // 🔥 ADD: Where to save BPDF

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string burnProofDataFile; // 🔥 ADD: Path to generated BPDF
    bool success;
    std::string errorMessage;
    std::string networkId;         // 🔥 ADD: Network ID for validation

    void serialize(CryptoNote::ISerializer &serializer);
  };
};







struct WithdrawDeposit
{
  struct Request
  {

    uint64_t depositId;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GiftDeposit
{
  struct Request
  {

    uint64_t amount;
    uint64_t term;
    std::string sourceAddress;
    std::string destinationAddress;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetMoneySupplyStats
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t baseMoneySupply;
    uint64_t ethernalXFG;
    uint64_t totalRebornXfg;
    	uint64_t totalMoneySupply;
    uint64_t circulatingSupply;
    double burnPercentage;
    double rebornPercentage;
    double supplyIncreasePercentage;
    
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetBaseTotalSupply
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t baseTotalSupply;    // All XFG created (base money supply)
    std::string formattedAmount; // Human-readable format
    
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetRealTotalSupply
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t realTotalSupply;    // baseTotalSupply - ethernalXFG
    uint64_t baseTotalSupply;    // All XFG created
    uint64_t ethernalXFG;     // Total burned XFG
    std::string formattedAmount; // Human-readable format
    
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetTotalDepositAmount
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t totalDepositAmount; // currentAmount in deposits - ethernalXFG
    uint64_t currentDepositAmount; // Current amount in all deposits
    uint64_t ethernalXFG;     // Total burned XFG
    std::string formattedAmount; // Human-readable format
    
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetCirculatingSupply
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t circulatingSupply;  // realTotalSupply - totalDepositAmount
    uint64_t realTotalSupply;    // actualTotalSupply - ethernalXFG
    uint64_t totalDepositAmount; // currentAmount in deposits - ethernalXFG
    std::string formattedAmount; // Human-readable format
    
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetEthernalXFG
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t ethernalXFG;     // Total burned XFG
    std::string formattedAmount; // Human-readable format
    
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetDynamicSupplyOverview
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t baseTotalSupply;    // All XFG created
    uint64_t realTotalSupply;    // baseTotalSupply - ethernalXFG
    uint64_t totalDepositAmount; // currentAmount in deposits - ethernalXFG
    uint64_t circulatingSupply;  // realTotalSupply - totalDepositAmount
    uint64_t ethernalXFG;     // Total burned XFG
    uint64_t currentDepositAmount; // Current amount in all deposits
    
    // Formatted amounts for display
    std::string baseTotalSupplyFormatted;
    std::string realTotalSupplyFormatted;
    std::string totalDepositAmountFormatted;
    std::string circulatingSupplyFormatted;
    std::string ethernalXFGFormatted;
    std::string currentDepositAmountFormatted;
    
    // Percentages
    double burnPercentage;       // (ethernalXFG / baseTotalSupply) * 100
    double depositPercentage;    // (totalDepositAmount / realTotalSupply) * 100
    double circulatingPercentage; // (circulatingSupply / realTotalSupply) * 100
    
    void serialize(CryptoNote::ISerializer &serializer);
  };
};



struct GetDeposit
{
  struct Request
  {
    size_t depositId;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t amount;
    uint64_t term;
    uint64_t interest;
    uint64_t height;
    uint64_t unlockHeight;
    std::string creatingTransactionHash;
    std::string spendingTransactionHash;
    bool locked;
    std::string address;
    bool useStagedUnlock;        // Indicates if this deposit uses staged unlock
    uint64_t transactionFee;     // Transaction fee for this deposit
    uint64_t totalFees;          // Total fees (1x for traditional, 4x for staged)

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetAddresses
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::vector<std::string> addresses;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct CreateAddress
{
  struct Request
  {
    std::string spendSecretKey;
    std::string spendPublicKey;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string address;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct CreateAddressList
{
  struct Request
  {
    std::vector<std::string> spendSecretKeys;
    bool reset;
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::vector<std::string> addresses;
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct DeleteAddress
{
  struct Request
  {
    std::string address;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetSpendKeys
{
  struct Request
  {
    std::string address;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string spendSecretKey;
    std::string spendPublicKey;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetBalance
{
  struct Request
  {
    std::string address;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint64_t availableBalance;
    uint64_t lockedAmount;
    uint64_t lockedDepositBalance;
    uint64_t unlockedDepositBalance;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetBlockHashes
{
  struct Request
  {
    uint32_t firstBlockIndex;
    uint32_t blockCount;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::vector<std::string> blockHashes;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct TransactionHashesInBlockRpcInfo
{
  std::string blockHash;
  std::vector<std::string> transactionHashes;

  void serialize(CryptoNote::ISerializer &serializer);
};

struct GetTransactionHashes
{
  struct Request
  {
    std::vector<std::string> addresses;
    std::string blockHash;
    uint32_t firstBlockIndex = std::numeric_limits<uint32_t>::max();
    uint32_t blockCount;
    std::string paymentId;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::vector<TransactionHashesInBlockRpcInfo> items;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct CreateIntegrated
{
  struct Request
  {
    std::string address;
    std::string payment_id;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string integrated_address;
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct SplitIntegrated
{
  struct Request
  {
    std::string integrated_address;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string address;
    std::string payment_id;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct TransferRpcInfo
{
  uint8_t type;
  std::string address;
  int64_t amount;
  std::string message;

  void serialize(CryptoNote::ISerializer &serializer);
};

struct TransactionRpcInfo
{
  uint8_t state;
  std::string transactionHash;
  uint32_t blockIndex;
  uint64_t timestamp;
  uint32_t confirmations = 0;
  bool isBase;
  uint64_t unlockTime;
  int64_t amount;
  uint64_t fee;
  std::vector<TransferRpcInfo> transfers;
  std::string extra;
  std::string paymentId;
  size_t firstDepositId = CryptoNote::WALLET_INVALID_DEPOSIT_ID;
  size_t depositCount = 0;

  void serialize(CryptoNote::ISerializer &serializer);
};

struct GetTransaction
{
  struct Request
  {
    std::string transactionHash;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    TransactionRpcInfo transaction;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct TransactionsInBlockRpcInfo
{
  std::string blockHash;
  std::vector<TransactionRpcInfo> transactions;

  void serialize(CryptoNote::ISerializer &serializer);
};

struct GetTransactions
{
  struct Request
  {
    std::vector<std::string> addresses;
    std::string blockHash;
    uint32_t firstBlockIndex = std::numeric_limits<uint32_t>::max();
    uint32_t blockCount;
    std::string paymentId;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::vector<TransactionsInBlockRpcInfo> items;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetUnconfirmedTransactionHashes
{
  struct Request
  {
    std::vector<std::string> addresses;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::vector<std::string> transactionHashes;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct WalletRpcOrder
{
  std::string address;
  uint64_t amount;
  std::string message;

  void serialize(CryptoNote::ISerializer &serializer);
};

struct WalletRpcMessage
{
  std::string address;
  std::string message;

  void serialize(CryptoNote::ISerializer &serializer);
};

struct SendTransaction
{
  struct Request
  {
    std::vector<std::string> sourceAddresses;
    std::vector<WalletRpcOrder> transfers;
    std::string changeAddress;
    uint64_t fee = CryptoNote::parameters::MINIMUM_FEE;
    uint32_t anonymity = DEFAULT_ANONYMITY_LEVEL;
    std::string extra;
    std::string paymentId;
    uint64_t unlockTime = 0;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;
    std::string transactionSecretKey;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};



struct CreateDelayedTransaction
{
  struct Request
  {
    std::vector<std::string> addresses;
    std::vector<WalletRpcOrder> transfers;
    std::string changeAddress;
    uint64_t fee = CryptoNote::parameters::MINIMUM_FEE;
    uint32_t anonymity = DEFAULT_ANONYMITY_LEVEL;
    std::string extra;
    std::string paymentId;
    uint64_t unlockTime = 0;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetDelayedTransactionHashes
{
  struct Request
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::vector<std::string> transactionHashes;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct DeleteDelayedTransaction
{
  struct Request
  {
    std::string transactionHash;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct SendDelayedTransaction
{
  struct Request
  {
    std::string transactionHash;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct GetMessagesFromExtra
{
  struct Request
  {
    std::string extra;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::vector<std::string> messages;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct EstimateFusion
{
  struct Request
  {
    uint64_t threshold;
    std::vector<std::string> addresses;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    uint32_t fusionReadyCount;
    uint32_t totalOutputCount;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

struct SendFusionTransaction
{
  struct Request
  {
    uint64_t threshold;
    uint32_t anonymity = 0;
    std::vector<std::string> addresses;
    std::string destinationAddress;

    void serialize(CryptoNote::ISerializer &serializer);
  };

  struct Response
  {
    std::string transactionHash;

    void serialize(CryptoNote::ISerializer &serializer);
  };
};

} //namespace PaymentService
