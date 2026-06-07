add_definitions(-DSTATICLIB)
add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)
include_directories(${CMAKE_SOURCE_DIR}/external/parallel_hashmap)
include_directories(${CMAKE_SOURCE_DIR}/external/httplib)

# Add JSON include directories globally for all targets
if(JSONCPP_INCLUDE_DIRS)
  include_directories(SYSTEM ${JSONCPP_INCLUDE_DIRS})
endif()

file(GLOB_RECURSE BlockchainExplorer BlockchainExplorer/*)
file(GLOB_RECURSE Common Common/*)
file(GLOB_RECURSE Crypto crypto/*)
add_library(Crypto STATIC ${Crypto})
file(GLOB_RECURSE CryptoNoteCore CryptoNoteCore/* CryptoNoteConfig.h DynamicRingSize.cpp OSPEADDecoySelection.cpp)
file(GLOB_RECURSE CryptoNoteProtocol CryptoNoteProtocol/*)
file(GLOB_RECURSE Daemon Daemon/*)
list(APPEND Daemon SwapDaemon/SwapHashLock.cpp)
file(GLOB_RECURSE Http HTTP/*)
file(GLOB_RECURSE InProcessNode InProcessNode/*)
file(GLOB_RECURSE Logging Logging/*)
# Optimizer sources
file(GLOB_RECURSE NodeRpcProxy NodeRpcProxy/*)
file(GLOB_RECURSE P2p P2p/*)
file(GLOB_RECURSE Net net/*)
file(GLOB_RECURSE RPC_SOURCES Rpc/*)
file(GLOB_RECURSE Serialization Serialization/*)
# Source files defined directly in add_executable calls

file(GLOB_RECURSE Transfers Transfers/*)
file(GLOB_RECURSE Wallet Wallet/*)
file(GLOB_RECURSE WalletLegacy WalletLegacy/*)
file(GLOB_RECURSE JsonRpcServer JsonRpcServer/*)
file(GLOB_RECURSE PaymentGate PaymentGate/*)
file(GLOB_RECURSE PaymentGateService PaymentGateService/*)
file(GLOB_RECURSE SimpleWallet SimpleWallet/*)

# Remove duplicate glob definition and explicitly define library sources
list(REMOVE_ITEM SimpleWallet SimpleWallet/main.cpp)

# Adaptor swap protocol helpers needed by wallet commands
list(APPEND SimpleWallet
    SwapDaemon/AdaptorSwap.cpp
    SwapDaemon/SwapTypes.cpp
)

# Optimizer sources - explicit path
set(OPTIMIZER_SOURCES
    OptimizerSrc/Optimizer.cpp
)

# Tor integration - only if enabled via top-level option
if(ENABLE_FUEGOTOR)
  add_subdirectory(FuegoTor)
endif()

# I2P integration
add_subdirectory(FuegoI2P)

# Meshtastic integration for offline/off-grid fallback
#add_subdirectory(FuegoMeshtastic)

# Define ENABLE_FUEGOMESH globally for targets that need it
#add_compile_definitions(ENABLE_FUEGOMESH)

# Eldernode services
# add_subdirectory(EldernodeIndexManager)
# add_subdirectory(BurnDepositValidationService)

# System files
if(MSVC)
  file(GLOB_RECURSE System Platform/Windows/System/* System/*)
elseif(APPLE)
  file(GLOB_RECURSE System Platform/OSX/System/* System/*)
elseif(ANDROID)
  file(GLOB_RECURSE System Platform/Android/System/* System/* Platform/Posix/System/*)
elseif(FREEBSD)
  file(GLOB_RECURSE System Platform/FreeBSD/System/* System/* Platform/Posix/System/*)
else()
  file(GLOB_RECURSE System System/* Platform/Linux/System/* Platform/Posix/System/*)
endif()

# Explicitly include platform-specific files
if(MSVC)
  list(APPEND System Platform/Windows/System/Ipv4Resolver.cpp Platform/Windows/System/ErrorMessage.cpp Platform/Windows/System/Future.h)
elseif(APPLE)
  list(APPEND System Platform/OSX/System/Ipv4Resolver.cpp Platform/OSX/System/ErrorMessage.cpp Platform/OSX/System/Future.h)
elseif(ANDROID)
  list(APPEND System Platform/Android/System/Ipv4Resolver.cpp Platform/Android/System/ErrorMessage.cpp Platform/Android/System/Future.h)
elseif(FREEBSD)
  list(APPEND System Platform/FreeBSD/System/Ipv4Resolver.cpp Platform/FreeBSD/System/ErrorMessage.cpp Platform/FreeBSD/System/Future.h)
else()
  list(APPEND System Platform/Linux/System/Ipv4Resolver.cpp Platform/Linux/System/ErrorMessage.cpp Platform/Linux/System/Future.h)
endif()

  # Add Boost linking to existing System target

# Explicitly include ContextGroupTimeout files for all platforms
list(APPEND System
  System/ContextGroupTimeout.cpp
  System/ContextGroupTimeout.h
)

# Add platform-specific files
if(MSVC)
  list(APPEND System Platform/Windows/System/Ipv4Resolver.cpp Platform/Windows/System/ErrorMessage.cpp Platform/Windows/System/Future.h)
elseif(APPLE)
  list(APPEND System Platform/OSX/System/Ipv4Resolver.cpp Platform/OSX/System/ErrorMessage.cpp Platform/OSX/System/Future.h)
elseif(ANDROID)
  list(APPEND System Platform/Android/System/Ipv4Resolver.cpp Platform/Android/System/ErrorMessage.cpp Platform/Android/System/Future.h)
elseif(FREEBSD)
  list(APPEND System Platform/FreeBSD/System/Ipv4Resolver.cpp Platform/FreeBSD/System/ErrorMessage.cpp Platform/FreeBSD/System/Future.h)
else()
  list(APPEND System Platform/Linux/System/Ipv4Resolver.cpp Platform/Linux/System/ErrorMessage.cpp Platform/Linux/System/Future.h)
endif()

# Add ContextGroupTimeout.cpp directly to System library
set(SYSTEM_SOURCES ${System})

source_group("" FILES ${Common} ${Crypto} ${CryptoNoteCore} ${CryptoNoteProtocol} ${Daemon} ${JsonRpcServer} ${Http} ${Logging} ${NodeRpcProxy} ${P2p} ${Rpc} ${Serialization} ${SimpleWallet} ${SYSTEM_SOURCES} ${Transfers} ${Wallet} ${WalletLegacy})

# --------------------------
# Libraries
# --------------------------
add_library(BlockchainExplorer STATIC ${BlockchainExplorer})
target_include_directories(BlockchainExplorer SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
target_link_libraries(BlockchainExplorer PUBLIC ${JSONCPP_LIBRARIES} ${Boost_LIBRARIES})
add_library(Common STATIC ${Common})
# System target already defined at line 81 - removing duplicate definition

if(NOT TARGET System)
  add_library(System STATIC ${SYSTEM_SOURCES})

  # Fix ucontext compilation issues on macOS
  if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      # Define _XOPEN_SOURCE for ucontext functions on macOS
      target_compile_definitions(System PRIVATE _XOPEN_SOURCE=700)
      target_compile_definitions(System PRIVATE __MAC_OS_X_VERSION_MIN_REQUIRED=101200)
      message(STATUS "macOS detected, added ucontext compatibility definitions for System library")
  endif()
endif()


add_library(CryptoNoteCore STATIC ${CryptoNoteCore})
if(MSVC)
    target_compile_options(CryptoNoteCore PRIVATE /W0)
else()
    target_compile_options(CryptoNoteCore PRIVATE -Wno-deprecated-declarations)
endif()
target_link_libraries(CryptoNoteCore PUBLIC
    Crypto
    Common
    P2P
    Serialization
    Logging
    Transfers
    System
    BlockchainExplorer
)
target_include_directories(CryptoNoteCore PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
)
target_include_directories(CryptoNoteCore SYSTEM PUBLIC ${Boost_INCLUDE_DIRS})
add_dependencies(CryptoNoteCore version)

# --------------------------
# Apply AES/SSE compile options to Crypto library for slow-hash.c
# --------------------------
# Robust AES-NI configuration that works in CI environments

# Always try to enable AES-NI for GCC/Clang on non-Android x86_64 systems
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT ANDROID)
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
        # Apple Silicon: enable ARM crypto extensions for hardware AES.
        # Apple M-series CPUs have ARMv8 crypto extensions (AES, PMULL, SHA1, SHA2).
        # We MUST use hardware AES on ARM64, not the NO_AES software fallback —
        # the portable software path in slow-hash.c produces CryptoNight hashes
        # that diverge from the x86_64/ARM hardware paths used by network miners,
        # causing "too weak proof of work" rejections when syncing the mainnet chain.
        # FORCE_USE_HEAP: allocate cn_slow_hash scratchpad on heap instead of stack
        # to avoid stack overflow (2MB buffer vs 512KB default thread stack size).
        target_compile_options(Crypto PRIVATE -march=armv8-a+crypto)
        target_compile_definitions(Crypto PRIVATE FORCE_USE_HEAP)
        message(STATUS "Apple Silicon detected, using ARM hardware AES (armv8-a+crypto) with heap allocation for Crypto library")
    else()
        # Enable AES-NI and SSE instructions for cryptographic operations
        target_compile_options(Crypto PRIVATE -maes -msse2 -msse4.1 -msse4.2)

        # Add preprocessor definitions for AES-NI support
        target_compile_definitions(Crypto PRIVATE __AES__ __SSE2__ __SSE4_1__ __SSE4_2__ HAVE_AES_NI)

        # Special handling for CI environments
        if (DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS} OR DEFINED ENV{GITHUB_REF} OR DEFINED ENV{GITHUB_RUN_ID})
            # In CI, add march=native for better performance (works in most CI environments)
            target_compile_options(Crypto PRIVATE -march=native)
            message(STATUS "CI environment detected, AES-NI enabled with march=native for Crypto library")
        else()
            message(STATUS "AES-NI instructions enabled for Crypto library")
        endif()
    endif()
elseif (CMAKE_C_COMPILER_ID MATCHES "MSVC" AND NOT ANDROID)
    # MSVC compiler - enable AVX2 which includes AES-NI support
    target_compile_options(Crypto PRIVATE /arch:AVX2)
    target_compile_definitions(Crypto PRIVATE HAVE_AES_NI)
    message(STATUS "MSVC detected, AES-NI enabled via AVX2 for Crypto library")

else()
    # Fallback for unknown compilers or Android
    target_compile_definitions(Crypto PRIVATE NO_AES)
    if (ANDROID)
        message(STATUS "Android detected, using software AES for Crypto library")
    else()
        message(STATUS "Unknown compiler ${CMAKE_C_COMPILER_ID}, using software AES for Crypto library")
    endif()
endif()

# Platform-specific adjustments handled above
# No additional flags needed for Darwin/Linux

message(STATUS "=== End AES-NI Configuration ===")

add_library(Http STATIC ${Http})
add_library(InProcessNode STATIC ${InProcessNode})
add_library(Logging STATIC ${Logging})
target_include_directories(Logging SYSTEM PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(Logging PUBLIC ${Boost_LIBRARIES})
target_link_libraries(Logging PUBLIC ${Boost_LIBRARIES})
add_library(NodeRpcProxy STATIC ${NodeRpcProxy})

# Swap core library: shared by libRpc and the xfg-swapd binary
# Active sources: adaptor-signature atomic swaps (v1)
# Pool sources are deferred to v11 — see src/SwapDaemon/pool_v11/README.md
add_library(SwapDaemonLib STATIC
    SwapDaemon/SwapTypes.cpp
    SwapDaemon/SwapStateMachine.cpp
    SwapDaemon/SwapSecretEncryption.cpp
    SwapDaemon/SwapDatabase.cpp
    SwapDaemon/AdaptorSwap.cpp
    SwapDaemon/SwapDaemon.cpp
    SwapDaemon/SwapTimelock.cpp
    SwapDaemon/SwapTxBuilder.cpp
    SwapDaemon/SwapPeerProtocol.cpp
    SwapDaemon/SwapP2P.cpp
    SwapDaemon/FuegoRpcClient.cpp
    SwapDaemon/PriceOracle.cpp
    SwapDaemon/OfferManager.cpp
    SwapDaemon/StatusServer.cpp
    SwapDaemon/PoolTypes.cpp
    SwapDaemon/PoolAMM.cpp
    SwapDaemon/PoolAttestation.cpp
    SwapDaemon/PoolDatabase.cpp
    # v11: pool trading — sources moved to SwapDaemon/pool_v11/, not compiled in v1
    # SwapDaemon/pool_v11/PoolTypes.cpp
    # SwapDaemon/pool_v11/PoolAMM.cpp
    # SwapDaemon/pool_v11/PoolAttestation.cpp
    # SwapDaemon/pool_v11/PoolOrganizer.cpp
    # SwapDaemon/pool_v11/PoolP2P.cpp
    # SwapDaemon/pool_v11/PoolDatabase.cpp
    # SwapDaemon/pool_v11/PoolDatabase.cpp
    SwapDaemon/PoolOrganizer.cpp
    SwapDaemon/ChainRegistry.cpp
    SwapDaemon/Monero/MoneroRpcClient.cpp
    SwapDaemon/Monero/AdaptorSignature.cpp
    SwapDaemon/Monero/MoneroAddress.cpp
    SwapDaemon/Monero/XmrChainClient.cpp
    SwapDaemon/Ethereum/EthRpcClient.cpp
    SwapDaemon/Ethereum/ContractAbi.cpp
    SwapDaemon/Ethereum/EthChainClient.cpp
    SwapDaemon/BitcoinCash/BchRpcClient.cpp
    SwapDaemon/BitcoinCash/HtlcScript.cpp
    SwapDaemon/BitcoinCash/BchChainClient.cpp
    SwapDaemon/Solana/SolRpcClient.cpp
    SwapDaemon/Solana/SolChainClient.cpp
    SwapDaemon/Crypto/Secp256k1Signer.cpp
    SwapDaemon/Crypto/RlpEncoder.cpp
    SwapDaemon/Crypto/Bip143Sighash.cpp
    SwapDaemon/Crypto/Base58Std.cpp
    SwapDaemon/Crypto/Ed25519Verify.cpp
    SwapDaemon/ChainClientConfig.cpp
    SwapDaemon/SwapHashLock.cpp
)
target_include_directories(SwapDaemonLib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/SwapDaemon
)
target_link_libraries(SwapDaemonLib PUBLIC Common Crypto Serialization CryptoNoteCore OpenSSL::SSL OpenSSL::Crypto secp256k1)
target_include_directories(SwapDaemonLib SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS})

# SwapDaemon unit test: adaptor_aggregate ↔ extract_secret roundtrip
add_executable(test_adaptor_roundtrip
    SwapDaemon/tests/test_adaptor_roundtrip.cpp)
target_link_libraries(test_adaptor_roundtrip PRIVATE
    SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)

# SwapDaemon unit test: PriceOracle ARB/BASE pricing and units
add_executable(test_price_oracle_arb
    SwapDaemon/tests/test_price_oracle_arb.cpp)
target_link_libraries(test_price_oracle_arb PRIVATE
    SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)

# SwapDaemon unit test: HTLC hashlock derivation (keccak256/sha256 of secret)
add_executable(test_swap_hashlock
    SwapDaemon/tests/test_swap_hashlock.cpp)
target_link_libraries(test_swap_hashlock PRIVATE
    SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)

# SwapDaemon unit test: Solana HTLC PDA account reference
add_executable(test_sol_htlc_address
    SwapDaemon/tests/test_sol_htlc_address.cpp)
target_link_libraries(test_sol_htlc_address PRIVATE
    SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)

# SwapDaemon MANUAL on-chain e2e (not run in CI; needs a live validator)
add_executable(test_sol_e2e
    SwapDaemon/tests/test_sol_e2e.cpp)
target_link_libraries(test_sol_e2e PRIVATE
    SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)

# SwapDaemon unit test: Monero address encoding + shared spend pub (A+B)
add_executable(test_monero_address
    SwapDaemon/tests/test_monero_address.cpp)
target_link_libraries(test_monero_address PRIVATE
    SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)

# SwapDaemon unit test: XMR shared-key ops (A+B, 2-term full key)
add_executable(test_xmr_keys
    SwapDaemon/tests/test_xmr_keys.cpp)
target_link_libraries(test_xmr_keys PRIVATE
    SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)

# XMR claim/refund adaptor tests (spend-key combine behaviour)
# Note: Path is relative from src/ directory
add_executable(XmrClaimAdaptorTests
    ../tests/SwapDaemon/XmrClaimAdaptorTests.cpp)
target_link_libraries(XmrClaimAdaptorTests PRIVATE
    SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)

add_library(Rpc STATIC ${RPC_SOURCES})
target_include_directories(Rpc PRIVATE ${CMAKE_SOURCE_DIR}/external/httplib)
target_link_libraries(Rpc PUBLIC SwapDaemonLib)
add_library(P2P STATIC ${CryptoNoteProtocol} ${P2p} ${Net})
# Check if target exists to prevent duplicate definition
if(NOT TARGET CryptoNoteCore)
  add_library(CryptoNoteCore STATIC ${CryptoNoteCore})
endif()

# Link jsoncpp to CryptoNoteCore that includes JSON headers
target_include_directories(CryptoNoteCore SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(CryptoNoteCore PUBLIC ${JSONCPP_LIBRARIES})

if(TARGET upnpc-static)
    add_dependencies(P2P upnpc-static)
    target_link_libraries(P2P PUBLIC upnpc-static)
    target_include_directories(P2P PUBLIC
        ${CMAKE_SOURCE_DIR}/external
        ${CMAKE_BINARY_DIR}/external/miniupnpc
    )
else()
    message(FATAL_ERROR "upnpc-static target not found. MiniUPnPc static library is required for UPnP support.")
endif()
add_library(Serialization STATIC ${Serialization})
target_link_libraries(Serialization PUBLIC Common)
# Link jsoncpp for any potential JSON serialization
target_link_libraries(Serialization PUBLIC ${JSONCPP_LIBRARIES})

# Fix ucontext compilation issues on macOS
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Define _XOPEN_SOURCE for ucontext functions on macOS
    target_compile_definitions(System PRIVATE _XOPEN_SOURCE=700)
    target_compile_definitions(System PRIVATE __MAC_OS_X_VERSION_MIN_REQUIRED=101200)
    message(STATUS "macOS detected, added ucontext compatibility definitions for System library")
endif()
add_library(Transfers STATIC ${Transfers})
add_library(Wallet STATIC ${Wallet} ${WalletLegacy})
add_library(PaymentGate STATIC ${PaymentGate})
add_library(JsonRpcServer STATIC ${JsonRpcServer})
# Link jsoncpp for JSON-RPC server functionality
target_include_directories(JsonRpcServer SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(JsonRpcServer PUBLIC ${JSONCPP_LIBRARIES})
add_library(SimpleWalletLib STATIC ${SimpleWallet})
target_link_libraries(SimpleWalletLib PUBLIC
    Wallet
    NodeRpcProxy
    Transfers
    Rpc
    Http
    CryptoNoteCore
    System
    Logging
    Common
    Crypto
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

add_executable(Daemon ${Daemon})
add_executable(TestnetDaemon Daemon/DaemonCommandsHandler.cpp Daemon/DaemonCommandsHandler.h TestnetDaemon/TestnetDaemon.cpp)
# SimpleWallet executable uses library + adds back main.cpp
add_executable(SimpleWallet ${SimpleWallet} SimpleWallet/main.cpp)
add_executable(PaymentGateService ${PaymentGateService})
add_executable(Optimizer ${OPTIMIZER_SOURCES})

# Swap Daemon executable (xfg-swapd) — active v1 atomic swap sources only
# v11 pool trading sources are in SwapDaemon/pool_v11/ and excluded here.
# See src/SwapDaemon/pool_v11/README.md for reintegration instructions.
add_executable(SwapDaemon
    SwapDaemon/main.cpp
    SwapDaemon/AdaptorSwap.cpp
    SwapDaemon/FuegoRpcClient.cpp
    SwapDaemon/PriceOracle.cpp
    SwapDaemon/OfferManager.cpp
    SwapDaemon/SwapDaemon.cpp
    SwapDaemon/SwapP2P.cpp
    SwapDaemon/SwapSecretEncryption.cpp
    SwapDaemon/ChainRegistry.cpp
    SwapDaemon/Monero/MoneroRpcClient.cpp
    SwapDaemon/Monero/AdaptorSignature.cpp
    SwapDaemon/Monero/MoneroAddress.cpp
    SwapDaemon/Monero/XmrChainClient.cpp
    SwapDaemon/Ethereum/EthRpcClient.cpp
    SwapDaemon/Ethereum/ContractAbi.cpp
    SwapDaemon/Ethereum/EthChainClient.cpp
    SwapDaemon/BitcoinCash/BchRpcClient.cpp
    SwapDaemon/BitcoinCash/HtlcScript.cpp
    SwapDaemon/BitcoinCash/BchChainClient.cpp
    SwapDaemon/Solana/SolRpcClient.cpp
    SwapDaemon/Solana/SolChainClient.cpp
    SwapDaemon/Crypto/Secp256k1Signer.cpp
    SwapDaemon/Crypto/RlpEncoder.cpp
    SwapDaemon/Crypto/Bip143Sighash.cpp
    SwapDaemon/Crypto/Base58Std.cpp
    SwapDaemon/Crypto/Ed25519Verify.cpp
    SwapDaemon/ChainClientConfig.cpp
    SwapDaemon/SwapHashLock.cpp
    # v11: pool trading — sources moved to SwapDaemon/pool_v11/, not compiled in v1
    # SwapDaemon/pool_v11/PoolTypes.cpp
    # SwapDaemon/pool_v11/PoolAMM.cpp
    # SwapDaemon/pool_v11/PoolAttestation.cpp
    # SwapDaemon/pool_v11/PoolOrganizer.cpp
    # SwapDaemon/pool_v11/PoolP2P.cpp
    # SwapDaemon/pool_v11/PoolDatabase.cpp
)

# Add TestnetWallet executable (extends simple_wallet with testnet-specific commands)
add_executable(testnet-wallet-cli
    TestnetWallet/main.cpp
    TestnetWallet/TestnetWallet.cpp
    SimpleWallet/PasswordContainer.cpp
    SimpleWallet/ClientHelper.cpp
    SimpleWallet/TransferCmd.cpp
)

# Set proper linker flags to find FuegoTor library (only if enabled)
if(ENABLE_FUEGOTOR)
  # Add link directory to ensure FuegoTor and FuegoI2P can be found for all relevant targets
  set(FUEGOTOR_LIB_PATH ${CMAKE_CURRENT_BINARY_DIR}/src/FuegoTor)
  set(FUEGOI2P_LIB_PATH ${CMAKE_CURRENT_BINARY_DIR}/src/FuegoI2P)
  target_link_directories(PaymentGateService PRIVATE ${FUEGOTOR_LIB_PATH} ${FUEGOI2P_LIB_PATH})
  target_link_directories(SimpleWallet PRIVATE ${FUEGOTOR_LIB_PATH} ${FUEGOI2P_LIB_PATH})
  target_link_directories(Daemon PRIVATE ${FUEGOTOR_LIB_PATH} ${FUEGOI2P_LIB_PATH})
  target_link_directories(TestnetDaemon PRIVATE ${FUEGOTOR_LIB_PATH} ${FUEGOI2P_LIB_PATH})
  target_link_directories(testnet-wallet-cli PRIVATE ${FUEGOTOR_LIB_PATH} ${FUEGOI2P_LIB_PATH})
  target_link_directories(SwapDaemon PRIVATE ${FUEGOTOR_LIB_PATH} ${FUEGOI2P_LIB_PATH})
endif()

target_link_libraries(testnet-wallet-cli PRIVATE
    Wallet
    NodeRpcProxy
    Transfers
    Rpc
    Http
    CryptoNoteCore
    Crypto
    P2P
    System
    Logging
    Common
    upnpc-static
    BlockchainExplorer
    SimpleWalletLib
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

set_target_properties(testnet-wallet-cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src")

if (MSVC)
  target_link_libraries(System PRIVATE ws2_32)
endif ()

target_link_libraries(Daemon PRIVATE
    CryptoNoteCore
    P2P
    Rpc
    CryptoNoteCore
    Crypto
    Common
    Logging
    System
    Http
    InProcessNode
    upnpc-static
    BlockchainExplorer
    FuegoI2P
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

target_link_libraries(TestnetDaemon PRIVATE
    CryptoNoteCore
    P2P
    Rpc
    CryptoNoteCore
    Crypto
    Common
    Logging
    System
    Http
    upnpc-static
    BlockchainExplorer
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

target_link_libraries(SimpleWallet PRIVATE
    Wallet
    NodeRpcProxy
    Transfers
    Rpc
    Http
    CryptoNoteCore
    System
    Logging
    Common
    Crypto
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

target_link_libraries(PaymentGateService PRIVATE
    PaymentGate
    JsonRpcServer
    Wallet
    NodeRpcProxy
    Transfers
    CryptoNoteCore
    Crypto
    P2P
    Rpc
    CryptoNoteCore
    Common
    Logging
    Http
    System
    InProcessNode
    upnpc-static
    BlockchainExplorer
    ${Boost_LIBRARIES}
    Serialization
  )

target_link_libraries(Optimizer PRIVATE
    PaymentGate
    Rpc
    Http
    CryptoNoteCore
    Logging
    Common
    Crypto
    System
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

find_package(OpenSSL REQUIRED)
target_link_libraries(SwapDaemon PRIVATE
    SwapDaemonLib
    Crypto Common Logging Crypto Common
    ${Boost_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    secp256k1
    ${JSONCPP_LIBRARIES}
)
target_include_directories(SwapDaemon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(SimpleWallet PRIVATE
    SimpleWalletLib
)

# Suppress deprecation warnings for Rpc target
if(APPLE)
  target_compile_options(Rpc PRIVATE -Wno-deprecated-declarations -Wno-deprecated -Wno-deprecated-builtins -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-deprecated-copy -Wno-deprecated-copy-dtor -Wno-system-headers -w)
  target_include_directories(Rpc SYSTEM PRIVATE /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1)
  target_include_directories(Rpc SYSTEM PRIVATE /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1)
  target_include_directories(Rpc SYSTEM PRIVATE /opt/homebrew/include)
  target_include_directories(Rpc SYSTEM PRIVATE /usr/local/include)
  target_include_directories(Rpc SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/parallel_hashmap)

  # Ensure proper C++ standard library linking
  target_link_libraries(Rpc PRIVATE -stdlib=libc++)
endif()

# Ensure all targets use proper C++ standard library on macOS
if(APPLE)
  target_link_libraries(BlockchainExplorer PRIVATE -stdlib=libc++)
  target_link_libraries(Common PRIVATE -stdlib=libc++)
  target_link_libraries(Crypto PRIVATE -stdlib=libc++)
  target_link_libraries(CryptoNoteCore PRIVATE -stdlib=libc++)
  target_link_libraries(Http PRIVATE -stdlib=libc++)
  target_link_libraries(InProcessNode PRIVATE -stdlib=libc++)
  target_link_libraries(Logging PRIVATE -stdlib=libc++)
  target_link_libraries(NodeRpcProxy PRIVATE -stdlib=libc++)
  target_link_libraries(Rpc PRIVATE -stdlib=libc++)
  target_link_libraries(SwapDaemonLib PRIVATE -stdlib=libc++)
  target_link_libraries(P2P PRIVATE -stdlib=libc++)
  target_link_libraries(Serialization PRIVATE -stdlib=libc++)
  target_link_libraries(System PRIVATE -stdlib=libc++)
  target_link_libraries(Transfers PRIVATE -stdlib=libc++)
  target_link_libraries(Wallet PRIVATE -stdlib=libc++)
  target_link_libraries(PaymentGate PRIVATE -stdlib=libc++)
  target_link_libraries(JsonRpcServer PRIVATE -stdlib=libc++)
  target_link_libraries(SimpleWalletLib PRIVATE -stdlib=libc++)
  target_link_libraries(SwapDaemon PRIVATE -stdlib=libc++)
endif()

# --------------------------
# ICU linking is handled in root CMakeLists.txt
# --------------------------

# --------------------------
# Additional Linux linking
# --------------------------
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux" OR APPLE AND NOT ANDROID)
  target_link_libraries(SimpleWallet PRIVATE -lresolv)
  target_link_libraries(testnet-wallet-cli PRIVATE -lresolv)
  target_link_libraries(Daemon PRIVATE -lresolv)
  target_link_libraries(TestnetDaemon PRIVATE -lresolv)
  target_link_libraries(PaymentGateService PRIVATE -lresolv)
  target_link_libraries(Optimizer PRIVATE -lresolv)
  target_link_libraries(SwapDaemon PRIVATE -lresolv)
endif ()

# --------------------------
# Dependencies
# --------------------------
add_dependencies(Rpc version)
add_dependencies(Daemon version)
add_dependencies(TestnetDaemon version)
add_dependencies(SimpleWallet version)
add_dependencies(testnet-wallet-cli version)
add_dependencies(PaymentGateService version)
add_dependencies(Optimizer version)
add_dependencies(P2P version)

# --------------------------
# Output names
# --------------------------
set_property(TARGET SimpleWallet PROPERTY OUTPUT_NAME "fire_wallet")
set_property(TARGET testnet-wallet-cli PROPERTY OUTPUT_NAME "test_wallet")
set_property(TARGET PaymentGateService PROPERTY OUTPUT_NAME "walletd")
set_property(TARGET Daemon PROPERTY OUTPUT_NAME "fuegod")
set_property(TARGET TestnetDaemon PROPERTY OUTPUT_NAME "testnetd")
set_property(TARGET Optimizer PROPERTY OUTPUT_NAME "optimizer")
set_property(TARGET SwapDaemon PROPERTY OUTPUT_NAME "xfg-swapd")
