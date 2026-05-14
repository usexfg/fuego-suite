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

# Optionally disable FuegoTor integration to simplify builds (default OFF for safety)
option(ENABLE_FUEGOTOR "Enable FuegoTor integration" OFF)

# If enabled, add the FuegoTor subdirectory, otherwise skip
if(ENABLE_FUEGOTOR)
  add_subdirectory(src/FuegoTor)
endif()

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

# Set C standard for proper stdint.h and other C headers
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wno-deprecated-builtins")
    message(STATUS "Using ${CMAKE_CXX_COMPILER_ID} compiler with C++17")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
    message(STATUS "Using MSVC compiler with C++17")
endif()

# Set C standard flags
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_C_COMPILER_ID STREQUAL "Clang" OR
   CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
    message(STATUS "Using ${CMAKE_C_COMPILER_ID} compiler with C99")
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

    # Add macOS specific definitions for Boost.Asio
    add_definitions(-D_DARWIN_C_SOURCE -D__BSD_VISIBLE=1)

    # Fix header search paths for macOS
    if(APPLE)
        # Ensure proper C++ standard library is used
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")

        # Explicitly set the sysroot to ensure proper header search paths
        if(EXISTS "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
            set(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
        elseif(EXISTS "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
            set(CMAKE_OSX_SYSROOT "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
        endif()

        # Add C++ standard library headers explicitly
        if(EXISTS "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
            include_directories(SYSTEM /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1)
            include_directories(SYSTEM /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include)
        elseif(EXISTS "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
            include_directories(SYSTEM /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1)
            include_directories(SYSTEM /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include)
        endif()

        # Add C standard library headers
        if(EXISTS "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
            include_directories(SYSTEM /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include)
        elseif(EXISTS "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
            include_directories(SYSTEM /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include)
        endif()

        # Add Homebrew include paths for macOS (after standard library paths)
        # Only use /opt/homebrew for ARM64, skip /usr/local to avoid x86_64 Boost
        include_directories(SYSTEM /opt/homebrew/include)

        # Ensure C targets also get proper system includes
        if(EXISTS "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
            set(CMAKE_INCLUDE_PATH "${CMAKE_INCLUDE_PATH}:/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include")
        elseif(EXISTS "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
            set(CMAKE_INCLUDE_PATH "${CMAKE_INCLUDE_PATH}:/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include")
        endif()
    endif()
endif()

if(MSVC)
   include_directories(src/Platform/Windows)
   include_directories(src/Platform/Posix)
elseif(APPLE)
   include_directories(src/Platform/OSX)
   include_directories(src/Platform/Posix)
   # Add additional system include directories for macOS
   # Only use /opt/homebrew for ARM64, skip /usr/local to avoid x86_64 Boost
   include_directories(SYSTEM /opt/homebrew/include)

   # Add C++ standard library headers explicitly
   if(EXISTS "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
       include_directories(SYSTEM /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1)
       include_directories(SYSTEM /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include)
   elseif(EXISTS "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
       include_directories(SYSTEM /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1)
       include_directories(SYSTEM /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include)
   endif()
else()
   include_directories(src/Platform/Linux)
   include_directories(src/Platform/Posix)
endif()
include_directories(src/Platform/Android)
include_directories(src/Platform/FreeBSD)

# --------------------------
# Boost - Updated for 1.86
# --------------------------
# Link static BOOST libraries everywhere
set(Boost_NO_BOOST_CMAKE OFF)
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_STATIC_RUNTIME OFF)

if(MINGW)
  set(Boost_THREADAPI win32)
  include_directories(SYSTEM src/platform/mingw)
endif()

# Required components - system is not needed in Boost 1.80+
# For ARM64 macOS, directly specify Boost paths to avoid x86_64 Boost in /usr/local
if(APPLE AND EXISTS "/opt/homebrew/opt/boost@1.85/lib")
    # Skip find_package and directly set Boost variables for ARM64
    set(Boost_INCLUDE_DIRS "/opt/homebrew/opt/boost@1.85/include")
    set(Boost_LIBRARIES
        "/opt/homebrew/opt/boost@1.85/lib/libboost_filesystem-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_thread-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_date_time-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_chrono-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_regex-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_serialization-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_program_options-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_coroutine-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_context-mt.dylib"
        "/opt/homebrew/opt/boost@1.85/lib/libboost_atomic-mt.dylib"
    )
    set(Boost_FOUND TRUE)
    message(STATUS "Using ARM64 Boost from /opt/homebrew/opt/boost@1.85")
else()
    # Required components - system is not needed in Boost 1.80+
    find_package(Boost REQUIRED COMPONENTS filesystem thread date_time chrono regex serialization program_options coroutine context atomic)
endif()
set(Boost_USE_STATIC_LIBS ON)
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
message(STATUS "Boost Found: ${Boost_INCLUDE_DIRS}")
message(STATUS "Found Boost Version: ${Boost_VERSION}")

# Platform-specific Boost library handling
if(MINGW)
  set(Boost_LIBRARIES "${Boost_LIBRARIES};ws2_32;mswsock;iphlpapi;bcrypt")
elseif(APPLE)
  # Ensure proper linking on macOS - prioritize Homebrew arm64 Boost
  # First: Check for Homebrew arm64 at /opt/homebrew (Apple Silicon)
  if(EXISTS "/opt/homebrew/opt/boost@1.85/lib")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/opt/boost@1.85/lib -Wl,-rpath,/opt/homebrew/opt/boost@1.85/lib")
    link_directories("/opt/homebrew/opt/boost@1.85/lib")
  elseif(EXISTS "/opt/homebrew/opt/boost/lib")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/opt/boost/lib -Wl,-rpath,/opt/homebrew/opt/boost/lib")
    link_directories("/opt/homebrew/opt/boost/lib")
  endif()
  # Skip x86_64 Boost at /usr/local on Apple Silicon to avoid architecture mismatch
elseif(OPENBSD OR ANDROID)
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
      set(JSONCPP_INCLUDE_DIRS "${BREW_JSONCPP_PREFIX}/include/json")
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
  # For non-macOS, try find_package first
  find_package(jsoncpp QUIET)
  if(jsoncpp_FOUND OR JSONCPP_FOUND)
    if(jsoncpp_FOUND)
      message(STATUS "Found jsoncpp via find_package")
    endif()
  else()
    set(JSONCPP_FOUND FALSE)
  endif()
endif()

if(NOT JSONCPP_FOUND)
  # Manual detection fallback
  set(JSONCPP_FOUND FALSE)
  set(JSONCPP_LIBRARIES "jsoncpp")
  set(JSONCPP_LIBRARY_DIRS "")

  # Try standard locations
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
    if(EXISTS "C:/vcpkg/installed/x64-windows/include/json/json.h")
      set(JSONCPP_INCLUDE_DIRS "C:/vcpkg/installed/x64-windows/include/json")
      set(JSONCPP_FOUND TRUE)
      message(STATUS "Found jsoncpp via vcpkg at ${JSONCPP_INCLUDE_DIRS}")
    endif()
  endif()

  if(JSONCPP_FOUND)
    include_directories(SYSTEM ${JSONCPP_INCLUDE_DIRS})
  else()
    include_directories(SYSTEM /usr/include/json /usr/include/jsoncpp/json /usr/include/x86_64-linux-gnu/jsoncpp/json)
    message(WARNING "jsoncpp headers not found, using fallback paths")
  endif()
endif()

# Ensure variables are always defined
if(NOT DEFINED JSONCPP_LIBRARIES)
  set(JSONCPP_LIBRARIES "jsoncpp")
endif()
if(NOT DEFINED JSONCPP_INCLUDE_DIRS)
  set(JSONCPP_INCLUDE_DIRS "/usr/include/json")
endif()
if(NOT DEFINED JSONCPP_LIBRARY_DIRS)
  set(JSONCPP_LIBRARY_DIRS "")
endif()

message(STATUS "jsoncpp include dirs: ${JSONCPP_INCLUDE_DIRS}")
message(STATUS "jsoncpp libraries: ${JSONCPP_LIBRARIES}")
message(STATUS "jsoncpp library dirs: ${JSONCPP_LIBRARY_DIRS}")

if(JSONCPP_LIBRARY_DIRS)
  link_directories(${JSONCPP_LIBRARY_DIRS})
endif()

message(STATUS "Boost libraries: ${Boost_LIBRARIES}")

# Configure runtime path for Boost libraries on macOS
if(APPLE)
    # Prioritize Homebrew arm64 Boost only on Apple Silicon
    if(EXISTS "/opt/homebrew/opt/boost@1.85/lib")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/opt/boost@1.85/lib -Wl,-rpath,/opt/homebrew/opt/boost@1.85/lib")
    endif()

    # Also add ICU library paths (arm64 only)
    if(EXISTS "/opt/homebrew/opt/icu4c/lib")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/opt/icu4c/lib -Wl,-rpath,/opt/homebrew/opt/icu4c/lib")
    endif()
endif()

# --------------------------
# ICU (skipped on Android)
# --------------------------
if(ANDROID)
    message(STATUS "Android build detected: skipping ICU discovery")
else()
    if(APPLE)
        # Ensure proper ICU paths for macOS
        if(EXISTS "/opt/homebrew/opt/icu4c")
            list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/icu4c")
            set(ICU_ROOT "/opt/homebrew/opt/icu4c")
        elseif(EXISTS "/usr/local/opt/icu4c")
            list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/icu4c")
            set(ICU_ROOT "/usr/local/opt/icu4c")
        endif()
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
        include_directories(SYSTEM ${ICU_INCLUDE_DIRS})

        # On macOS, ensure we have the correct ICU library paths
        if(APPLE)
            # Add ICU library directories
            if(EXISTS "/opt/homebrew/opt/icu4c/lib")
                link_directories("/opt/homebrew/opt/icu4c/lib")
            elseif(EXISTS "/usr/local/opt/icu4c/lib")
                link_directories("/usr/local/opt/icu4c/lib")
            endif()

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
    else()
        if(WIN32)
            message(FATAL_ERROR "ICU not found! Make sure it's installed via vcpkg.")
        else()
            message(FATAL_ERROR "ICU not found, install via Homebrew: brew install icu4c")
        endif()
    endif()
endif()

# --------------------------
# Boost linking for executables (macOS)
# --------------------------
if(APPLE AND Boost_FOUND)
    message(STATUS "Linking Boost libraries to executables")
    foreach(exec_target Daemon SimpleWallet PaymentGateService Optimizer TestnetDaemon testnet-wallet-cli)
        if(TARGET ${exec_target})
            message(STATUS "Linking Boost to ${exec_target}")
            target_link_libraries(${exec_target} PRIVATE ${Boost_LIBRARIES})
        endif()
    endforeach()
endif()

# --------------------------
# Compiler flags
# --------------------------
if(APPLE)
    # Enhanced compiler flags for macOS to ensure proper header search
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -Wno-deprecated -Wno-deprecated-builtins -stdlib=libc++")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

    # Ensure we're using the correct C++ standard library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")

    # Set explicit sysroot if available
    if(EXISTS "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
        set(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
    elseif(EXISTS "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
        set(CMAKE_OSX_SYSROOT "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
    endif()

    # Ensure proper system header inclusion
    if(EXISTS "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
    elseif(EXISTS "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
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
# add_subdirectory(tui)  # Disabled - requires separate build

# --------------------------
# ICU linking for executables (macOS)
# --------------------------
if(APPLE AND ICU_FOUND)
    message(STATUS "Linking ICU libraries to executables")
    foreach(exec_target Daemon SimpleWallet PaymentGateService Optimizer TestnetDaemon testnet-wallet-cli)
        if(TARGET ${exec_target})
            message(STATUS "Linking ICU to ${exec_target}")
            target_link_libraries(${exec_target} PRIVATE ${ICU_LIBRARIES})

            # Ensure proper rpath for ICU libraries on macOS
            if(EXISTS "/opt/homebrew/opt/icu4c/lib")
                set_target_properties(${exec_target} PROPERTIES
                    LINK_FLAGS "-L/opt/homebrew/opt/icu4c/lib -Wl,-rpath,/opt/homebrew/opt/icu4c/lib")
            elseif(EXISTS "/usr/local/opt/icu4c/lib")
                set_target_properties(${exec_target} PROPERTIES
                    LINK_FLAGS "-L/usr/local/opt/icu4c/lib -Wl,-rpath,/usr/local/opt/icu4c/lib")
            endif()
        endif()
    endforeach()

    # Note: TUI is a Go application and doesn't need ICU linking
    # The Go TUI component is built separately with its own dependencies
endif()
