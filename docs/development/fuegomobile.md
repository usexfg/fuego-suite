# Fuego Mobile SDK - Development Plan

> **Status**: Active Development  
> **Priority**: P0 - High Priority  
> **Timeline**: 8-10 weeks (ASAP)  
> **Target**: Parallel Android + iOS development with Flutter SDK

---

## Executive Summary

Fuego Mobile SDK enables developers to integrate full Fuego node functionality into mobile applications. This plan outlines the architecture, implementation phases, and technical specifications for building a production-ready mobile SDK supporting **Android**, **iOS**, and **Flutter**.

### Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **Platform Strategy** | Parallel (Android + iOS) | Maximize market reach, shared C API layer |
| **Node Mode** | Light/Pruned | Mobile resource constraints (2-5GB storage vs 50GB full) |
| **Feature Scope** | Fully Featured | Wallet, CD, Atomic Swaps, P2P sync |
| **Framework** | Flutter First | Cross-platform code reuse, single codebase |
| **Timeline** | Aggressive (8-10 weeks) | Market demand, competitive pressure |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      Flutter Application                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │  FuegoSDK   │  │  FuegoWallet│  │  FuegoSwap/CD Widgets   │ │
│  │   (Dart)    │  │   (Dart)    │  │         (Dart)          │ │
│  └──────┬──────┘  └──────┬──────┘  └────────────┬────────────┘ │
└─────────┼────────────────┼──────────────────────┼──────────────┘
          │                │                      │
┌─────────▼────────────────▼──────────────────────▼──────────────┐
│                   Platform Channel Bridge                       │
│  ┌────────────────────────┐    ┌─────────────────────────────┐ │
│  │   Android (Kotlin)     │    │      iOS (Swift)            │ │
│  │  FuegoNode.kt          │    │  FuegoNode.swift            │ │
│  │  FuegoWallet.kt        │    │  FuegoWallet.swift          │ │
│  └───────────┬────────────┘    └─────────────┬───────────────┘ │
└──────────────┼────────────────────────────────┼────────────────┘
               │                                │
┌──────────────▼────────────────────────────────▼────────────────┐
│                      C API Layer (Shared)                       │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  fuego_core.h  │  fuego_wallet.h  │  fuego_swap.h       │  │
│  │  fuego_node.h  │  fuego_cd.h      │  fuego_types.h      │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
               │
┌──────────────▼─────────────────────────────────────────────────┐
│                  Fuego Core Libraries (C++)                     │
│  ┌──────────────┐ ┌──────────────┐ ┌─────────────────────────┐ │
│  │ libCrypto.a  │ │ libWallet.a  │ │ libCryptoNoteCore.a     │ │
│  │ libSystem.a  │ │ libP2p.a     │ │ libSerialization.a      │ │
│  └──────────────┘ └──────────────┘ └─────────────────────────┘ │
│                                                                 │
│  Mobile Optimizations:                                          │
│  • Pruned blockchain (last 10k blocks)                         │
│  • Reduced peer count (50 vs 500)                              │
│  • Compact block relay                                         │
│  • Memory-mapped storage                                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Mobile Core Optimizations (Weeks 1-3)

### 1.1 CMake Mobile Toolchain Configuration

**File: `cmake/android.toolchain.cmake`**
```cmake
# Android NDK Toolchain for Fuego Mobile
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 24)  # API 24 minimum
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK})
set(CMAKE_ANDROID_STL_TYPE c++_static)

# Mobile-specific compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Oz -fdata-sections -ffunction-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections -Wl,--strip-all")

# Mobile build definitions
add_definitions(-DMOBILE_BUILD=1)
add_definitions(-DLIGHT_NODE=1)
add_definitions(-DENABLE_PRUNING=1)
```

**File: `cmake/ios.toolchain.cmake`**
```cmake
# iOS Toolchain for Fuego Mobile
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "iOS architectures")

# iOS-specific compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Oz -fdata-sections -ffunction-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-dead_strip")

# Mobile build definitions
add_definitions(-DMOBILE_BUILD=1)
add_definitions(-DLIGHT_NODE=1)
add_definitions(-DENABLE_PRUNING=1)
```

### 1.2 Mobile-Specific Configuration

**File: `src/CryptoNoteConfigMobile.h`**
```cpp
#pragma once

#include "CryptoNoteConfig.h"

#ifdef MOBILE_BUILD

namespace CryptoNote {
namespace parameters {

// === Storage Optimizations ===
// Pruned mode: keep only last N blocks (~2GB vs 50GB full)
const uint32_t MOBILE_PRUNING_DEPTH = 10000;  // Last 10k blocks

// Reduced blockchain windows for memory efficiency
const size_t BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW_MOBILE = 11;
const size_t DIFFICULTY_WINDOW_MOBILE = 45;  // Smaller window

// Block size limits (reduce memory pressure)
const size_t CRYPTONOTE_MAX_BLOCK_BLOB_SIZE_MOBILE = 2000000;  // 2MB vs 8MB

// === Network Optimizations ===
// Reduced peer count for mobile
const size_t MOBILE_MAX_PEERS = 50;  // vs 500 on desktop
const size_t MOBILE_MAX_OUTBOUND_PEERS = 10;
const size_t MOBILE_MAX_INBOUND_PEERS = 5;

// Connection timeouts (battery-aware)
const uint32_t MOBILE_HANDSHAKE_TIMEOUT = 10000;  // 10s vs 30s
const uint32_t MOBILE_PING_TIMEOUT = 5000;  // 5s vs 15s

// Sync optimizations
const uint32_t MOBILE_SYNC_BATCH_SIZE = 100;  // Blocks per batch
const uint32_t MOBILE_FAST_SYNC_THRESHOLD = 1000;  // Use fast sync if >1000 blocks behind

// === Memory Optimizations ===
// Reduced cache sizes
const size_t MOBILE_BLOCK_CACHE_SIZE = 100;  // vs 1000 on desktop
const size_t MOBILE_TX_POOL_MAX_SIZE = 500;  // vs 5000 on desktop

// === Power Management ===
// Battery-aware sync intervals
const uint32_t MOBILE_SYNC_INTERVAL_PLUGGED = 30;  // 30 seconds when charging
const uint32_t MOBILE_SYNC_INTERVAL_BATTERY = 300;  // 5 minutes on battery
const uint32_t MOBILE_SYNC_INTERVAL_BACKGROUND = 900;  // 15 minutes in background

// === Feature Flags ===
const bool MOBILE_ENABLE_FULL_SYNC = false;  // Disable full sync by default
const bool MOBILE_ENABLE_ATOMIC_SWAPS = true;  // Enable swaps on mobile
const bool MOBILE_ENABLE_CD_OPERATIONS = true;  // Enable CD operations

}  // namespace parameters
}  // namespace CryptoNote

#endif  // MOBILE_BUILD
```

### 1.3 Build System Modifications

**File: `CMakeLists.txt` (mobile additions)**
```cmake
# === Mobile Build Configuration ===
option(MOBILE_BUILD "Build for mobile platforms" OFF)
option(LIGHT_NODE "Enable light/pruned node mode" OFF)
option(ENABLE_PRUNING "Enable blockchain pruning" OFF)

if(MOBILE_BUILD)
    message(STATUS "Building Fuego Mobile SDK")
    add_definitions(-DMOBILE_BUILD=1)
    
    if(LIGHT_NODE)
        add_definitions(-DLIGHT_NODE=1)
        message(STATUS "Light node mode enabled")
    endif()
    
    if(ENABLE_PRUNING)
        add_definitions(-DENABLE_PRUNING=1)
        message(STATUS "Blockchain pruning enabled")
    endif()
    
    # Mobile-specific optimizations
    if(NOT MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Oz -fdata-sections -ffunction-sections")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
    endif()
endif()

# === Mobile SDK Library Targets ===
if(MOBILE_BUILD)
    # Shared C API library
    add_library(fuego_mobile_api SHARED
        src/mobile/fuego_core.cpp
        src/mobile/fuego_wallet.cpp
        src/mobile/fuego_node.cpp
        src/mobile/fuego_swap.cpp
        src/mobile/fuego_cd.cpp
    )
    
    target_link_libraries(fuego_mobile_api
        libCryptoNoteCore.a
        libWallet.a
        libP2p.a
        libCrypto.a
        libSerialization.a
        ${Boost_LIBRARIES}
    )
    
    target_include_directories(fuego_mobile_api PUBLIC
        src/mobile/include
    )
endif()
```

---

## Phase 2: C API Layer (Weeks 2-4)

### 2.1 Core C API Header

