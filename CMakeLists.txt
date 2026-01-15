cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

# Set BOOST_NO_BOOST_CMAKE early to disable CMake config mode and force classic FindBoost
set(BOOST_NO_BOOST_CMAKE ON)
set(Boost_NO_BOOST_CMAKE ON)
set(Boost_DIR "")
set(Boost_CONFIG_PATH "")

include(CheckCXXCompilerFlag)
set(VERSION "0.2")
# Packaged from main commits
set(COMMIT 72946d9)
set(REFS " (HEAD -> master)")

set(CMAKE_CXX_STANDARD 14)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CONFIGURATION_TYPES Debug RelWithDebInfo Release CACHE STRING INTERNAL)
set(CMAKE_SKIP_INSTALL_RULES ON)
set(CMAKE_SKIP_PACKAGE_ALL_DEPENDENCY ON)
set(CMAKE_SUPPRESS_REGENERATION ON)

project(fuegoX)

# --------------------------
# Version generation
# --------------------------
set(VERSION "0.2")
set(COMMIT 72946d9)
set(REFS " (HEAD -> master)")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/version")
configure_file("src/version.h.in" "${CMAKE_BINARY_DIR}/version/version.h")
add_custom_target(version ALL)

# --------------------------
# C++ Standard
# --------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
    message(STATUS "Using ${CMAKE_CXX_COMPILER_ID} compiler with C++17")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
    message(STATUS "Using MSVC compiler with C++17")
endif()

# Debug/Release flags - platform-specific
if(MSVC)
    # MSVC uses different optimization and warning flags
    set(CMAKE_C_FLAGS_DEBUG   "/Zi /Od /MDd")
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /MDd")
    set(CMAKE_C_FLAGS_RELEASE   "/O2 /DNDEBUG /MD")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG /MD")
    # Disable specific warnings that cause issues
    string(REPLACE "/W3" "/W0" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/W3" "/W0" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
else()
    set(CMAKE_C_FLAGS_DEBUG   "-g3 -O0")
    set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0")
    set(CMAKE_C_FLAGS_RELEASE   "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "C++ Flags: ${CMAKE_CXX_FLAGS}")

# --------------------------
# Include directories
# --------------------------
include_directories(include src external "${CMAKE_BINARY_DIR}/version")

if(APPLE OR FREEBSD)
    include_directories(SYSTEM /usr/include/malloc)
    if(POLICY CMP0042)
        cmake_policy(SET CMP0042 NEW)
    endif()
    enable_language(ASM)
endif()

if(MSVC)
    include_directories(src/Platform/Windows)
elseif(APPLE)
    include_directories(src/Platform/OSX)
    include_directories(src/Platform/Posix)
else()
    include_directories(src/Platform/Linux)
    include_directories(src/Platform/Posix)
endif()

# --------------------------

# --------------------------
# Set default static linking option based on platform
if(MSVC OR MINGW)
  set(DEFAULT_STATIC true)
else()
  set(DEFAULT_STATIC false)
endif()

option(STATIC "Link libraries statically" ${DEFAULT_STATIC})

# Clear Boost variables from cache to ensure fresh detection
unset(Boost_INCLUDE_DIRS CACHE)
unset(Boost_LIBRARIES CACHE)
unset(Boost_LIBRARY_DIRS CACHE)
unset(Boost_VERSION CACHE)
unset(Boost_FOUND CACHE)

set(OLD_LIB_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
if(STATIC AND MINGW)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif()

# Link BOOST libraries based on STATIC option
set(Boost_NO_BOOST_CMAKE ON)
if(STATIC)
  set(Boost_USE_STATIC_LIBS ON)
  set(Boost_USE_STATIC_RUNTIME ON)
  message(STATUS "Using static Boost libraries")
else()
  set(Boost_USE_STATIC_LIBS OFF)
  set(Boost_USE_STATIC_RUNTIME OFF)
  message(STATUS "Using dynamic Boost libraries")
endif()
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_DEBUG_LIBS OFF)

if(MINGW)
  set(Boost_THREADAPI win32)
  include_directories(SYSTEM src/platform/mingw)
endif()

find_package(Boost REQUIRED COMPONENTS system filesystem thread date_time chrono regex serialization program_options coroutine context atomic)
message(STATUS "Boost Found: ${Boost_INCLUDE_DIRS}")
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
message(STATUS "Found Boost Version: ${Boost_VERSION}")
message(STATUS "Boost Libraries: ${Boost_LIBRARIES}")
message(STATUS "Boost Library Dirs: ${Boost_LIBRARY_DIRS}")

set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_LIB_SUFFIXES})
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Could not find Boost libraries, please make sure you have installed Boost or libboost-all-dev (version 1.58 or higher) or the equivalent")
elseif(Boost_FOUND)
  message(STATUS "Found Boost Version: ${Boost_VERSION}")
