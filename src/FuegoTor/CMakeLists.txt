# FuegoTor - Tor Integration for Fuego
# CMakeLists.txt for FuegoTor module

cmake_minimum_required(VERSION 3.16)
project(FuegoTor VERSION 0.1.0 LANGUAGES CXX)

# C++17 standard requirement
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL QUIET)

# Platform-specific libraries
if(WIN32)
    # Windows-specific libraries - use direct names as they're always available
    set(FUEGOTOR_PLATFORM_LIBS
        ws2_32
        wsock32
        iphlpapi
    )
elseif(APPLE)
    # macOS-specific libraries
    set(FUEGOTOR_PLATFORM_LIBS
        "-framework Security"
        "-framework SystemConfiguration"
    )
else()
    # Linux-specific libraries
    find_package(OpenSSL QUIET)
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        # Try to find libevent for better performance
        pkg_check_modules(EVENT libevent)
        if(EVENT_FOUND)
            set(FUEGOTOR_EXTRA_LIBS ${EVENT_LIBRARIES})
            set(FUEGOTOR_EXTRA_INCLUDE_DIRS ${EVENT_INCLUDE_DIRS})
        endif()
    endif()

    set(FUEGOTOR_PLATFORM_LIBS
        pthread
        resolv
        crypto
    )
endif()

# Tor integration libraries (optional)
find_library(TOR_LIB tor)
if(TOR_LIB)
    set(HAVE_TOR_LIB TRUE)
    message(STATUS "FuegoTor: Found libtor")
else()
    set(HAVE_TOR_LIB FALSE)
    message(WARNING "FuegoTor: libtor not found, using SOCKS5 proxy only")
endif()

# Feature options
option(ENABLE_FUEGOTOR "Enable FuegoTor integration" ON)
option(ENABLE_TOR_LIB "Use libtor if available" ${HAVE_TOR_LIB})
option(ENABLE_HIDDEN_SERVICE "Enable Tor hidden service support" ON)
option(ENABLE_TOR_CONTROL "Enable Tor control protocol" ON)

if(NOT ENABLE_FUEGOTOR)
    message(STATUS "FuegoTor: Disabled")
    return()
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/CryptoNoteCore
    ${CMAKE_SOURCE_DIR}/src/Common
    ${CMAKE_SOURCE_DIR}/src/System
    ${CMAKE_SOURCE_DIR}/src/Logging
    ${CMAKE_SOURCE_DIR}/src/Serialization
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${FUEGOTOR_EXTRA_INCLUDE_DIRS}
)

# Source files
set(FUEGOTOR_SOURCES
    src/Fuegotor.cpp
)

# Create FuegoTor library
add_library(Fuegotor STATIC ${FUEGOTOR_SOURCES})

# Set library properties
set_target_properties(Fuegotor PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "FuegoTor"
    ARCHIVE_OUTPUT_NAME "FuegoTor"
)



# Compiler-specific options
if(MSVC)
    # For MSVC, use less strict warnings to avoid errors on socket definitions
    target_compile_options(Fuegotor PRIVATE /W3)
else()
    target_compile_options(Fuegotor PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-deprecated-builtins
        -Werror
        -O2
    )

    # Enable C++17 features
    target_compile_features(Fuegotor PRIVATE cxx_std_17)
endif()

# Platform-specific compile definitions
if(WIN32)
    target_compile_definitions(Fuegotor PRIVATE
        _WIN32_WINNT=0x0601
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
elseif(APPLE)
    target_compile_definitions(Fuegotor PRIVATE
        __MACOSX__
        _DARWIN_C_SOURCE
    )
else()
    target_compile_definitions(Fuegotor PRIVATE
        _GNU_SOURCE
        _POSIX_C_SOURCE=200809L
    )
endif()

# Feature-specific definitions
if(ENABLE_TOR_LIB)
    target_compile_definitions(Fuegotor PRIVATE FUEGOTOR_USE_LIBTOR=1)
endif()

if(ENABLE_HIDDEN_SERVICE)
    target_compile_definitions(Fuegotor PRIVATE FUEGOTOR_ENABLE_HIDDEN_SERVICE=1)
endif()

if(ENABLE_TOR_CONTROL)
    target_compile_definitions(Fuegotor PRIVATE FUEGOTOR_ENABLE_CONTROL=1)
endif()

# Link libraries
target_link_libraries(Fuegotor PRIVATE
    Threads::Threads
    ${FUEGOTOR_PLATFORM_LIBS}
    ${FUEGOTOR_EXTRA_LIBS}
)

# Ensure Windows libraries are properly linked on Windows
if(WIN32)
    target_link_libraries(Fuegotor PRIVATE ws2_32 iphlpapi)
endif()

# Conditional Tor library linking
if(ENABLE_TOR_LIB AND HAVE_TOR_LIB)
    target_link_libraries(Fuegotor PRIVATE ${TOR_LIB})
    target_compile_definitions(Fuegotor PRIVATE FUEGOTOR_HAVE_TOR=1)
endif()

# Link OpenSSL if available
if(OpenSSL_FOUND)
    target_link_libraries(Fuegotor PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(Fuegotor PRIVATE FUEGOTOR_HAVE_OPENSSL=1)
endif()

# Export configuration
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config/FuegotorConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/FuegotorConfig.h
    @ONLY
)

# Installation rules
install(TARGETS Fuegotor
    EXPORT FuegoTorTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/fuego/FuegoTor
    FILES_MATCHING PATTERN "*.h"
)

# Summary
message(STATUS "FuegoTor Configuration:")
message(STATUS "  Build: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Enable FuegoTor: ${ENABLE_FUEGOTOR}")
message(STATUS "  Use libtor: ${ENABLE_TOR_LIB}")
message(STATUS "  Hidden service: ${ENABLE_HIDDEN_SERVICE}")
message(STATUS "  Tor control: ${ENABLE_TOR_CONTROL}")
message(STATUS "  libtor found: ${HAVE_TOR_LIB}")