**File: `src/mobile/include/fuego_core.h`**
```c
#ifndef FUEGO_CORE_H
#define FUEGO_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Type Definitions
// ============================================================================

// Opaque handles
typedef struct FuegoNode FuegoNode;
typedef struct FuegoWallet FuegoWallet;
typedef struct FuegoSwap FuegoSwap;
typedef struct FuegoCD FuegoCD;

// Status codes
typedef enum {
    FUEGO_SUCCESS = 0,
    FUEGO_ERROR_UNKNOWN = 1,
    FUEGO_ERROR_INVALID_PARAM = 2,
    FUEGO_ERROR_INSUFFICIENT_FUNDS = 3,
    FUEGO_ERROR_NETWORK_ERROR = 4,
    FUEGO_ERROR_SYNC_IN_PROGRESS = 5,
    FUEGO_ERROR_NOT_INITIALIZED = 6,
    FUEGO_ERROR_ALREADY_EXISTS = 7,
    FUEGO_ERROR_NOT_FOUND = 8
} FuegoStatus;

// Node configuration
typedef struct {
    const char* data_dir;           // Blockchain data directory
    const char* config_file;        // Config file path (optional)
    bool light_mode;                // Enable light/pruned mode
    uint32_t pruning_depth;         // Number of blocks to keep (0 = disabled)
    uint16_t port;                  // P2P port (default: 8167)
    uint16_t rpc_port;              // RPC port (default: 8168)
    bool testnet;                   // Use testnet
    const char* seed_nodes;         // Comma-separated seed nodes (optional)
    uint32_t max_peers;             // Maximum peer connections (default: 50)
    bool background_mode;           // Optimize for background operation
} FuegoNodeConfig;

// Wallet balance
typedef struct {
    uint64_t total;                 // Total balance (atomic units)
    uint64_t available;             // Available balance (spendable)
    uint64_t locked;                // Locked balance (in transactions)
    uint64_t cd_locked;             // Locked in Certificate of Deposits
} FuegoBalance;

// Transaction info
typedef struct {
    char hash[65];                  // Transaction hash (hex string)
    uint64_t amount;                // Amount (atomic units)
    uint64_t fee;                   // Transaction fee
    uint32_t block_height;          // Block height (0 = unconfirmed)
    uint64_t timestamp;             // Transaction timestamp
    bool incoming;                  // true = received, false = sent
    const char* address;            // Counterparty address
    const char* payment_id;         // Payment ID (optional)
} FuegoTransaction;

// Sync progress
typedef struct {
    uint32_t current_height;        // Current synced height
    uint32_t target_height;         // Target blockchain height
    uint32_t peers_connected;       // Number of connected peers
    double sync_percentage;         // Sync progress (0.0 - 100.0)
    uint64_t download_speed;        // Download speed (bytes/sec)
    bool is_synced;                 // true if fully synced
} FuegoSyncProgress;

// Callback function types
typedef void (*FuegoStatusCallback)(FuegoStatus status, const char* message, void* user_data);
typedef void (*FuegoBalanceCallback)(const FuegoBalance* balance, void* user_data);
typedef void (*FuegoTransactionCallback)(const FuegoTransaction* tx, void* user_data);
typedef void (*FuegoSyncProgressCallback)(const FuegoSyncProgress* progress, void* user_data);
typedef void (*FuegoSwapCallback)(const FuegoSwap* swap, void* user_data);
typedef void (*FuegoCDCallback)(const FuegoCD* cd, void* user_data);

// ============================================================================
// Node Lifecycle
// ============================================================================

/**
 * Create a new Fuego node instance
 * @param config Node configuration
 * @param callback Status callback for async events
 * @param user_data User data passed to callback
 * @return FuegoNode handle or NULL on failure
 */
FuegoNode* fuego_node_create(const FuegoNodeConfig* config, 
                              FuegoStatusCallback callback, 
                              void* user_data);

/**
 * Destroy a Fuego node instance and free resources
 * @param node FuegoNode handle
 */
void fuego_node_destroy(FuegoNode* node);

/**
 * Start the Fuego node (begins P2P sync)
 * @param node FuegoNode handle
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_node_start(FuegoNode* node);

/**
 * Stop the Fuego node gracefully
 * @param node FuegoNode handle
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_node_stop(FuegoNode* node);

/**
 * Check if node is fully synced
 * @param node FuegoNode handle
 * @return true if synced, false otherwise
 */
bool fuego_node_is_synced(const FuegoNode* node);

/**
 * Get current sync progress
 * @param node FuegoNode handle
 * @param progress Output sync progress struct
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_node_get_sync_progress(const FuegoNode* node, FuegoSyncProgress* progress);

/**
 * Get current blockchain height
 * @param node FuegoNode handle
 * @return Block height or 0 on error
 */
uint32_t fuego_node_get_height(const FuegoNode* node);

/**
 * Get number of connected peers
 * @param node FuegoNode handle
 * @return Peer count
 */
uint32_t fuego_node_get_peer_count(const FuegoNode* node);

// ============================================================================
// Wallet Operations
// ============================================================================

/**
 * Create a new wallet attached to a node
 * @param node FuegoNode handle (must be started)
 * @param seed Wallet seed phrase (12 or 24 words)
 * @param password Encryption password (optional, can be NULL)
 * @param callback Status callback for async events
 * @param user_data User data passed to callback
 * @return FuegoWallet handle or NULL on failure
 */
FuegoWallet* fuego_wallet_create(FuegoNode* node, 
                                  const char* seed, 
                                  const char* password,
                                  FuegoStatusCallback callback, 
                                  void* user_data);

/**
 * Create a new wallet with random seed
 * @param node FuegoNode handle (must be started)
 * @param out_seed Buffer to store generated seed (min 256 bytes)
 * @param password Encryption password (optional, can be NULL)
 * @param callback Status callback for async events
 * @param user_data User data passed to callback
 * @return FuegoWallet handle or NULL on failure
 */
FuegoWallet* fuego_wallet_create_new(FuegoNode* node,
                                      char* out_seed,
                                      const char* password,
                                      FuegoStatusCallback callback,
                                      void* user_data);

/**
 * Destroy a wallet instance and zero sensitive data
 * @param wallet FuegoWallet handle
 */
void fuego_wallet_destroy(FuegoWallet* wallet);

/**
 * Get wallet balance
 * @param wallet FuegoWallet handle
 * @param balance Output balance struct
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_get_balance(const FuegoWallet* wallet, FuegoBalance* balance);

/**
 * Send XFG to an address
 * @param wallet FuegoWallet handle
 * @param address Destination address
 * @param amount Amount to send (atomic units)
 * @param fee Transaction fee (0 = use default)
 * @param payment_id Payment ID (optional, can be NULL)
 * @param mixin_count Ring signature mixin (0 = use default)
 * @param out_tx_hash Buffer to store transaction hash (min 65 bytes)
 * @param callback Callback for transaction confirmation
 * @param user_data User data passed to callback
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_send(FuegoWallet* wallet,
                               const char* address,
                               uint64_t amount,
                               uint64_t fee,
                               const char* payment_id,
                               uint32_t mixin_count,
                               char* out_tx_hash,
                               FuegoTransactionCallback callback,
                               void* user_data);

/**
 * Get wallet transaction history
 * @param wallet FuegoWallet handle
 * @param count Number of transactions to retrieve (0 = all)
 * @param offset Offset for pagination
 * @param out_transactions Output array of transactions (caller must free)
 * @param out_count Actual number of transactions returned
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_get_transactions(const FuegoWallet* wallet,
                                           uint32_t count,
                                           uint32_t offset,
                                           FuegoTransaction** out_transactions,
                                           uint32_t* out_count);

/**
 * Get primary wallet address
 * @param wallet FuegoWallet handle
 * @return Address string (caller must not free)
 */
const char* fuego_wallet_get_address(const FuegoWallet* wallet);

/**
 * Get wallet seed phrase
 * @param wallet FuegoWallet handle
 * @param out_seed Buffer to store seed (min 256 bytes)
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_get_seed(const FuegoWallet* wallet, char* out_seed);

/**
 * Export wallet keys to file
 * @param wallet FuegoWallet handle
 * @param filename Output file path
 * @param password Encryption password
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_export_keys(const FuegoWallet* wallet, 
                                      const char* filename, 
                                      const char* password);

/**
 * Import wallet keys from file
 * @param node FuegoNode handle
 * @param filename Input file path
 * @param password Decryption password
 * @param callback Status callback
 * @param user_data User data
 * @return FuegoWallet handle or NULL on failure
 */
FuegoWallet* fuego_wallet_import_keys(FuegoNode* node,
                                       const char* filename,
                                       const char* password,
                                       FuegoStatusCallback callback,
                                       void* user_data);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Validate a Fuego address
 * @param address Address string to validate
 * @param testnet true for testnet addresses, false for mainnet
 * @return true if valid, false otherwise
 */
bool fuego_validate_address(const char* address, bool testnet);

/**
 * Parse amount string to atomic units
 * @param amount_str Amount string (e.g., "1.5" for 1.5 XFG)
 * @param out_amount Output amount in atomic units
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_parse_amount(const char* amount_str, uint64_t* out_amount);

/**
 * Format atomic units to human-readable string
 * @param amount Amount in atomic units
 * @param out_buffer Output buffer (min 32 bytes)
 * @param buffer_size Buffer size
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_format_amount(uint64_t amount, char* out_buffer, size_t buffer_size);

/**
 * Get library version string
 * @return Version string (e.g., "0.2.0-mobile")
 */
const char* fuego_get_version(void);

#ifdef __cplusplus
}
#endif

#endif  // FUEGO_CORE_H
```

### 2.2 Wallet C API Header

**File: `src/mobile/include/fuego_wallet.h`**
```c
#ifndef FUEGO_WALLET_H
#define FUEGO_WALLET_H

#include "fuego_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Subaddress Management
// ============================================================================

/**
 * Create a new subaddress
 * @param wallet FuegoWallet handle
 * @param account_index Account index (0 = primary account)
 * @param label Subaddress label (optional)
 * @param out_address Buffer to store subaddress (min 128 bytes)
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_create_subaddress(FuegoWallet* wallet,
                                            uint32_t account_index,
                                            const char* label,
                                            char* out_address);

/**
 * Get all subaddresses for an account
 * @param wallet FuegoWallet handle
 * @param account_index Account index
 * @param out_addresses Output array of addresses (caller must free)
 * @param out_count Number of addresses returned
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_get_subaddresses(const FuegoWallet* wallet,
                                           uint32_t account_index,
                                           char*** out_addresses,
                                           uint32_t* out_count);

// ============================================================================
// Integrated Addresses
// ============================================================================

/**
 * Create an integrated address with payment ID
 * @param wallet FuegoWallet handle
 * @param payment_id Payment ID (8 bytes hex string)
 * @param out_address Buffer to store integrated address (min 128 bytes)
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_create_integrated_address(FuegoWallet* wallet,
                                                    const char* payment_id,
                                                    char* out_address);

// ============================================================================
// Address Book
// ============================================================================

typedef struct {
    uint32_t id;
    const char* address;
    const char* label;
    const char* payment_id;
    uint64_t created_at;
} FuegoAddressBookEntry;

/**
 * Add address to address book
 * @param wallet FuegoWallet handle
 * @param address Address to save
 * @param label Address label
 * @param payment_id Default payment ID (optional)
 * @param out_id Output address book entry ID
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_add_address_book_entry(FuegoWallet* wallet,
                                                 const char* address,
                                                 const char* label,
                                                 const char* payment_id,
                                                 uint32_t* out_id);

/**
 * Get all address book entries
 * @param wallet FuegoWallet handle
 * @param out_entries Output array of entries (caller must free)
 * @param out_count Number of entries returned
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_get_address_book(const FuegoWallet* wallet,
                                           FuegoAddressBookEntry** out_entries,
                                           uint32_t* out_count);

/**
 * Delete address book entry
 * @param wallet FuegoWallet handle
 * @param id Entry ID to delete
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_wallet_delete_address_book_entry(FuegoWallet* wallet, uint32_t id);

#ifdef __cplusplus
}
#endif

#endif  // FUEGO_WALLET_H
```

### 2.3 CD (Certificate of Deposit) C API Header

