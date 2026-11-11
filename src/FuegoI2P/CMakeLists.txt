# FuegoI2P - I2P Integration for Fuego
# CMakeLists.txt for FuegoI2P module

cmake_minimum_required(VERSION 3.16)
project(FuegoI2P VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
find_package(OpenSSL QUIET)

# Platform-specific libraries
if(WIN32)
    set(FUEGOI2P_PLATFORM_LIBS
        ws2_32
        wsock32
        iphlpapi
    )
elseif(APPLE)
    set(FUEGOI2P_PLATFORM_LIBS
        "-framework Security"
        "-framework SystemConfiguration"
    )
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(EVENT libevent)
        if(EVENT_FOUND)
            set(FUEGOI2P_EXTRA_LIBS ${EVENT_LIBRARIES})
            set(FUEGOI2P_EXTRA_INCLUDE_DIRS ${EVENT_INCLUDE_DIRS})
        endif()
    endif()
    set(FUEGOI2P_PLATFORM_LIBS
        pthread
        resolv
        crypto
    )
endif()

# I2P integration - use SOCKS5 proxy by default (i2pd compatible)
find_library(I2P_SOCKS_LIB i2pd)
if(I2P_SOCKS_LIB)
    set(HAVE_I2P_LIB TRUE)
    message(STATUS "FuegoI2P: Found i2pd library")
else()
    set(HAVE_I2P_LIB FALSE)
    message(STATUS "FuegoI2P: i2pd library not found, using SOCKS5 proxy only")
endif()

option(ENABLE_FUEGOI2P "Enable FuegoI2P integration" ON)
option(ENABLE_I2P_LIB "Use i2pd library if available" ${HAVE_I2P_LIB})
option(ENABLE_I2P_HIDDEN_SERVICE "Enable I2P hidden service support" ON)

if(NOT ENABLE_FUEGOI2P)
    message(STATUS "FuegoI2P: Disabled")
    return()
endif()

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
    ${FUEGOI2P_EXTRA_INCLUDE_DIRS}
)

set(FUEGOI2P_SOURCES
    src/Fuegoi2p.cpp
)

add_library(FuegoI2P STATIC ${FUEGOI2P_SOURCES})

set_target_properties(FuegoI2P PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "FuegoI2P"
    ARCHIVE_OUTPUT_NAME "FuegoI2P"
)

if(MSVC)
    target_compile_options(FuegoI2P PRIVATE /W3)
else()
    target_compile_options(FuegoI2P PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-deprecated-builtins
        -O2
    )
    target_compile_features(FuegoI2P PRIVATE cxx_std_17)
endif()

if(WIN32)
    target_compile_definitions(FuegoI2P PRIVATE
        _WIN32_WINNT=0x0601
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
elseif(APPLE)
    target_compile_definitions(FuegoI2P PRIVATE
        __MACOSX__
        _DARWIN_C_SOURCE
    )
else()
    target_compile_definitions(FuegoI2P PRIVATE
        _GNU_SOURCE
        _POSIX_C_SOURCE=200809L
    )
endif()

if(ENABLE_I2P_LIB AND HAVE_I2P_LIB)
    target_compile_definitions(FuegoI2P PRIVATE FUEGOI2P_USE_LIBI2P=1)
endif()

if(ENABLE_I2P_HIDDEN_SERVICE)
    target_compile_definitions(FuegoI2P PRIVATE FUEGOI2P_ENABLE_HIDDEN_SERVICE=1)
endif()

target_link_libraries(FuegoI2P PRIVATE
    Threads::Threads
    ${FUEGOI2P_PLATFORM_LIBS}
    ${FUEGOI2P_EXTRA_LIBS}
)

if(WIN32)
    target_link_libraries(FuegoI2P PRIVATE ws2_32 iphlpapi)
endif()

if(ENABLE_I2P_LIB AND HAVE_I2P_LIB)
    target_link_libraries(FuegoI2P PRIVATE ${I2P_SOCKS_LIB})
    target_compile_definitions(FuegoI2P PRIVATE FUEGOI2P_HAVE_I2P=1)
endif()

if(OpenSSL_FOUND)
    target_link_libraries(FuegoI2P PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(FuegoI2P PRIVATE FUEGOI2P_HAVE_OPENSSL=1)
endif()

message(STATUS "FuegoI2P Configuration:")
message(STATUS "  Build: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Enable FuegoI2P: ${ENABLE_FUEGOI2P}")
message(STATUS "  Use i2pd lib: ${ENABLE_I2P_LIB}")
message(STATUS "  Hidden service: ${ENABLE_I2P_HIDDEN_SERVICE}")
message(STATUS "  i2pd lib found: ${HAVE_I2P_LIB}")
