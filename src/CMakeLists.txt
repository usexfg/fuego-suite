add_definitions(-DSTATICLIB)
include_directories(${CMAKE_SOURCE_DIR}/external/parallel_hashmap)

# Add JSON include directories globally for all targets
if(JSONCPP_INCLUDE_DIRS)
  include_directories(SYSTEM ${JSONCPP_INCLUDE_DIRS})
endif()

file(GLOB_RECURSE BlockchainExplorer BlockchainExplorer/*)
file(GLOB_RECURSE Common Common/*)
file(GLOB_RECURSE Crypto crypto/*)
file(GLOB_RECURSE CryptoNoteCore CryptoNoteCore/* CryptoNoteConfig.h DynamicRingSize.cpp OSPEADDecoySelection.cpp)
file(GLOB_RECURSE CryptoNoteProtocol CryptoNoteProtocol/*)
file(GLOB_RECURSE Daemon Daemon/*)
file(GLOB_RECURSE Http HTTP/*)
file(GLOB_RECURSE InProcessNode InProcessNode/*)
file(GLOB_RECURSE Logging Logging/*)
# Optimizer sources
file(GLOB_RECURSE NodeRpcProxy NodeRpcProxy/*)
file(GLOB_RECURSE P2p P2p/*)
file(GLOB_RECURSE Rpc Rpc/*)
file(GLOB_RECURSE Serialization Serialization/*)
# Source files defined directly in add_executable calls

file(GLOB_RECURSE Transfers Transfers/*)
file(GLOB_RECURSE Wallet Wallet/* BurnTransactionHandler.cpp)
file(GLOB_RECURSE WalletLegacy WalletLegacy/*)
file(GLOB_RECURSE JsonRpcServer JsonRpcServer/*)
file(GLOB_RECURSE PaymentGate PaymentGate/*)
file(GLOB_RECURSE PaymentGateService PaymentGateService/*)
file(GLOB_RECURSE SimpleWallet SimpleWallet/*)
file(GLOB_RECURSE Optimizer Optimizer/*)
file(GLOB_RECURSE SimpleWalletLib SimpleWallet/*)

# Tor integration
add_subdirectory(FuegoTor)

# Eldernode services
add_subdirectory(EldernodeIndexManager)
add_subdirectory(BurnDepositValidationService)

# System files
if(WIN32)
  file(GLOB_RECURSE System System/* Platform/Windows/System/*)
elseif(APPLE)
  file(GLOB_RECURSE System System/* Platform/OSX/System/*)
else()
  file(GLOB_RECURSE System System/* Platform/Linux/System/*)
endif()

source_group("" FILES ${Common} ${Crypto} ${CryptoNoteCore} ${CryptoNoteProtocol} ${Daemon} ${JsonRpcServer} ${Http} ${Logging} ${NodeRpcProxy} ${P2p} ${Rpc} ${Serialization} ${SimpleWallet} ${System} ${Transfers} ${Wallet} ${WalletLegacy})

# --------------------------
# Libraries
# --------------------------
add_library(BlockchainExplorer STATIC ${BlockchainExplorer})
target_include_directories(BlockchainExplorer SYSTEM PUBLIC ${JSONCPP_INCLUDE_DIRS})
target_link_libraries(BlockchainExplorer PUBLIC ${JSONCPP_LIBRARIES})
add_library(Common STATIC ${Common})
add_library(Crypto STATIC ${Crypto})

add_library(CryptoNoteCore STATIC ${CryptoNoteCore})
target_compile_options(CryptoNoteCore PRIVATE -Wno-deprecated-declarations)
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
add_dependencies(CryptoNoteCore version)

# --------------------------
# Apply AES/SSE compile options to Crypto library for slow-hash.c
# --------------------------
# Robust AES-NI configuration that works in CI environments

# Always try to enable AES-NI for GCC/Clang on non-Android systems
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT ANDROID)
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

# Platform-specific adjustments
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # On macOS, only enable for Intel Macs
    if (CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        # Apple Silicon - use software AES
        target_compile_definitions(Crypto PRIVATE NO_AES)
        target_compile_options(Crypto PRIVATE -msse2 -msse4.1)  # Keep SSE for other crypto operations
        message(STATUS "Apple Silicon detected, using software AES for Crypto library")
    else()
        # Intel Mac or unknown - AES-NI should be enabled above
        message(STATUS "macOS Intel or unknown architecture, AES-NI configuration applied")
    endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux - AES-NI should be enabled above for GCC/Clang
    message(STATUS "Linux detected, AES-NI configuration applied")
endif()

message(STATUS "=== End AES-NI Configuration ===")

add_library(Http STATIC ${Http})
add_library(InProcessNode STATIC ${InProcessNode})
add_library(Logging STATIC ${Logging})
add_library(NodeRpcProxy STATIC ${NodeRpcProxy})
add_library(Rpc STATIC ${Rpc})
add_library(P2P STATIC ${CryptoNoteProtocol} ${P2p})
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
add_library(System STATIC ${System})

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
add_executable(Daemon ${Daemon})
add_executable(TestnetDaemon Daemon/DaemonCommandsHandler.cpp Daemon/DaemonCommandsHandler.h TestnetDaemon/TestnetDaemon.cpp)
add_executable(SimpleWallet ${SimpleWallet})
add_executable(PaymentGateService ${PaymentGateService})
add_executable(Optimizer ${Optimizer})

# JSON libraries will be linked through keyword signatures below

# Add TestnetWallet executable
add_executable(testnet-wallet-cli TestnetWallet/TestnetWallet.cpp)

# Set proper linker flags to find FuegoTor library
# FuegoTor library path is handled by target_link_directories
# No global link_directories needed to avoid path duplication

# Add link directory to ensure FuegoTor can be found for all relevant targets
set(FUEGOTOR_LIB_PATH ${CMAKE_CURRENT_BINARY_DIR}/src/FuegoTor)
target_link_directories(PaymentGateService PRIVATE ${FUEGOTOR_LIB_PATH})
target_link_directories(SimpleWallet PRIVATE ${FUEGOTOR_LIB_PATH})
target_link_directories(Daemon PRIVATE ${FUEGOTOR_LIB_PATH})
target_link_directories(testnet-wallet-cli PRIVATE ${FUEGOTOR_LIB_PATH})

# Create a minimal library for testnet-wallet without the SimpleWallet class conflict
# TestnetWallet needs PasswordContainer but NOT the SimpleWallet class implementation (which would cause ODR violation)
add_library(TestnetWalletCommon STATIC SimpleWallet/PasswordContainer.cpp)

target_link_libraries(testnet-wallet-cli PRIVATE
    # REMOVED: SimpleWalletLib - contains SimpleWallet.cpp with conflicting simple_wallet class
    TestnetWalletCommon
    Wallet
    NodeRpcProxy
    Transfers
    Rpc
    Http
    Fuegotor
    CryptoNoteCore
    Crypto
    P2P
    System
    Logging
    Common
    upnpc-static
    BlockchainExplorer
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

# Add include paths for headers used by TestnetWallet (SimpleWallet.h and TestnetWallet.h)
target_include_directories(testnet-wallet-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/SimpleWallet
    ${CMAKE_CURRENT_SOURCE_DIR}/TestnetWallet
)

set_target_properties(testnet-wallet-cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src")

if (MSVC)
  target_link_libraries(System PRIVATE ws2_32)
endif ()

target_link_libraries(Daemon PRIVATE
    CryptoNoteCore
    P2P
    Rpc
    System
    Http
    Logging
    Common
    InProcessNode
    upnpc-static
    BlockchainExplorer
    Fuegotor
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

target_link_libraries(TestnetDaemon PRIVATE
    CryptoNoteCore
    P2P
    Rpc
    System
    Http
    Logging
    Common
    Crypto
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
    Http
    System
    Logging
    Common
    InProcessNode
    upnpc-static
    BlockchainExplorer
    Fuegotor
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
    Fuegotor
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

target_link_libraries(SimpleWallet PRIVATE
    SimpleWalletLib
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
    Fuegotor
    ${Boost_LIBRARIES}
    Serialization
    ${JSONCPP_LIBRARIES}
)

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
set_property(TARGET SimpleWallet PROPERTY OUTPUT_NAME "xfg_wallet")
set_property(TARGET testnet-wallet-cli PROPERTY OUTPUT_NAME "test_wallet")
set_property(TARGET PaymentGateService PROPERTY OUTPUT_NAME "walletd")
set_property(TARGET Daemon PROPERTY OUTPUT_NAME "fuegod")
set_property(TARGET TestnetDaemon PROPERTY OUTPUT_NAME "testnetd")
set_property(TARGET Optimizer PROPERTY OUTPUT_NAME "fuego-optimizer")