**File: `src/mobile/include/fuego_cd.h`**
```c
#ifndef FUEGO_CD_H
#define FUEGO_CD_H

#include "fuego_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CD Types
// ============================================================================

// CD term options (in blocks)
typedef enum {
    CD_TERM_3_MONTHS = 16440,      // ~3 months (90 days)
    CD_TERM_6_MONTHS = 32880,      // ~6 months (180 days)
    CD_TERM_12_MONTHS = 65760,     // ~12 months (365 days)
    CD_TERM_24_MONTHS = 131520     // ~24 months (730 days)
} FuegoCDTerm;

// CD status
typedef enum {
    CD_STATUS_LOCKED = 0,          // Funds locked, earning interest
    CD_STATUS_UNLOCKED = 1,        // Term complete, ready to withdraw
    CD_STATUS_WITHDRAWN = 2,       // Already withdrawn
    CD_STATUS_TRANSFERRED = 3      // Transferred to another wallet
} FuegoCDStatus;

// CD info
typedef struct {
    uint64_t commitment_id;         // Unique commitment identifier
    uint64_t amount;                // Principal amount (atomic units)
    uint64_t interest;              // Accrued interest (atomic units)
    uint64_t total;                 // Total (principal + interest)
    uint32_t creation_height;       // Block height when CD was created
    uint32_t unlock_height;         // Block height when CD unlocks
    uint32_t term;                  // Term in blocks
    FuegoCDStatus status;           // Current status
    uint64_t epoch_rate;            // Epoch fee rate (fixed-point)
    bool transferable;              // Can be transferred
} FuegoCDInfo;

// ============================================================================
// CD Operations
// ============================================================================

/**
 * Create a new Certificate of Deposit
 * @param wallet FuegoWallet handle
 * @param amount Principal amount (atomic units)
 * @param term CD term in blocks (use FuegoCDTerm enum values)
 * @param fee Transaction fee (0 = use default)
 * @param out_commitment_id Output commitment ID
 * @param callback Callback for CD confirmation
 * @param user_data User data passed to callback
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_cd_create(FuegoWallet* wallet,
                             uint64_t amount,
                             uint32_t term,
                             uint64_t fee,
                             uint64_t* out_commitment_id,
                             FuegoCDCallback callback,
                             void* user_data);

/**
 * Withdraw a matured CD
 * @param wallet FuegoWallet handle
 * @param commitment_id Commitment ID to withdraw
 * @param fee Transaction fee (0 = use default)
 * @param out_tx_hash Buffer to store transaction hash (min 65 bytes)
 * @param callback Callback for withdrawal confirmation
 * @param user_data User data passed to callback
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_cd_withdraw(FuegoWallet* wallet,
                               uint64_t commitment_id,
                               uint64_t fee,
                               char* out_tx_hash,
                               FuegoCDCallback callback,
                               void* user_data);

/**
 * Get CD information by commitment ID
 * @param wallet FuegoWallet handle
 * @param commitment_id Commitment ID
 * @param out_info Output CD info struct
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_cd_get_info(const FuegoWallet* wallet,
                               uint64_t commitment_id,
                               FuegoCDInfo* out_info);

/**
 * Get all CDs for a wallet
 * @param wallet FuegoWallet handle
 * @param status Filter by status (use -1 for all)
 * @param out_cds Output array of CD info (caller must free)
 * @param out_count Number of CDs returned
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_cd_get_all(const FuegoWallet* wallet,
                              int status_filter,
                              FuegoCDInfo** out_cds,
                              uint32_t* out_count);

/**
 * Get total CD balance (principal + interest)
 * @param wallet FuegoWallet handle
 * @param out_total Output total CD balance (atomic units)
 * @param out_locked Output locked CD balance (atomic units)
 * @param out_unlocked Output unlocked CD balance (atomic units)
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_cd_get_balance(const FuegoWallet* wallet,
                                  uint64_t* out_total,
                                  uint64_t* out_locked,
                                  uint64_t* out_unlocked);

/**
 * Estimate CD interest at current rates
 * @param wallet FuegoWallet handle
 * @param amount Principal amount (atomic units)
 * @param term CD term in blocks
 * @param out_interest Estimated interest (atomic units)
 * @param out_apy Annual percentage yield (fixed-point, divide by 10000 for %)
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_cd_estimate_interest(const FuegoWallet* wallet,
                                        uint64_t amount,
                                        uint32_t term,
                                        uint64_t* out_interest,
                                        uint64_t* out_apy);

/**
 * Transfer a CD to another wallet (if transferable)
 * @param wallet FuegoWallet handle
 * @param commitment_id Commitment ID to transfer
 * @param recipient_address Recipient wallet address
 * @param fee Transaction fee (0 = use default)
 * @param out_tx_hash Buffer to store transaction hash (min 65 bytes)
 * @param callback Callback for transfer confirmation
 * @param user_data User data passed to callback
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_cd_transfer(FuegoWallet* wallet,
                               uint64_t commitment_id,
                               const char* recipient_address,
                               uint64_t fee,
                               char* out_tx_hash,
                               FuegoCDCallback callback,
                               void* user_data);

#ifdef __cplusplus
}
#endif

#endif  // FUEGO_CD_H
```

### 2.4 Atomic Swap C API Header

**File: `src/mobile/include/fuego_swap.h`**
```c
#ifndef FUEGO_SWAP_H
#define FUEGO_SWAP_H

#include "fuego_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Swap Types
// ============================================================================

// Supported swap chains
typedef enum {
    SWAP_CHAIN_BTC = 0,      // Bitcoin
    SWAP_CHAIN_ETH = 1,      // Ethereum
    SWAP_CHAIN_SOL = 2,      // Solana
    SWAP_CHAIN_XMR = 3,      // Monero
    SWAP_CHAIN_BCH = 4,      // Bitcoin Cash
    SWAP_CHAIN_LTC = 5,      // Litecoin
    SWAP_CHAIN_USDT = 6      // Tether (ERC-20)
} FuegoSwapChain;

// Swap status
typedef enum {
    SWAP_STATUS_INITIATED = 0,    // Swap initiated, waiting for counterparty
    SWAP_STATUS_LOCKED = 1,       // Funds locked, counterparty detected
    SWAP_STATUS_CLAIMED = 2,      // Counterparty claimed, can claim
    SWAP_STATUS_REFUNDED = 3,     // Refunded (timeout or cancellation)
    SWAP_STATUS_COMPLETED = 4,    // Swap completed successfully
    SWAP_STATUS_FAILED = 5        // Swap failed
} FuegoSwapStatus;

// Swap direction
typedef enum {
    SWAP_DIRECTION_SELL = 0,      // Sell XFG for other chain
    SWAP_DIRECTION_BUY = 1        // Buy XFG with other chain
} FuegoSwapDirection;

// Swap info
typedef struct {
    char swap_id[65];             // Unique swap identifier
    FuegoSwapChain chain;         // Target blockchain
    FuegoSwapDirection direction; // Buy or sell
    uint64_t xfg_amount;          // XFG amount (atomic units)
    uint64_t other_amount;        // Other chain amount (in smallest unit)
    double exchange_rate;         // Exchange rate
    FuegoSwapStatus status;       // Current status
    uint64_t created_at;          // Creation timestamp
    uint64_t expires_at;          // Expiration timestamp
    uint32_t confirmations;       // Number of confirmations
    const char* other_address;    // Counterparty address on other chain
    const char* tx_hash;          // Transaction hash on other chain
    const char* secret_hash;      // Hash of secret (for HTLC)
    const char* secret;           // Secret (revealed after claim)
} FuegoSwapInfo;

// Swap offer
typedef struct {
    FuegoSwapChain chain;
    FuegoSwapDirection direction;
    uint64_t min_amount;
    uint64_t max_amount;
    double rate;
    uint64_t liquidity;
} FuegoSwapOffer;

// ============================================================================
// Swap Operations
// ============================================================================

/**
 * Get available swap offers
 * @param wallet FuegoWallet handle
 * @param chain Target blockchain
 * @param out_offers Output array of offers (caller must free)
 * @param out_count Number of offers returned
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_get_offers(const FuegoWallet* wallet,
                                   FuegoSwapChain chain,
                                   FuegoSwapOffer** out_offers,
                                   uint32_t* out_count);

/**
 * Initiate a new atomic swap
 * @param wallet FuegoWallet handle
 * @param chain Target blockchain
 * @param direction Buy or sell
 * @param xfg_amount XFG amount (atomic units)
 * @param min_other_amount Minimum acceptable amount on other chain
 * @param out_swap_id Output swap ID (min 65 bytes)
 * @param callback Callback for swap status updates
 * @param user_data User data passed to callback
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_initiate(FuegoWallet* wallet,
                                 FuegoSwapChain chain,
                                 FuegoSwapDirection direction,
                                 uint64_t xfg_amount,
                                 uint64_t min_other_amount,
                                 char* out_swap_id,
                                 FuegoSwapCallback callback,
                                 void* user_data);

/**
 * Claim a completed swap (receive funds from other chain)
 * @param wallet FuegoWallet handle
 * @param swap_id Swap ID to claim
 * @param secret Secret revealed by counterparty (for HTLC)
 * @param out_tx_hash Buffer to store claim transaction hash (min 65 bytes)
 * @param callback Callback for claim confirmation
 * @param user_data User data passed to callback
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_claim(FuegoWallet* wallet,
                              const char* swap_id,
                              const char* secret,
                              char* out_tx_hash,
                              FuegoSwapCallback callback,
                              void* user_data);

/**
 * Refund a failed or timed-out swap
 * @param wallet FuegoWallet handle
 * @param swap_id Swap ID to refund
 * @param out_tx_hash Buffer to store refund transaction hash (min 65 bytes)
 * @param callback Callback for refund confirmation
 * @param user_data User data passed to callback
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_refund(FuegoWallet* wallet,
                               const char* swap_id,
                               char* out_tx_hash,
                               FuegoSwapCallback callback,
                               void* user_data);

/**
 * Cancel a pending swap (before counterparty locks funds)
 * @param wallet FuegoWallet handle
 * @param swap_id Swap ID to cancel
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_cancel(FuegoWallet* wallet, const char* swap_id);

/**
 * Get swap information by ID
 * @param wallet FuegoWallet handle
 * @param swap_id Swap ID
 * @param out_info Output swap info struct
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_get_info(const FuegoWallet* wallet,
                                 const char* swap_id,
                                 FuegoSwapInfo* out_info);

/**
 * Get all swaps for a wallet
 * @param wallet FuegoWallet handle
 * @param status_filter Filter by status (use -1 for all)
 * @param out_swaps Output array of swap info (caller must free)
 * @param out_count Number of swaps returned
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_get_all(const FuegoWallet* wallet,
                                int status_filter,
                                FuegoSwapInfo** out_swaps,
                                uint32_t* out_count);

/**
 * Get estimated swap completion time
 * @param chain Target blockchain
 * @param out_minutes Estimated time in minutes
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_get_estimate_time(FuegoSwapChain chain, uint32_t* out_minutes);

// ============================================================================
// Liquidity Pool Operations
// ============================================================================

/**
 * Add liquidity to swap pool
 * @param wallet FuegoWallet handle
 * @param chain Target blockchain
 * @param xfg_amount XFG amount (atomic units)
 * @param other_amount Other chain amount
 * @param out_lp_tokens Output LP tokens received
 * @param callback Callback for confirmation
 * @param user_data User data
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_add_liquidity(FuegoWallet* wallet,
                                      FuegoSwapChain chain,
                                      uint64_t xfg_amount,
                                      uint64_t other_amount,
                                      uint64_t* out_lp_tokens,
                                      FuegoSwapCallback callback,
                                      void* user_data);

/**
 * Remove liquidity from swap pool
 * @param wallet FuegoWallet handle
 * @param chain Target blockchain
 * @param lp_tokens LP tokens to burn
 * @param out_xfg_amount Output XFG amount received
 * @param out_other_amount Output other chain amount received
 * @param callback Callback for confirmation
 * @param user_data User data
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_remove_liquidity(FuegoWallet* wallet,
                                         FuegoSwapChain chain,
                                         uint64_t lp_tokens,
                                         uint64_t* out_xfg_amount,
                                         uint64_t* out_other_amount,
                                         FuegoSwapCallback callback,
                                         void* user_data);

/**
 * Get LP token balance for a chain
 * @param wallet FuegoWallet handle
 * @param chain Target blockchain
 * @param out_balance Output LP token balance
 * @return FUEGO_SUCCESS on success, error code on failure
 */
FuegoStatus fuego_swap_get_lp_balance(const FuegoWallet* wallet,
                                       FuegoSwapChain chain,
                                       uint64_t* out_balance);

#ifdef __cplusplus
}
#endif

#endif  // FUEGO_SWAP_H
```

---

## Phase 3: Flutter SDK (Weeks 4-8)

### 3.1 Flutter Package Structure