endif()

# Platform-specific Boost library handling
if(MINGW)
  set(Boost_LIBRARIES "${Boost_LIBRARIES};ws2_32;mswsock;iphlpapi;bcrypt")
elseif(APPLE OR OPENBSD OR ANDROID)
  set(Boost_LIBRARIES "${Boost_LIBRARIES}")
elseif(NOT MSVC)
  set(Boost_LIBRARIES "${Boost_LIBRARIES};rt")
endif()

# --------------------------
# jsoncpp Configuration
# --------------------------
# Check for Homebrew installation first (takes precedence on macOS)
if(APPLE)
    # Check via Homebrew on macOS first
    execute_process(COMMAND brew --prefix jsoncpp OUTPUT_VARIABLE BREW_JSONCPP_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE BREW_RESULT)
    if(BREW_RESULT EQUAL 0)
      set(JSONCPP_INCLUDE_DIRS "${BREW_JSONCPP_PREFIX}/include")
      set(JSONCPP_LIBRARY_DIRS "${BREW_JSONCPP_PREFIX}/lib")
      set(JSONCPP_LIBRARIES "${BREW_JSONCPP_PREFIX}/lib/libjsoncpp.dylib")
      set(JSONCPP_FOUND TRUE)
      message(STATUS "Found jsoncpp via Homebrew at ${JSONCPP_INCLUDE_DIRS}")
      message(STATUS "jsoncpp library: ${JSONCPP_LIBRARIES}")
    else()
      # Fall back to find_package if Homebrew fails
      find_package(jsoncpp QUIET)
      if(jsoncpp_FOUND OR JSONCPP_FOUND)
        if(jsoncpp_FOUND)
          message(STATUS "Found jsoncpp via find_package")
        endif()
      else()
        set(JSONCPP_FOUND FALSE)
      endif()
    endif()
else()
  # For non-macOS, try multiple detection methods
  set(JSONCPP_FOUND FALSE)

  # Try pkg-config first
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(JSONCPP QUIET jsoncpp)
    if(JSONCPP_FOUND)
      message(STATUS "Found jsoncpp via pkg-config")
      message(STATUS "JSONCPP_INCLUDE_DIRS: ${JSONCPP_INCLUDE_DIRS}")
      message(STATUS "JSONCPP_LIBRARIES: ${JSONCPP_LIBRARIES}")
      set(JSONCPP_INCLUDE_DIRS ${JSONCPP_INCLUDE_DIRS})
      set(JSONCPP_LIBRARIES ${JSONCPP_LIBRARIES})
    endif()
  endif()

  # Try find_package if pkg-config didn't work
  if(NOT JSONCPP_FOUND)
    find_package(jsoncpp QUIET)
    if(jsoncpp_FOUND)
      message(STATUS "Found jsoncpp via find_package")
      message(STATUS "JSONCPP_INCLUDE_DIRS: ${JSONCPP_INCLUDE_DIRS}")
      message(STATUS "JSONCPP_LIBRARIES: ${JSONCPP_LIBRARIES}")
    endif()
  endif()

  # Skip find_package on Ubuntu to avoid config file errors
  # Manual detection should handle header locations correctly
  # if(NOT JSONCPP_FOUND)
  #   find_package(jsoncpp NAMES jsoncpp jsoncpp-config jsoncppConfig QUIET NO_MODULE)
  #   if(JSONCPP_FOUND)
  #     message(STATUS "Found jsoncpp via standard module")
  #     message(STATUS "JSONCPP_INCLUDE_DIRS: ${JSONCPP_INCLUDE_DIRS}")
  #     message(STATUS "JSONCPP_LIBRARIES: ${JSONCPP_LIBRARIES}")
  #   endif()
  # endif()
