set(UPNPC_BUILD_STATIC ON CACHE BOOL "Build static library")
set(UPNPC_BUILD_SHARED OFF CACHE BOOL "Build shared library")
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Build test executables")

add_subdirectory(miniupnpc)

if (DO_TESTS)
  add_subdirectory(gtest)
endif()

set_property(TARGET upnpc-static PROPERTY FOLDER "external")

if(MSVC)
  set_property(TARGET upnpc-static APPEND_STRING PROPERTY COMPILE_FLAGS " -wd4244 -wd4267")
elseif(NOT MSVC)
  set_property(TARGET upnpc-static APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-undef -Wno-unused-result -Wno-unused-value")
endif()

if(DO_TESTS)
  set_property(TARGET gtest gtest_main PROPERTY FOLDER "external")
endif()

# secp256k1 — required for ETH/BCH signing in SwapDaemon
option(USE_VENDORED_SECP256K1 "Build secp256k1 from external/" ON)
if(USE_VENDORED_SECP256K1)
  set(SECP256K1_BUILD_BENCHMARK           OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_TESTS               OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_EXHAUSTIVE_TESTS    OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_CTIME_TESTS         OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_RECOVERY    ON  CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_SCHNORRSIG  ON  CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_EXTRAKEYS   ON  CACHE BOOL "" FORCE)
  add_subdirectory(secp256k1)
endif()