```
fuego_sdk/
├── pubspec.yaml
├── README.md
├── CHANGELOG.md
├── LICENSE
├── analysis_options.yaml
├── lib/
│   ├── fuego_sdk.dart              // Main export
│   ├── fuego_node.dart             // Node management
│   ├── fuego_wallet.dart           // Wallet operations
│   ├── fuego_cd.dart               // Certificate of Deposits
│   ├── fuego_swap.dart             // Atomic swaps
│   ├── fuego_types.dart            // Type definitions
│   ├── fuego_exceptions.dart       // Custom exceptions
│   ├── fuego_config.dart           // Configuration
│   ├── internal/
│   │   ├── method_channel.dart     // Platform channel wrapper
│   │   ├── event_channel.dart      // Event stream handler
│   │   └── native_types.dart       // Native type conversions
│   └── widgets/
│       ├── balance_display.dart    // Balance widget
│       ├── transaction_list.dart   // Transaction history widget
│       ├── cd_staking_panel.dart   // CD staking widget
│       ├── swap_panel.dart         // Atomic swap widget
│       └── sync_status.dart        // Sync progress widget
├── android/
│   ├── build.gradle
│   ├── settings.gradle
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── kotlin/org/fuego/sdk/
│       │   ├── FuegoSdkPlugin.kt
│       │   ├── FuegoNodeManager.kt
│       │   ├── FuegoWalletManager.kt
│       │   └── ...
│       └── cpp/
│           ├── CMakeLists.txt
│           └── jni_bridge.cpp
├── ios/
│   ├── fuego_sdk.podspec
│   ├── Classes/
│   │   ├── FuegoSdkPlugin.swift
│   │   ├── FuegoNodeManager.swift
│   │   ├── FuegoWalletManager.swift
│   │   └── ...
│   └── Bridge/
│       ├── fuego-bridge.h
│       └── fuego-bridge.mm
└── example/
    ├── pubspec.yaml
    └── lib/
        ├── main.dart
        ├── screens/
        │   ├── wallet_screen.dart
        │   ├── cd_screen.dart
        │   └── swap_screen.dart
        └── widgets/
```

### 3.2 Flutter SDK Implementation

**File: `lib/fuego_sdk.dart`**
```dart
/// Fuego SDK for Flutter
/// 
/// A complete Flutter SDK for integrating Fuego blockchain functionality
/// into mobile applications. Supports wallet management, CD staking,
/// and atomic swaps.
library fuego_sdk;

// Core exports
export 'fuego_node.dart';
export 'fuego_wallet.dart';
export 'fuego_cd.dart';
export 'fuego_swap.dart';
export 'fuego_types.dart';
export 'fuego_exceptions.dart';
export 'fuego_config.dart';

// Widget exports
export 'widgets/balance_display.dart';
export 'widgets/transaction_list.dart';
export 'widgets/cd_staking_panel.dart';
export 'widgets/swap_panel.dart';
export 'widgets/sync_status.dart';

import 'fuego_node.dart';
import 'fuego_wallet.dart';
import 'fuego_config.dart';
import 'internal/method_channel.dart';
import 'internal/event_channel.dart';

/// Main Fuego SDK class
/// 
/// Provides singleton access to all Fuego functionality.
/// Initialize once at app startup.
class FuegoSDK {
  static FuegoSDK? _instance;
  late final FuegoMethodChannel _methodChannel;
  late final FuegoEventChannel _eventChannel;
  
  FuegoNode? _node;
  FuegoWallet? _wallet;
  
  bool _initialized = false;
  
  /// Private constructor for singleton
  FuegoSDK._() {
    _methodChannel = FuegoMethodChannel();
    _eventChannel = FuegoEventChannel();
  }
  
  /// Get singleton instance
  static FuegoSDK get instance {
    _instance ??= FuegoSDK._();
    return _instance!;
  }
  
  /// Initialize the SDK
  /// 
  /// Must be called before using any other SDK methods.
  /// 
  /// [config] - Optional configuration (uses defaults if null)
  /// 
  /// Throws [FuegoInitializationException] on failure.
  Future<void> init({FuegoConfig? config}) async {
    if (_initialized) {
      return;
    }
    
    try {
      config ??= FuegoConfig.defaultMobile();
      
      await _methodChannel.init(config);
      await _eventChannel.setupStreams();
      
      _initialized = true;
    } catch (e) {
      throw FuegoInitializationException('Failed to initialize SDK: $e');
    }
  }
  
  /// Check if SDK is initialized
  bool get isInitialized => _initialized;
  
  /// Get or create node instance
  Future<FuegoNode> getNode() async {
    if (_node == null) {
      _node = FuegoNode(_methodChannel, _eventChannel);
      await _node!.start();
    }
    return _node!;
  }
  
  /// Get or create wallet instance
  /// 
  /// [seed] - Optional seed phrase (creates new wallet if null)
  /// [password] - Optional encryption password
  Future<FuegoWallet> getWallet({String? seed, String? password}) async {
    if (_wallet == null) {
      final node = await getNode();
      _wallet = FuegoWallet(_methodChannel, _eventChannel, node);
      
      if (seed != null) {
        await _wallet!.import(seed: seed, password: password);
      } else {
        await _wallet!.create(password: password);
      }
    }
    return _wallet!;
  }
  
  /// Disconnect and cleanup resources
  Future<void> dispose() async {
    await _wallet?.dispose();
    await _node?.stop();
    await _eventChannel.close();
    _wallet = null;
    _node = null;
    _initialized = false;
  }
  
  /// Validate a Fuego address
  static Future<bool> validateAddress(String address, {bool testnet = false}) {
    return instance._methodChannel.validateAddress(address, testnet: testnet);
  }
  
  /// Parse amount string to atomic units
  static Future<int> parseAmount(String amount) {
    return instance._methodChannel.parseAmount(amount);
  }
  
  /// Format atomic units to human-readable string
  static Future<String> formatAmount(int amount) {
    return instance._methodChannel.formatAmount(amount);
  }
  
  /// Get SDK version
  static const String version = '0.2.0-mobile';
}
```

**File: `lib/fuego_wallet.dart`**
```dart
import 'fuego_node.dart';
import 'fuego_cd.dart';
import 'fuego_swap.dart';
import 'fuego_types.dart';
import 'fuego_exceptions.dart';
import 'internal/method_channel.dart';
import 'internal/event_channel.dart';

/// Fuego Wallet
/// 
/// Main wallet class for managing XFG, CDs, and atomic swaps.
class FuegoWallet {
  final FuegoMethodChannel _channel;
  final FuegoEventChannel _events;
  final FuegoNode _node;
  
  String? _address;
  FuegoBalance? _balance;
  bool _disposed = false;
  
  /// Internal constructor - use FuegoSDK.getWallet() instead
  FuegoWallet(this._channel, this._events, this._node);
  
  /// Create a new wallet with random seed
  /// 
  /// [password] - Optional encryption password
  /// 
  /// Returns the generated seed phrase (store securely!)
  Future<String> create({String? password}) async {
    if (_disposed) {
      throw FuegoWalletException('Wallet has been disposed');
    }
    
    try {
      final seed = await _channel.walletCreateNew(password: password);
      _address = await _channel.walletGetAddress();
      return seed;
    } catch (e) {
      throw FuegoWalletException('Failed to create wallet: $e');
    }
  }
  
  /// Import existing wallet from seed
  /// 
  /// [seed] - 12 or 24 word seed phrase
  /// [password] - Optional encryption password
  Future<void> import({required String seed, String? password}) async {
    if (_disposed) {
      throw FuegoWalletException('Wallet has been disposed');
    }
    
    try {
      await _channel.walletImport(seed: seed, password: password);
      _address = await _channel.walletGetAddress();
    } catch (e) {
      throw FuegoWalletException('Failed to import wallet: $e');
    }
  }
  
  /// Get wallet address
  Future<String> getAddress() async {
    if (_address == null) {
      _address = await _channel.walletGetAddress();
    }
    return _address!;
  }
  
  /// Get wallet balance
  /// 
  /// Returns cached balance if available, otherwise fetches from node.
  Future<FuegoBalance> getBalance({bool refresh = false}) async {
    if (_disposed) {
      throw FuegoWalletException('Wallet has been disposed');
    }
    
    if (_balance == null || refresh) {
      _balance = await _channel.walletGetBalance();
    }
    return _balance!;
  }
  
  /// Stream of balance updates
  Stream<FuegoBalance> get balanceStream {
    return _events.balanceStream;
  }
  
  /// Send XFG to an address
  /// 
  /// [address] - Destination address
  /// [amount] - Amount in atomic units
  /// [fee] - Transaction fee (null = use default)
  /// [paymentId] - Optional payment ID
  /// [mixin] - Ring signature mixin count (null = use default)
  /// 
  /// Returns transaction hash
  Future<String> send({
    required String address,
    required int amount,
    int? fee,
    String? paymentId,
    int? mixin,
  }) async {
    if (_disposed) {
      throw FuegoWalletException('Wallet has been disposed');
    }
    
    // Validate address
    final isValid = await FuegoSDK.validateAddress(address);
    if (!isValid) {
      throw FuegoValidationException('Invalid destination address');
    }
    
    // Check balance
    final balance = await getBalance();
    if (balance.available < amount) {
      throw FuegoInsufficientFundsException(
        'Insufficient funds: available ${balance.available}, requested $amount',
      );
    }
    
    try {
      return await _channel.walletSend(
        address: address,
        amount: amount,
        fee: fee,
        paymentId: paymentId,
        mixin: mixin,
      );
    } catch (e) {
      throw FuegoTransactionException('Failed to send transaction: $e');
    }
  }
  
  /// Get transaction history
  /// 
  /// [count] - Number of transactions (null = all)
  /// [offset] - Offset for pagination
  Future<List<FuegoTransaction>> getTransactions({
    int? count,
    int offset = 0,
  }) async {
    if (_disposed) {
      throw FuegoWalletException('Wallet has been disposed');
    }
    
    return await _channel.walletGetTransactions(count: count, offset: offset);
  }
  
  /// Stream of new transactions
  Stream<FuegoTransaction> get transactionStream {
    return _events.transactionStream;
  }
  
  /// Create a subaddress
  /// 
  /// [accountIndex] - Account index (default: 0)
  /// [label] - Optional label
  Future<String> createSubaddress({
    int accountIndex = 0,
    String? label,
  }) async {
    if (_disposed) {
      throw FuegoWalletException('Wallet has been disposed');
    }
    
    return await _channel.walletCreateSubaddress(
      accountIndex: accountIndex,
      label: label,
    );
  }
  
  /// Get seed phrase
  /// 
  /// WARNING: Handle with care! Store securely!
  Future<String> getSeed() async {
    if (_disposed) {
      throw FuegoWalletException('Wallet has been disposed');
    }
    
    return await _channel.walletGetSeed();
  }
  
  /// Export wallet keys to file
  /// 
  /// [filename] - Output file path
  /// [password] - Encryption password
  Future<void> exportKeys({
    required String filename,
    required String password,
  }) async {
    if (_disposed) {
      throw FuegoWalletException('Wallet has been disposed');
    }
    
    await _channel.walletExportKeys(filename: filename, password: password);
  }
  
  /// CD Operations
  
  /// Create a Certificate of Deposit
  Future<FuegoCD> createCD({
    required int amount,
    required FuegoCDTerm term,
    int? fee,
  }) async {
    final cd = FuegoCD(_channel, _events);
    return await cd.create(amount: amount, term: term, fee: fee);
  }
  
  /// Get all CDs
  Future<List<FuegoCD>> getCDs({int? statusFilter}) async {
    final cd = FuegoCD(_channel, _events);
    return await cd.getAll(statusFilter: statusFilter);
  }
  
  /// Get CD balance
  Future<FuegoCDBalance> getCDBalance() async {
    final cd = FuegoCD(_channel, _events);
    return await cd.getBalance();
  }
  
  /// Swap Operations
  
  /// Get swap offers for a chain
  Future<List<FuegoSwapOffer>> getSwapOffers(FuegoSwapChain chain) async {
    final swap = FuegoSwap(_channel, _events);
    return await swap.getOffers(chain: chain);
  }
  
  /// Initiate atomic swap
  Future<FuegoSwap> initiateSwap({
    required FuegoSwapChain chain,
    required FuegoSwapDirection direction,
    required int xfgAmount,
    required int minOtherAmount,
  }) async {
    final swap = FuegoSwap(_channel, _events);
    return await swap.initiate(
      chain: chain,
      direction: direction,
      xfgAmount: xfgAmount,
      minOtherAmount: minOtherAmount,
    );
  }
  
  /// Get all swaps
  Future<List<FuegoSwap>> getSwaps({int? statusFilter}) async {
    final swap = FuegoSwap(_channel, _events);
    return await swap.getAll(statusFilter: statusFilter);
  }
  
  /// Dispose wallet and free resources
  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    // Native cleanup handled by platform channel
  }
}
```