endif()

if(NOT JSONCPP_FOUND)
  # Manual detection fallback
  set(JSONCPP_FOUND FALSE)
  set(JSONCPP_LIBRARIES "jsoncpp")
  set(JSONCPP_LIBRARY_DIRS "")

  message(STATUS "Attempting manual jsoncpp detection...")

  # Try standard locations - check both json/json.h and jsoncpp/json/json.h patterns
  if(EXISTS "/usr/include/jsoncpp/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/include/jsoncpp")
    set(JSONCPP_FOUND TRUE)
    message(STATUS "Found jsoncpp headers at /usr/include/jsoncpp")
  elseif(EXISTS "/usr/include/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/include")
    set(JSONCPP_FOUND TRUE)
    message(STATUS "Found jsoncpp headers at /usr/include")
  elseif(EXISTS "/usr/include/x86_64-linux-gnu/jsoncpp/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/include/x86_64-linux-gnu/jsoncpp")
    set(JSONCPP_FOUND TRUE)
    message(STATUS "Found jsoncpp headers at /usr/include/x86_64-linux-gnu/jsoncpp")
  elseif(EXISTS "/usr/local/include/jsoncpp/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/local/include/jsoncpp")
    set(JSONCPP_FOUND TRUE)
    message(STATUS "Found jsoncpp headers at /usr/local/include/jsoncpp")
  elseif(EXISTS "/usr/local/include/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/local/include")
    set(JSONCPP_FOUND TRUE)
    message(STATUS "Found jsoncpp headers at /usr/local/include")
  elseif(WIN32)
    if(EXISTS "C:/vcpkg/installed/x64-windows/include/json/json.h")
      set(JSONCPP_INCLUDE_DIRS "C:/vcpkg/installed/x64-windows/include/json")
      set(JSONCPP_FOUND TRUE)
      message(STATUS "Found jsoncpp via vcpkg at ${JSONCPP_INCLUDE_DIRS}")
    endif()
  endif()

  if(JSONCPP_FOUND)
    message(STATUS "Using JSONCPP_INCLUDE_DIRS: ${JSONCPP_INCLUDE_DIRS}")
    include_directories(SYSTEM ${JSONCPP_INCLUDE_DIRS})
  else()
    message(STATUS "Manual detection failed, checking common paths...")
    # Debug what files actually exist
    if(EXISTS "/usr/include/jsoncpp/json/json.h")
      message(STATUS "Found: /usr/include/jsoncpp/json/json.h")
    endif()
    if(EXISTS "/usr/include/json/json.h")
      message(STATUS "Found: /usr/include/json/json.h")
    endif()
    if(EXISTS "/usr/local/include/jsoncpp/json/json.h")
      message(STATUS "Found: /usr/local/include/jsoncpp/json/json.h")
    endif()

    # More comprehensive fallback paths - order matters for correct header resolution
    include_directories(SYSTEM
      /usr/include/jsoncpp
      /usr/include
      /usr/include/x86_64-linux-gnu/jsoncpp
      /usr/local/include/jsoncpp
      /usr/local/include)
    message(WARNING "jsoncpp headers not found via detection, using fallback paths")
  endif()
endif()

# Ensure variables are always defined
if(NOT DEFINED JSONCPP_LIBRARIES)
  set(JSONCPP_LIBRARIES "jsoncpp")
endif()
if(NOT DEFINED JSONCPP_INCLUDE_DIRS)
  # Try to detect the most common installation paths
  if(EXISTS "/usr/include/jsoncpp/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/include/jsoncpp")
  elseif(EXISTS "/usr/include/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/include")
  elseif(EXISTS "/usr/local/include/jsoncpp/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/local/include/jsoncpp")
  elseif(EXISTS "/usr/local/include/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/local/include")
  else()
    # Default fallback
    set(JSONCPP_INCLUDE_DIRS "/usr/include/jsoncpp")
  endif()
