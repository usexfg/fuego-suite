cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

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
else()
    include_directories(src/Platform/Linux)
endif()

# --------------------------
# Boost
# --------------------------
if(APPLE)
    if(EXISTS /opt/homebrew)
        set(BOOST_ROOT /opt/homebrew)
    elseif(EXISTS /usr/local)
        set(BOOST_ROOT /usr/local)
    endif()
endif()

# First try with all required components
# Note: boost_system is header-only in Boost 1.69+ so we don't require it as a component
# For vcpkg on Windows, header-only components need to be in the include path
set(BOOST_COMPONENTS filesystem thread date_time chrono regex serialization program_options)

# Additional components for MSVC/vcpkg builds
if(MSVC)
    list(APPEND BOOST_COMPONENTS variant uuid)
endif()

# Try to find coroutine component (may not be available in all Boost versions)
find_package(Boost 1.65 COMPONENTS coroutine QUIET)
if(Boost_COROUTINE_FOUND)
    list(APPEND BOOST_COMPONENTS coroutine)
    message(STATUS "Boost coroutine component found, including in build")
else()
    message(WARNING "Boost coroutine component not found - using alternative context implementation")
endif()

# Handle vcpkg Boost installation specifically
if(WIN32 AND DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    message(STATUS "Using vcpkg toolchain, configuring Boost search...")
    # For vcpkg, find_package with components should work
    find_package(Boost 1.65 REQUIRED COMPONENTS ${BOOST_COMPONENTS})
else()
    # Find Boost with available components (non-vcpkg)
    find_package(Boost 1.65 REQUIRED COMPONENTS ${BOOST_COMPONENTS})
endif()
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")

# Handle macOS warnings for deprecated Boost builtins
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    add_compile_options(-Wno-deprecated-builtins)
    message(STATUS "Suppressing deprecated builtin warnings for Boost 1.65 compatibility")
endif()
# Verify boost/variant.hpp exists for MSVC
if(MSVC AND Boost_INCLUDE_DIRS)
    if(EXISTS "${Boost_INCLUDE_DIRS}/boost/variant.hpp")
        message(STATUS "✓ boost/variant.hpp confirmed")
    else()
        message(WARNING "boost/variant.hpp not found at ${Boost_INCLUDE_DIRS}/boost/variant.hpp")
    endif()
endif()

# Check for Homebrew installation first (takes precedence on macOS)
if(APPLE)
    # Check via Homebrew on macOS first
    execute_process(COMMAND brew --prefix jsoncpp OUTPUT_VARIABLE BREW_JSONCPP_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE BREW_RESULT)
    if(BREW_RESULT EQUAL 0)
      set(JSONCPP_INCLUDE_DIRS "${BREW_JSONCPP_PREFIX}/include/json")
      set(JSONCPP_LIBRARY_DIRS "${BREW_JSONCPP_PREFIX}/lib")
      # For Homebrew on macOS, we need to use the full path to the library
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
  # For non-macOS, try find_package first
  find_package(jsoncpp QUIET)

  if(jsoncpp_FOUND OR JSONCPP_FOUND)
    # jsoncpp was found via find_package
    if(jsoncpp_FOUND)
      message(STATUS "Found jsoncpp via find_package")
    endif()
  else()
    set(JSONCPP_FOUND FALSE)
  endif()
endif()

if(NOT JSONCPP_FOUND)
  # Fall back to manual detection
  set(JSONCPP_FOUND FALSE)
  set(JSONCPP_LIBRARIES "jsoncpp")
  set(JSONCPP_LIBRARY_DIRS "")

  # Try standard locations, with preference for the expected location
  if(EXISTS "/usr/include/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/include/json")
    set(JSONCPP_FOUND TRUE)
    message(STATUS "Found jsoncpp headers at /usr/include/json")
  elseif(EXISTS "/usr/include/jsoncpp/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/include/jsoncpp/json")
    set(JSONCPP_FOUND TRUE)
    message(STATUS "Found jsoncpp headers at /usr/include/jsoncpp/json")
  elseif(EXISTS "/usr/include/x86_64-linux-gnu/jsoncpp/json/json.h")
    set(JSONCPP_INCLUDE_DIRS "/usr/include/x86_64-linux-gnu/jsoncpp/json")
    set(JSONCPP_FOUND TRUE)
    message(STATUS "Found jsoncpp headers at /usr/include/x86_64-linux-gnu/jsoncpp/json")
  elseif(WIN32)
    # Check for vcpkg on Windows
    if(EXISTS "C:/vcpkg/installed/x64-windows/include/json/json.h")
      set(JSONCPP_INCLUDE_DIRS "C:/vcpkg/installed/x64-windows/include/json")
      set(JSONCPP_FOUND TRUE)
      message(STATUS "Found jsoncpp via vcpkg at ${JSONCPP_INCLUDE_DIRS}")
    endif()
  endif()

  # Apply JSON include directories if found
  if(JSONCPP_FOUND)
    include_directories(SYSTEM ${JSONCPP_INCLUDE_DIRS})
  else()
    # As a last resort, try common include locations
    include_directories(SYSTEM /usr/include/json /usr/include/jsoncpp/json /usr/include/x86_64-linux-gnu/jsoncpp/json)
    message(WARNING "jsoncpp headers not found, using fallback paths")
    endif()
  endif()

# Ensure JSONCPP_LIBRARIES is always defined
if(NOT DEFINED JSONCPP_LIBRARIES)
  set(JSONCPP_LIBRARIES "jsoncpp")
endif()

# Ensure JSONCPP_INCLUDE_DIRS is always defined
if(NOT DEFINED JSONCPP_INCLUDE_DIRS)
  set(JSONCPP_INCLUDE_DIRS "/usr/include/json")
endif()

# Ensure JSONCPP_LIBRARY_DIRS is always defined
if(NOT DEFINED JSONCPP_LIBRARY_DIRS)
  set(JSONCPP_LIBRARY_DIRS "")
endif()

message(STATUS "jsoncpp include dirs: ${JSONCPP_INCLUDE_DIRS}")
message(STATUS "jsoncpp libraries: ${JSONCPP_LIBRARIES}")
message(STATUS "jsoncpp library dirs: ${JSONCPP_LIBRARY_DIRS}")

# Add library directory if specified
if(JSONCPP_LIBRARY_DIRS)
  link_directories(${JSONCPP_LIBRARY_DIRS})
endif()
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")

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