**File: `lib/fuego_cd.dart`**
```dart
import 'fuego_types.dart';
import 'fuego_exceptions.dart';
import 'internal/method_channel.dart';
import 'internal/event_channel.dart';

/// Certificate of Deposit term options
enum FuegoCDTerm {
  term3Months(16440),   // ~3 months
  term6Months(32880),   // ~6 months
  term12Months(65760),  // ~12 months
  term24Months(131520); // ~24 months
  
  final int blocks;
  const FuegoCDTerm(this.blocks);
  
  int get days => blocks ~/ 180;  // Approximate days (180 blocks/day)
}

/// CD status
enum FuegoCDStatus {
  locked,      // Funds locked, earning interest
  unlocked,    // Term complete, ready to withdraw
  withdrawn,   // Already withdrawn
  transferred; // Transferred to another wallet
}

/// CD information
class FuegoCDInfo {
  final int commitmentId;
  final int amount;
  final int interest;
  final int total;
  final int creationHeight;
  final int unlockHeight;
  final FuegoCDTerm term;
  final FuegoCDStatus status;
  final double epochRate;
  final bool transferable;
  final DateTime? createdAt;
  final DateTime? unlocksAt;
  
  FuegoCDInfo({
    required this.commitmentId,
    required this.amount,
    required this.interest,
    required this.total,
    required this.creationHeight,
    required this.unlockHeight,
    required this.term,
    required this.status,
    required this.epochRate,
    required this.transferable,
    this.createdAt,
    this.unlockHeight,
  });
  
  /// Check if CD can be withdrawn
  bool get canWithdraw => status == FuegoCDStatus.unlocked;
  
  /// Check if CD can be transferred
  bool get canTransfer => transferable && status == FuegoCDStatus.locked;
  
  /// Annual Percentage Yield
  double get apy => epochRate * 365 * 180 / 10000;  // Approximate APY
}

/// CD balance summary
class FuegoCDBalance {
  final int total;
  final int locked;
  final int unlocked;
  final int totalInterest;
  
  FuegoCDBalance({
    required this.total,
    required this.locked,
    required this.unlocked,
    required this.totalInterest,
  });
}

/// Certificate of Deposit manager
class FuegoCD {
  final FuegoMethodChannel _channel;
  final FuegoEventChannel _events;
  
  FuegoCD(this._channel, this._events);
  
  /// Create a new CD
  /// 
  /// [amount] - Principal amount in atomic units
  /// [term] - CD term
  /// [fee] - Transaction fee (null = default)
  Future<FuegoCDInfo> create({
    required int amount,
    required FuegoCDTerm term,
    int? fee,
  }) async {
    try {
      final commitmentId = await _channel.cdCreate(
        amount: amount,
        term: term.blocks,
        fee: fee,
      );
      
      return await getInfo(commitmentId);
    } catch (e) {
      throw FuegoCDException('Failed to create CD: $e');
    }
  }
  
  /// Withdraw a matured CD
  /// 
  /// [commitmentId] - CD to withdraw
  /// [fee] - Transaction fee (null = default)
  /// 
  /// Returns transaction hash
  Future<String> withdraw({
    required int commitmentId,
    int? fee,
  }) async {
    try {
      return await _channel.cdWithdraw(
        commitmentId: commitmentId,
        fee: fee,
      );
    } catch (e) {
      throw FuegoCDException('Failed to withdraw CD: $e');
    }
  }
  
  /// Get CD information
  Future<FuegoCDInfo> getInfo(int commitmentId) async {
    return await _channel.cdGetInfo(commitmentId);
  }
  
  /// Get all CDs
  /// 
  /// [statusFilter] - Filter by status (null = all)
  Future<List<FuegoCDInfo>> getAll({FuegoCDStatus? statusFilter}) async {
    return await _channel.cdGetAll(statusFilter: statusFilter?.index);
  }
  
  /// Get CD balance
  Future<FuegoCDBalance> getBalance() async {
    return await _channel.cdGetBalance();
  }
  
  /// Estimate interest for a CD
  Future<FuegoCDEstimate> estimateInterest({
    required int amount,
    required FuegoCDTerm term,
  }) async {
    return await _channel.cdEstimateInterest(
      amount: amount,
      term: term.blocks,
    );
  }
  
  /// Transfer a CD to another wallet
  /// 
  /// [commitmentId] - CD to transfer
  /// [recipientAddress] - Destination address
  /// [fee] - Transaction fee (null = default)
  Future<String> transfer({
    required int commitmentId,
    required String recipientAddress,
    int? fee,
  }) async {
    try {
      // Validate address
      final isValid = await _channel.validateAddress(recipientAddress);
      if (!isValid) {
        throw FuegoValidationException('Invalid recipient address');
      }
      
      return await _channel.cdTransfer(
        commitmentId: commitmentId,
        recipientAddress: recipientAddress,
        fee: fee,
      );
    } catch (e) {
      throw FuegoCDException('Failed to transfer CD: $e');
    }
  }
  
  /// Stream of CD updates
  Stream<FuegoCDInfo> get updatesStream {
    return _events.cdStream;
  }
}

/// CD interest estimate
class FuegoCDEstimate {
  final int interest;
  final double apy;
  final int total;
  
  FuegoCDEstimate({
    required this.interest,
    required this.apy,
    required this.total,
  });
}
```

**File: `lib/fuego_swap.dart`**
```dart
import 'fuego_types.dart';
import 'fuego_exceptions.dart';
import 'internal/method_channel.dart';
import 'internal/event_channel.dart';

/// Supported swap chains
enum FuegoSwapChain {
  btc('Bitcoin', 'BTC'),
  eth('Ethereum', 'ETH'),
  sol('Solana', 'SOL'),
  xmr('Monero', 'XMR'),
  bch('Bitcoin Cash', 'BCH'),
  ltc('Litecoin', 'LTC'),
  usdt('Tether', 'USDT');
  
  final String name;
  final String symbol;
  
  const FuegoSwapChain(this.name, this.symbol);
}

/// Swap direction
enum FuegoSwapDirection {
  sell,  // Sell XFG for other chain
  buy;   // Buy XFG with other chain
}

/// Swap status
enum FuegoSwapStatus {
  initiated,    // Waiting for counterparty
  locked,       // Funds locked
  claimed,      // Can claim
  refunded,     // Refunded
  completed,    // Completed
  failed;       // Failed
}

/// Swap offer from liquidity provider
class FuegoSwapOffer {
  final FuegoSwapChain chain;
  final FuegoSwapDirection direction;
  final int minAmount;
  final int maxAmount;
  final double rate;
  final int liquidity;
  final Duration estimatedTime;
  
  FuegoSwapOffer({
    required this.chain,
    required this.direction,
    required this.minAmount,
    required this.maxAmount,
    required this.rate,
    required this.liquidity,
    required this.estimatedTime,
  });
  
  /// Calculate output amount for given input
  int calculateOutput(int inputAmount) {
    return (inputAmount * rate).floor();
  }
}

/// Swap information
class FuegoSwapInfo {
  final String swapId;
  final FuegoSwapChain chain;
  final FuegoSwapDirection direction;
  final int xfgAmount;
  final int otherAmount;
  final double rate;
  final FuegoSwapStatus status;
  final DateTime createdAt;
  final DateTime? expiresAt;
  final int confirmations;
  final String? otherAddress;
  final String? txHash;
  final String? secretHash;
  final String? secret;
  
  FuegoSwapInfo({
    required this.swapId,
    required this.chain,
    required this.direction,
    required this.xfgAmount,
    required this.otherAmount,
    required this.rate,
    required this.status,
    required this.createdAt,
    this.expiresAt,
    this.confirmations = 0,
    this.otherAddress,
    this.txHash,
    this.secretHash,
    this.secret,
  });
  
  /// Check if swap can be claimed
  bool get canClaim => status == FuegoSwapStatus.claimed;
  
  /// Check if swap can be refunded
  bool get canRefund => 
      status == FuegoSwapStatus.locked && 
      expiresAt != null && 
      DateTime.now().isAfter(expiresAt!);
  
  /// Check if swap is complete
  bool get isComplete => status == FuegoSwapStatus.completed;
  
  /// Check if swap has failed
  bool get hasFailed => status == FuegoSwapStatus.failed;
}

/// Atomic Swap manager
class FuegoSwap {
  final FuegoMethodChannel _channel;
  final FuegoEventChannel _events;
  
  FuegoSwap(this._channel, this._events);
  
  /// Get available swap offers
  /// 
  /// [chain] - Target blockchain
  Future<List<FuegoSwapOffer>> getOffers({required FuegoSwapChain chain}) async {
    try {
      return await _channel.swapGetOffers(chain: chain);
    } catch (e) {
      throw FuegoSwapException('Failed to get swap offers: $e');
    }
  }
  
  /// Initiate a new atomic swap
  /// 
  /// [chain] - Target blockchain
  /// [direction] - Buy or sell
  /// [xfgAmount] - XFG amount in atomic units
  /// [minOtherAmount] - Minimum acceptable amount on other chain
  Future<FuegoSwapInfo> initiate({
    required FuegoSwapChain chain,
    required FuegoSwapDirection direction,
    required int xfgAmount,
    required int minOtherAmount,
  }) async {
    try {
      final swapId = await _channel.swapInitiate(
        chain: chain,
        direction: direction,
        xfgAmount: xfgAmount,
        minOtherAmount: minOtherAmount,
      );
      
      return await getInfo(swapId);
    } catch (e) {
      throw FuegoSwapException('Failed to initiate swap: $e');
    }
  }
  
  /// Claim a completed swap
  /// 
  /// [swapId] - Swap ID to claim
  /// [secret] - Secret revealed by counterparty (for HTLC)
  /// 
  /// Returns transaction hash
  Future<String> claim({
    required String swapId,
    required String secret,
  }) async {
    try {
      return await _channel.swapClaim(
        swapId: swapId,
        secret: secret,
      );
    } catch (e) {
      throw FuegoSwapException('Failed to claim swap: $e');
    }
  }
  
  /// Refund a failed or timed-out swap
  /// 
  /// [swapId] - Swap ID to refund
  /// 
  /// Returns transaction hash
  Future<String> refund({required String swapId}) async {
    try {
      return await _channel.swapRefund(swapId: swapId);
    } catch (e) {
      throw FuegoSwapException('Failed to refund swap: $e');
    }
  }
  
  /// Cancel a pending swap
  /// 
  /// [swapId] - Swap ID to cancel
  Future<void> cancel({required String swapId}) async {
    try {
      await _channel.swapCancel(swapId: swapId);
    } catch (e) {
      throw FuegoSwapException('Failed to cancel swap: $e');
    }
  }
  
  /// Get swap information
  Future<FuegoSwapInfo> getInfo(String swapId) async {
    return await _channel.swapGetInfo(swapId);
  }
  
  /// Get all swaps
  /// 
  /// [statusFilter] - Filter by status (null = all)
  Future<List<FuegoSwapInfo>> getAll({FuegoSwapStatus? statusFilter}) async {
    return await _channel.swapGetAll(statusFilter: statusFilter?.index);
  }
  
  /// Get estimated completion time for a chain
  Future<Duration> getEstimatedTime(FuegoSwapChain chain) async {
    return await _channel.swapGetEstimatedTime(chain: chain);
  }
  
  /// Add liquidity to swap pool
  /// 
  /// [chain] - Target blockchain
  /// [xfgAmount] - XFG amount
  /// [otherAmount] - Other chain amount
  /// 
  /// Returns LP tokens received
  Future<int> addLiquidity({
    required FuegoSwapChain chain,
    required int xfgAmount,
    required int otherAmount,
  }) async {
    try {
      return await _channel.swapAddLiquidity(
        chain: chain,
        xfgAmount: xfgAmount,
        otherAmount: otherAmount,
      );
    } catch (e) {
      throw FuegoSwapException('Failed to add liquidity: $e');
    }
  }
  
  /// Remove liquidity from swap pool
  /// 
  /// [chain] - Target blockchain
  /// [lpTokens] - LP tokens to burn
  /// 
  /// Returns (XFG amount, other chain amount)
  Future<(int, int)> removeLiquidity({
    required FuegoSwapChain chain,
    required int lpTokens,
  }) async {
    try {
      return await _channel.swapRemoveLiquidity(
        chain: chain,
        lpTokens: lpTokens,
      );
    } catch (e) {
      throw FuegoSwapException('Failed to remove liquidity: $e');
    }
  }
  
  /// Get LP token balance
  Future<int> getLpBalance(FuegoSwapChain chain) async {
    return await _channel.swapGetLpBalance(chain: chain);
  }
  
  /// Stream of swap updates
  Stream<FuegoSwapInfo> get updatesStream {
    return _events.swapStream;
  }
}
```