endif()
if(NOT DEFINED JSONCPP_LIBRARY_DIRS)
  set(JSONCPP_LIBRARY_DIRS "")
endif()

message(STATUS "jsoncpp include dirs: ${JSONCPP_INCLUDE_DIRS}")
message(STATUS "jsoncpp libraries: ${JSONCPP_LIBRARIES}")
message(STATUS "jsoncpp library dirs: ${JSONCPP_LIBRARY_DIRS}")

# Final verification that headers can be found
if(NOT JSONCPP_FOUND)
  if(EXISTS "${JSONCPP_INCLUDE_DIRS}/json/json.h")
    message(STATUS "Verified: jsoncpp headers found at ${JSONCPP_INCLUDE_DIRS}/json/json.h")
    set(JSONCPP_FOUND TRUE)
  else()
    message(WARNING "Could not verify jsoncpp headers at expected location ${JSONCPP_INCLUDE_DIRS}/json/json.h")
  endif()
endif()

if(JSONCPP_LIBRARY_DIRS)
  link_directories(${JSONCPP_LIBRARY_DIRS})
endif()

message(STATUS "Boost libraries: ${Boost_LIBRARIES}")

# --------------------------
# ICU (skipped on Android)
# --------------------------
if(ANDROID)
    message(STATUS "Android build detected: skipping ICU discovery")
else()
    if(APPLE)
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/icu4c" "/usr/local/opt/icu4c")
    endif()

    # Special handling for vcpkg on Windows
    if(WIN32 AND DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        message(STATUS "Using vcpkg toolchain, configuring ICU search...")
        # First try to find ICU without specifying components (vcpkg way)
        find_package(ICU QUIET)
        if(NOT ICU_FOUND)
            # If that fails, try with explicit components
            find_package(ICU REQUIRED COMPONENTS data i18n uc)
        endif()
    else()
        # Standard ICU detection for non-vcpkg builds
        find_package(ICU REQUIRED COMPONENTS data i18n uc)
    endif()

    if(ICU_FOUND)
        message(STATUS "Found ICU includes: ${ICU_INCLUDE_DIRS}")
        message(STATUS "Found ICU libraries: ${ICU_LIBRARIES}")
        message(STATUS "ICU version: ${ICU_VERSION}")
        include_directories(${ICU_INCLUDE_DIRS})
    else()
        if(WIN32)
            message(FATAL_ERROR "ICU not found! Make sure it's installed via vcpkg.")
        else()
            message(FATAL_ERROR "ICU not found, install via Homebrew: brew install icu4c")
        endif()
    endif()

    # On macOS, ensure we have the correct ICU library paths
    if(APPLE)
        if(ICU_DATA_LIBRARY)
            message(STATUS "ICU Data Library: ${ICU_DATA_LIBRARY}")
        endif()
        if(ICU_I18N_LIBRARY)
            message(STATUS "ICU I18N Library: ${ICU_I18N_LIBRARY}")
        endif()
        if(ICU_UC_LIBRARY)
            message(STATUS "ICU UC Library: ${ICU_UC_LIBRARY}")
        endif()
    endif()
endif()

# --------------------------
# Testing
# --------------------------
option(BUILD_TESTS "Build test executables" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --------------------------
# Subdirectories
# --------------------------
add_subdirectory(external)
add_subdirectory(src)
add_subdirectory(tui)

# --------------------------
# ICU linking for executables (macOS)
# --------------------------
if(APPLE AND ICU_FOUND)
    message(STATUS "Linking ICU libraries to executables")
    foreach(exec_target Daemon SimpleWallet PaymentGateService Optimizer TestnetDaemon testnet-wallet-cli)
        if(TARGET ${exec_target})
            message(STATUS "Linking ICU to ${exec_target}")
            target_link_libraries(${exec_target} PRIVATE ${ICU_LIBRARIES})
        endif()
    endforeach()

    # Note: TUI is a Go application and doesn't need ICU linking
    # The Go TUI component is built separately with its own dependencies
endif()