---

## Phase 4: Platform Implementations (Weeks 5-9)

### 4.1 Android Implementation

**File: `android/src/main/kotlin/org/fuego/sdk/FuegoSdkPlugin.kt`**
```kotlin
package org.fuego.sdk

import android.content.Context
import android.os.Handler
import android.os.Looper
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result

class FuegoSdkPlugin : FlutterPlugin, MethodCallHandler, EventChannel.StreamHandler {
    private lateinit var channel: MethodChannel
    private lateinit var eventChannel: EventChannel
    private lateinit var context: Context
    private lateinit var handler: Handler
    
    private var nodeManager: FuegoNodeManager? = null
    private var walletManager: FuegoWalletManager? = null
    private var eventSink: EventChannel.EventSink? = null
    
    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
        handler = Handler(Looper.getMainLooper())
        
        channel = MethodChannel(binding.binaryMessenger, "org.fuego.sdk/methods")
        channel.setMethodCallHandler(this)
        
        eventChannel = EventChannel(binding.binaryMessenger, "org.fuego.sdk/events")
        eventChannel.setStreamHandler(this)
    }
    
    override fun onMethodCall(call: MethodCall, result: Result) {
        when (call.method) {
            "init" -> handleInit(call, result)
            "nodeStart" -> handleNodeStart(result)
            "nodeStop" -> handleNodeStop(result)
            "nodeGetHeight" -> handleNodeGetHeight(result)
            "walletCreateNew" -> handleWalletCreateNew(call, result)
            "walletImport" -> handleWalletImport(call, result)
            "walletGetAddress" -> handleWalletGetAddress(result)
            "walletGetBalance" -> handleWalletGetBalance(result)
            "walletSend" -> handleWalletSend(call, result)
            "cdCreate" -> handleCdCreate(call, result)
            "cdWithdraw" -> handleCdWithdraw(call, result)
            "swapInitiate" -> handleSwapInitiate(call, result)
            "swapClaim" -> handleSwapClaim(call, result)
            else -> result.notImplemented()
        }
    }
    
    private fun handleInit(call: MethodCall, result: Result) {
        try {
            val config = FuegoConfig(
                dataDir = call.argument<String>("dataDir") ?: context.filesDir.absolutePath,
                lightMode = call.argument<Boolean>("lightMode") ?: true,
                pruningDepth = call.argument<Int>("pruningDepth") ?: 10000,
                testnet = call.argument<Boolean>("testnet") ?: false,
                maxPeers = call.argument<Int>("maxPeers") ?: 50,
                backgroundMode = call.argument<Boolean>("backgroundMode") ?: true
            )
            
            nodeManager = FuegoNodeManager(context, config)
            walletManager = FuegoWalletManager(nodeManager!!)
            
            result.success(true)
        } catch (e: Exception) {
            result.error("INIT_ERROR", e.message, null)
        }
    }
    
    private fun handleNodeStart(result: Result) {
        try {
            nodeManager?.start()
            result.success(true)
        } catch (e: Exception) {
            result.error("NODE_START_ERROR", e.message, null)
        }
    }
    
    private fun handleNodeStop(result: Result) {
        try {
            nodeManager?.stop()
            result.success(true)
        } catch (e: Exception) {
            result.error("NODE_STOP_ERROR", e.message, null)
        }
    }
    
    private fun handleNodeGetHeight(result: Result) {
        try {
            val height = nodeManager?.getHeight() ?: 0
            result.success(height)
        } catch (e: Exception) {
            result.error("NODE_HEIGHT_ERROR", e.message, null)
        }
    }
    
    private fun handleWalletCreateNew(call: MethodCall, result: Result) {
        try {
            val password = call.argument<String>("password")
            val seed = walletManager?.createNew(password)
            result.success(seed)
        } catch (e: Exception) {
            result.error("WALLET_CREATE_ERROR", e.message, null)
        }
    }
    
    private fun handleWalletImport(call: MethodCall, result: Result) {
        try {
            val seed = call.argument<String>("seed")!!
            val password = call.argument<String>("password")
            walletManager?.import(seed, password)
            result.success(true)
        } catch (e: Exception) {
            result.error("WALLET_IMPORT_ERROR", e.message, null)
        }
    }
    
    private fun handleWalletGetAddress(result: Result) {
        try {
            val address = walletManager?.getAddress()
            result.success(address)
        } catch (e: Exception) {
            result.error("WALLET_ADDRESS_ERROR", e.message, null)
        }
    }
    
    private fun handleWalletGetBalance(result: Result) {
        try {
            val balance = walletManager?.getBalance()
            result.success(mapOf(
                "total" to balance?.total,
                "available" to balance?.available,
                "locked" to balance?.locked,
                "cdLocked" to balance?.cdLocked
            ))
        } catch (e: Exception) {
            result.error("WALLET_BALANCE_ERROR", e.message, null)
        }
    }
    
    private fun handleWalletSend(call: MethodCall, result: Result) {
        try {
            val address = call.argument<String>("address")!!
            val amount = call.argument<Long>("amount")!!
            val fee = call.argument<Long>("fee")
            val paymentId = call.argument<String>("paymentId")
            val mixin = call.argument<Int>("mixin")
            
            val txHash = walletManager?.send(address, amount, fee, paymentId, mixin)
            result.success(txHash)
        } catch (e: Exception) {
            result.error("WALLET_SEND_ERROR", e.message, null)
        }
    }
    
    private fun handleCdCreate(call: MethodCall, result: Result) {
        try {
            val amount = call.argument<Long>("amount")!!
            val term = call.argument<Int>("term")!!
            val fee = call.argument<Long>("fee")
            
            val commitmentId = walletManager?.createCd(amount, term, fee)
            result.success(commitmentId)
        } catch (e: Exception) {
            result.error("CD_CREATE_ERROR", e.message, null)
        }
    }
    
    private fun handleCdWithdraw(call: MethodCall, result: Result) {
        try {
            val commitmentId = call.argument<Long>("commitmentId")!!
            val fee = call.argument<Long>("fee")
            
            val txHash = walletManager?.withdrawCd(commitmentId, fee)
            result.success(txHash)
        } catch (e: Exception) {
            result.error("CD_WITHDRAW_ERROR", e.message, null)
        }
    }
    
    private fun handleSwapInitiate(call: MethodCall, result: Result) {
        try {
            val chain = call.argument<Int>("chain")!!
            val direction = call.argument<Int>("direction")!!
            val xfgAmount = call.argument<Long>("xfgAmount")!!
            val minOtherAmount = call.argument<Long>("minOtherAmount")!!
            
            val swapId = walletManager?.initiateSwap(chain, direction, xfgAmount, minOtherAmount)
            result.success(swapId)
        } catch (e: Exception) {
            result.error("SWAP_INITIATE_ERROR", e.message, null)
        }
    }
    
    private fun handleSwapClaim(call: MethodCall, result: Result) {
        try {
            val swapId = call.argument<String>("swapId")!!
            val secret = call.argument<String>("secret")!!
            
            val txHash = walletManager?.claimSwap(swapId, secret)
            result.success(txHash)
        } catch (e: Exception) {
            result.error("SWAP_CLAIM_ERROR", e.message, null)
        }
    }
    
    // EventChannel.StreamHandler
    
    override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
        eventSink = events
        
        // Setup callbacks from native
        nodeManager?.setSyncProgressCallback { progress ->
            handler.post {
                eventSink?.success(mapOf(
                    "type" to "syncProgress",
                    "currentHeight" to progress.currentHeight,
                    "targetHeight" to progress.targetHeight,
                    "percentage" to progress.percentage,
                    "peersConnected" to progress.peersConnected
                ))
            }
        }
        
        walletManager?.setBalanceCallback { balance ->
            handler.post {
                eventSink?.success(mapOf(
                    "type" to "balance",
                    "total" to balance.total,
                    "available" to balance.available,
                    "locked" to balance.locked,
                    "cdLocked" to balance.cdLocked
                ))
            }
        }
        
        walletManager?.setTransactionCallback { tx ->
            handler.post {
                eventSink?.success(mapOf(
                    "type" to "transaction",
                    "hash" to tx.hash,
                    "amount" to tx.amount,
                    "fee" to tx.fee,
                    "blockHeight" to tx.blockHeight,
                    "timestamp" to tx.timestamp,
                    "incoming" to tx.incoming,
                    "address" to tx.address
                ))
            }
        }
    }
    
    override fun onCancel(arguments: Any?) {
        eventSink = null
    }
    
    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
        eventChannel.setStreamHandler(null)
        nodeManager?.stop()
        nodeManager = null
        walletManager = null
    }
}
```

**File: `android/src/main/kotlin/org/fuego/sdk/FuegoNodeManager.kt`**
```kotlin
package org.fuego.sdk

import android.content.Context
import android.content.pm.ServiceInfo
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import java.io.File

/**
 * Fuego Node Manager for Android
 * 
 * Manages the Fuego node lifecycle, including background operation
 * with foreground service for reliable syncing.
 */
class FuegoNodeManager(
    private val context: Context,
    private val config: FuegoConfig
) {
    private var nodePtr: Long = 0
    private var isRunning = false
    private var syncProgressCallback: ((SyncProgress) -> Unit)? = null
    
    data class FuegoConfig(
        val dataDir: String,
        val lightMode: Boolean,
        val pruningDepth: Int,
        val testnet: Boolean,
        val maxPeers: Int,
        val backgroundMode: Boolean
    )
    
    data class SyncProgress(
        val currentHeight: Int,
        val targetHeight: Int,
        val percentage: Double,
        val peersConnected: Int
    )
    
    init {
        // Create data directory
        File(config.dataDir).mkdirs()
        
        // Initialize native node
        nodePtr = nativeNodeCreate(
            config.dataDir,
            config.lightMode,
            config.pruningDepth,
            config.testnet,
            config.maxPeers,
            config.backgroundMode
        )
    }
    
    fun start() {
        if (isRunning) return
        
        // Start foreground service for background sync
        if (config.backgroundMode) {
            startForegroundService()
        }
        
        // Start native node
        nativeNodeStart(nodePtr)
        isRunning = true
        
        // Begin sync progress monitoring
        startSyncMonitoring()
    }
    
    fun stop() {
        if (!isRunning) return
        
        nativeNodeStop(nodePtr)
        isRunning = false
        
        // Stop foreground service
        stopForegroundService()
    }
    
    fun getHeight(): Int {
        return nativeNodeGetHeight(nodePtr)
    }
    
    fun getPeerCount(): Int {
        return nativeNodeGetPeerCount(nodePtr)
    }
    
    fun setSyncProgressCallback(callback: (SyncProgress) -> Unit) {
        syncProgressCallback = callback
    }
    
    private fun startSyncMonitoring() {
        Thread {
            while (isRunning) {
                val progress = SyncProgress(
                    currentHeight = getHeight(),
                    targetHeight = nativeNodeGetTargetHeight(nodePtr),
                    percentage = nativeNodeGetSyncPercentage(nodePtr),
                    peersConnected = getPeerCount()
                )
                
                syncProgressCallback?.invoke(progress)
                
                // Update notification
                if (config.backgroundMode) {
                    updateNotification(progress)
                }
                
                Thread.sleep(5000)  // Update every 5 seconds
            }
        }.start()
    }
    
    private fun startForegroundService() {
        val intent = Intent(context, FuegoNodeService::class.java)
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channelId = createNotificationChannel()
            val notification = NotificationCompat.Builder(context, channelId)
                .setContentTitle("Fuego Node")
                .setContentText("Syncing blockchain...")
                .setSmallIcon(R.drawable.ic_fuego_notification)
                .setOngoing(true)
                .build()
            
            context.startForegroundService(intent)
        } else {
            context.startService(intent)
        }
    }
    
    private fun stopForegroundService() {
        val intent = Intent(context, FuegoNodeService::class.java)
        context.stopService(intent)
    }
    
    private fun createNotificationChannel(): String {
        val channelId = "fuego_node_sync"
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                channelId,
                "Fuego Node Sync",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Shows Fuego node sync progress"
                setShowBadge(false)
            }
            
            val notificationManager = context.getSystemService(NotificationManager::class.java)
            notificationManager.createNotificationChannel(channel)
        }
        
        return channelId
    }
    
    private fun updateNotification(progress: SyncProgress) {
        val notificationManager = context.getSystemService(NotificationManager::class.java)
        val notification = NotificationCompat.Builder(context, "fuego_node_sync")
            .setContentTitle("Fuego Node")
            .setContentText("Syncing: ${progress.percentage}% (${progress.currentHeight}/${progress.targetHeight})")
            .setSmallIcon(R.drawable.ic_fuego_notification)
            .setProgress(100, progress.percentage.toInt(), false)
            .setOngoing(true)
            .build()
        
        notificationManager.notify(1, notification)
    }
    
    // Native methods (implemented in JNI bridge)
    private external fun nativeNodeCreate(
        dataDir: String,
        lightMode: Boolean,
        pruningDepth: Int,
        testnet: Boolean,
        maxPeers: Int,
        backgroundMode: Boolean
    ): Long
    
    private external fun nativeNodeStart(nodePtr: Long)
    private external fun nativeNodeStop(nodePtr: Long)
    private external fun nativeNodeGetHeight(nodePtr: Long): Int
    private external fun nativeNodeGetPeerCount(nodePtr: Long): Int
    private external fun nativeNodeGetTargetHeight(nodePtr: Long): Int
    private external fun nativeNodeGetSyncPercentage(nodePtr: Long): Double
    
    companion object {
        init {
            System.loadLibrary("fuego_mobile_api")
        }
    }
}

/**
 * Foreground Service for Fuego Node
 */
class FuegoNodeService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null
    
    override fun onCreate() {
        super.onCreate()
        // Service created - node is syncing in background
    }
    
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY  // Restart if killed
    }
}
```

### 4.2 iOS Implementation

**File: `ios/Classes/FuegoSdkPlugin.swift`**
```swift
import Flutter
import UIKit
import BackgroundTasks

public class FuegoSdkPlugin: NSObject, FlutterPlugin, FlutterStreamHandler {
    private var methodChannel: FlutterMethodChannel?
    private var eventChannel: FlutterEventChannel?
    private var eventSink: FlutterEventSink?
    
    private var nodeManager: FuegoNodeManager?
    private var walletManager: FuegoWalletManager?
    
    public static func register(with registrar: FlutterPluginRegistrar) {
        let instance = FuegoSdkPlugin()
        
        instance.methodChannel = FlutterMethodChannel(
            name: "org.fuego.sdk/methods",
            binaryMessenger: registrar.messenger()
        )
        
        instance.eventChannel = FlutterEventChannel(
            name: "org.fuego.sdk/events",
            binaryMessenger: registrar.messenger()
        )
        
        registrar.addMethodCallDelegate(instance, channel: instance.methodChannel!)
        instance.eventChannel?.setStreamHandler(instance)
    }
    
    public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "init":
            handleInit(call, result: result)
        case "nodeStart":
            handleNodeStart(result: result)
        case "nodeStop":
            handleNodeStop(result: result)
        case "nodeGetHeight":
            handleNodeGetHeight(result: result)
        case "walletCreateNew":
            handleWalletCreateNew(call, result: result)
        case "walletImport":
            handleWalletImport(call, result: result)
        case "walletGetAddress":
            handleWalletGetAddress(result: result)
        case "walletGetBalance":
            handleWalletGetBalance(result: result)
        case "walletSend":
            handleWalletSend(call, result: result)
        case "cdCreate":
            handleCdCreate(call, result: result)
        case "cdWithdraw":
            handleCdWithdraw(call, result: result)
        case "swapInitiate":
            handleSwapInitiate(call, result: result)
        case "swapClaim":
            handleSwapClaim(call, result: result)
        default:
            result(FlutterMethodNotImplemented)
        }
    }
    
    private func handleInit(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: Any],
              let dataDir = args["dataDir"] as? String else {
            result(FlutterError(code: "INVALID_ARGS", message: "Missing required arguments", details: nil))
            return
        }
        
        let lightMode = args["lightMode"] as? Bool ?? true
        let pruningDepth = args["pruningDepth"] as? Int ?? 10000
        let testnet = args["testnet"] as? Bool ?? false
        let maxPeers = args["maxPeers"] as? Int ?? 50
        let backgroundMode = args["backgroundMode"] as? Bool ?? true
        
        let config = FuegoConfig(
            dataDir: dataDir,
            lightMode: lightMode,
            pruningDepth: pruningDepth,
            testnet: testnet,
            maxPeers: maxPeers,
            backgroundMode: backgroundMode
        )
        
        do {
            nodeManager = try FuegoNodeManager(config: config)
            walletManager = FuegoWalletManager(nodeManager: nodeManager!)
            result(true)
        } catch {
            result(FlutterError(code: "INIT_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleNodeStart(result: @escaping FlutterResult) {
        do {
            try nodeManager?.start()
            registerBackgroundTasks()
            result(true)
        } catch {
            result(FlutterError(code: "NODE_START_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleNodeStop(result: @escaping FlutterResult) {
        nodeManager?.stop()
        result(true)
    }
    
    private func handleNodeGetHeight(result: @escaping FlutterResult) {
        let height = nodeManager?.getHeight() ?? 0
        result(height)
    }
    
    private func handleWalletCreateNew(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        let args = call.arguments as? [String: Any]
        let password = args?["password"] as? String
        
        do {
            let seed = try walletManager?.createNew(password: password)
            result(seed)
        } catch {
            result(FlutterError(code: "WALLET_CREATE_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleWalletImport(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: Any],
              let seed = args["seed"] as? String else {
            result(FlutterError(code: "INVALID_ARGS", message: "Missing seed", details: nil))
            return
        }
        
        let password = args["password"] as? String
        
        do {
            try walletManager?.import(seed: seed, password: password)
            result(true)
        } catch {
            result(FlutterError(code: "WALLET_IMPORT_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleWalletGetAddress(result: @escaping FlutterResult) {
        do {
            let address = try walletManager?.getAddress()
            result(address)
        } catch {
            result(FlutterError(code: "WALLET_ADDRESS_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleWalletGetBalance(result: @escaping FlutterResult) {
        do {
            let balance = try walletManager?.getBalance()
            result([
                "total": balance?.total ?? 0,
                "available": balance?.available ?? 0,
                "locked": balance?.locked ?? 0,
                "cdLocked": balance?.cdLocked ?? 0
            ])
        } catch {
            result(FlutterError(code: "WALLET_BALANCE_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleWalletSend(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: Any],
              let address = args["address"] as? String,
              let amount = args["amount"] as? UInt64 else {
            result(FlutterError(code: "INVALID_ARGS", message: "Missing required arguments", details: nil))
            return
        }
        
        let fee = args["fee"] as? UInt64
        let paymentId = args["paymentId"] as? String
        let mixin = args["mixin"] as? Int
        
        do {
            let txHash = try walletManager?.send(
                address: address,
                amount: amount,
                fee: fee,
                paymentId: paymentId,
                mixin: mixin
            )
            result(txHash)
        } catch {
            result(FlutterError(code: "WALLET_SEND_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleCdCreate(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: Any],
              let amount = args["amount"] as? UInt64,
              let term = args["term"] as? UInt32 else {
            result(FlutterError(code: "INVALID_ARGS", message: "Missing required arguments", details: nil))
            return
        }
        
        let fee = args["fee"] as? UInt64
        
        do {
            let commitmentId = try walletManager?.createCd(amount: amount, term: term, fee: fee)
            result(commitmentId)
        } catch {
            result(FlutterError(code: "CD_CREATE_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleCdWithdraw(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: Any],
              let commitmentId = args["commitmentId"] as? UInt64 else {
            result(FlutterError(code: "INVALID_ARGS", message: "Missing commitmentId", details: nil))
            return
        }
        
        let fee = args["fee"] as? UInt64
        
        do {
            let txHash = try walletManager?.withdrawCd(commitmentId: commitmentId, fee: fee)
            result(txHash)
        } catch {
            result(FlutterError(code: "CD_WITHDRAW_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleSwapInitiate(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: Any],
              let chain = args["chain"] as? Int32,
              let direction = args["direction"] as? Int32,
              let xfgAmount = args["xfgAmount"] as? UInt64,
              let minOtherAmount = args["minOtherAmount"] as? UInt64 else {
            result(FlutterError(code: "INVALID_ARGS", message: "Missing required arguments", details: nil))
            return
        }
        
        do {
            let swapId = try walletManager?.initiateSwap(
                chain: chain,
                direction: direction,
                xfgAmount: xfgAmount,
                minOtherAmount: minOtherAmount
            )
            result(swapId)
        } catch {
            result(FlutterError(code: "SWAP_INITIATE_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    private func handleSwapClaim(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: Any],
              let swapId = args["swapId"] as? String,
              let secret = args["secret"] as? String else {
            result(FlutterError(code: "INVALID_ARGS", message: "Missing required arguments", details: nil))
            return
        }
        
        do {
            let txHash = try walletManager?.claimSwap(swapId: swapId, secret: secret)
            result(txHash)
        } catch {
            result(FlutterError(code: "SWAP_CLAIM_ERROR", message: error.localizedDescription, details: nil))
        }
    }
    
    // MARK: - FlutterStreamHandler
    
    public func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        eventSink = events
        
        nodeManager?.setSyncProgressCallback { [weak self] progress in
            DispatchQueue.main.async {
                events([
                    "type": "syncProgress",
                    "currentHeight": progress.currentHeight,
                    "targetHeight": progress.targetHeight,
                    "percentage": progress.percentage,
                    "peersConnected": progress.peersConnected
                ])
            }
        }
        
        walletManager?.setBalanceCallback { [weak self] balance in
            DispatchQueue.main.async {
                events([
                    "type": "balance",
                    "total": balance.total,
                    "available": balance.available,
                    "locked": balance.locked,
                    "cdLocked": balance.cdLocked
                ])
            }
        }
        
        walletManager?.setTransactionCallback { [weak self] tx in
            DispatchQueue.main.async {
                events([
                    "type": "transaction",
                    "hash": tx.hash,
                    "amount": tx.amount,
                    "fee": tx.fee,
                    "blockHeight": tx.blockHeight,
                    "timestamp": tx.timestamp,
                    "incoming": tx.incoming,
                    "address": tx.address
                ])
            }
        }
        
        return nil
    }
    
    public func onCancel(withArguments arguments: Any?) -> FlutterError? {
        eventSink = nil
        return nil
    }
    
    // MARK: - Background Tasks
    
    private func registerBackgroundTasks() {
        if #available(iOS 13.0, *) {
            BGTaskScheduler.shared.register(
                forTaskWithIdentifier: "org.fuego.sdk.sync",
                using: nil
            ) { [weak self] task in
                self?.handleBackgroundSync(task: task as! BGAppRefreshTask)
            }
        }
    }
    
    private func handleBackgroundSync(task: BGAppRefreshTask) {
        // Schedule next background sync
        scheduleBackgroundSync()
        
        // Perform sync
        let operation = BlockOperation { [weak self] in
            self?.nodeManager?.sync()
        }
        
        task.expirationHandler = {
            operation.cancel()
        }
        
        operation.completionBlock = {
            task.setTaskCompleted(success: !operation.isCancelled)
        }
        
        DispatchQueue.global().addOperation(operation)
    }
    
    private func scheduleBackgroundSync() {
        if #available(iOS 13.0, *) {
            let request = BGAppRefreshTaskRequest(identifier: "org.fuego.sdk.sync")
            request.earliestBeginDate = Date(timeIntervalSinceNow: 5 * 60)  // 5 minutes
            
            do {
                try BGTaskScheduler.shared.submit(request)
            } catch {
                print("Could not schedule background sync: \(error)")
            }
        }
    }
}
```

---

## Phase 5: Testing & QA (Weeks 8-10)

### 5.1 Test Matrix

| Test Category | Platforms | Priority | Automation |
|---------------|-----------|----------|------------|
| Unit Tests | All | P0 | 100% |
| Integration Tests | All | P0 | 80% |
| E2E Tests | All | P1 | 60% |
| Performance Tests | All | P1 | 50% |
| Security Tests | All | P0 | 70% |
| UX Tests | All | P2 | Manual |

### 5.2 Performance Targets

| Metric | Target | Critical Threshold |
|--------|--------|-------------------|
| Cold Start | <5s | <10s |
| Sync (pruned) | <30min | <60min |
| Memory Usage | <200MB | <500MB |
| Battery (background) | <2%/hour | <5%/hour |
| Storage (pruned) | 2-5GB | <10GB |
| Transaction Send | <3s | <10s |
| Swap Initiate | <5s | <15s |

### 5.3 Security Checklist

- [ ] Seed phrase stored in secure enclave (iOS) / keystore (Android)
- [ ] Memory zeroization for sensitive data
- [ ] Biometric authentication support
- [ ] Certificate pinning for P2P connections
- [ ] Rate limiting for RPC calls
- [ ] Input validation on all public APIs
- [ ] No sensitive data in logs
- [ ] Secure deletion of temporary files

---

## Phase 6: Release & Documentation (Week 10)

### 6.1 Release Checklist

- [ ] All tests passing
- [ ] Performance targets met
- [ ] Security audit completed
- [ ] Documentation complete
- [ ] Example apps working
- [ ] pub.dev package published
- [ ] CocoaPods podspec published
- [ ] Maven Central artifact published
- [ ] GitHub release created
- [ ] Announcement blog post

### 6.2 Documentation Structure

```
docs/
├── getting-started/
│   ├── installation.md
│   ├── quickstart.md
│   └── migration-guide.md
├── guides/
│   ├── wallet-management.md
│   ├── cd-staking.md
│   ├── atomic-swaps.md
│   ├── background-sync.md
│   └── security-best-practices.md
├── api-reference/
│   ├── fuego_sdk.md
│   ├── fuego_node.md
│   ├── fuego_wallet.md
│   ├── fuego_cd.md
│   └── fuego_swap.md
├── widgets/
│   ├── balance-display.md
│   ├── transaction-list.md
│   ├── cd-staking-panel.md
│   └── swap-panel.md
└── troubleshooting/
    ├── common-issues.md
    ├── faq.md
    └── support.md
```

---

## Appendix A: Build Commands

### Android Build

```bash
# Setup
export ANDROID_NDK=/path/to/android-ndk
export ANDROID_SDK=/path/to/android-sdk

# Build mobile library
cd fuego_WS
mkdir -p build/android && cd build/android
cmake ../.. \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DMOBILE_BUILD=ON \
  -DLIGHT_NODE=ON \
  -DENABLE_PRUNING=ON
make -j8

# Build Flutter Android
cd fuego_WS/fuego_sdk/android
./gradlew build
./gradlew publishToMavenLocal
```

### iOS Build

```bash
# Build mobile library
cd fuego_WS
mkdir -p build/ios && cd build/ios
cmake ../.. \
  -DCMAKE_TOOLCHAIN_FILE=../../cmake/ios.toolchain.cmake \
  -DPLATFORM=OS64 \
  -DMOBILE_BUILD=ON \
  -DLIGHT_NODE=ON \
  -DENABLE_PRUNING=ON
make -j8

# Build Flutter iOS
cd fuego_WS/fuego_sdk/ios
pod lib lint
pod trunk push fuego_sdk.podspec
```

### Flutter Package

```bash
# Publish to pub.dev
cd fuego_WS/fuego_sdk
flutter pub get
flutter test
flutter analyze
dart pub publish --dry-run
dart pub publish
```

---

## Appendix B: Dependencies

### Android Dependencies

```gradle
// build.gradle
dependencies {
    implementation 'androidx.core:core-ktx:1.12.0'
    implementation 'androidx.lifecycle:lifecycle-runtime-ktx:2.7.0'
    implementation 'androidx.lifecycle:lifecycle-service:2.7.0'
    implementation 'org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3'
    
    // Fuego SDK
    implementation 'org.fuego:fuego-sdk:0.2.0'
}
```

### iOS Dependencies

```ruby
# Podfile
platform :ios, '15.0'

target 'Runner' do
  use_frameworks!
  
  pod 'FuegoSDK', '~> 0.2.0'
end
```

### Flutter Dependencies

```yaml
# pubspec.yaml
dependencies:
  flutter:
    sdk: flutter
  fuego_sdk: ^0.2.0
  
dev_dependencies:
  flutter_test:
    sdk: flutter
  flutter_lints: ^3.0.0
  mockito: ^5.4.0
  integration_test:
    sdk: flutter
```

---

## Appendix C: Example App

### Basic Wallet App

```dart
import 'package:fuego_sdk/fuego_sdk.dart';
import 'package:flutter/material.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await FuegoSDK.instance.init();
  runApp(const FuegoWalletApp());
}

class FuegoWalletApp extends StatefulWidget {
  const FuegoWalletApp({super.key});
  
  @override
  State<FuegoWalletApp> createState() => _FuegoWalletAppState();
}

class _FuegoWalletAppState extends State<FuegoWalletApp> {
  FuegoWallet? _wallet;
  FuegoBalance? _balance;
  
  @override
  void initState() {
    super.initState();
    _loadWallet();
  }
  
  Future<void> _loadWallet() async {
    // Try to load existing wallet or create new
    try {
      _wallet = await FuegoSDK.instance.getWallet();
      _updateBalance();
    } catch (e) {
      // Create new wallet
      final seed = await FuegoSDK.instance.getWallet().then((w) => w.create());
      // TODO: Show seed to user securely!
      _wallet = await FuegoSDK.instance.getWallet();
      _updateBalance();
    }
  }
  
  Future<void> _updateBalance() async {
    final balance = await _wallet!.getBalance(refresh: true);
    setState(() => _balance = balance);
    
    // Listen for balance updates
    _wallet!.balanceStream.listen((b) {
      setState(() => _balance = b);
    });
  }
  
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('Fuego Wallet')),
        body: _wallet == null
            ? const Center(child: CircularProgressIndicator())
            : Column(
                children: [
                  // Balance Display
                  FuegoBalanceDisplay(balance: _balance!),
                  
                  // Send Button
                  ElevatedButton(
                    onPressed: () => _showSendDialog(),
                    child: const Text('Send XFG'),
                  ),
                  
                  // CD Staking Button
                  ElevatedButton(
                    onPressed: () => _navigateToCDScreen(),
                    child: const Text('Stake (CD)'),
                  ),
                  
                  // Atomic Swap Button
                  ElevatedButton(
                    onPressed: () => _navigateToSwapScreen(),
                    child: const Text('Atomic Swap'),
                  ),
                ],
              ),
      ),
    );
  }
  
  void _showSendDialog() {
    // TODO: Implement send dialog
  }
  
  void _navigateToCDScreen() {
    // TODO: Navigate to CD staking screen
  }
  
  void _navigateToSwapScreen() {
    // TODO: Navigate to atomic swap screen
  }
}
```

---

**Document Version**: 1.0  
**Last Updated**: 2026-04-26  
**Maintained By**: Fuego Core Team
