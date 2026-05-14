# FuegoMeshtastic - Meshtastic Integration for Fuego
# CMakeLists.txt for FuegoMeshtastic module

cmake_minimum_required(VERSION 3.16)
project(FuegoMeshtastic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

# Feature options
option(ENABLE_FUEGOMESH "Enable FuegoMeshtastic integration" ON)
option(ENABLE_MESHTASTIC_SERIAL "Enable serial transport" ON)
option(ENABLE_MESHTASTIC_TCP "Enable TCP transport (MQTT bridge)" ON)
option(ENABLE_MESHTASTIC_BT "Enable Bluetooth transport" OFF)

if(NOT ENABLE_FUEGOMESH)
    message(STATUS "FuegoMeshtastic: Disabled")
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
)

# Source files
set(FUEGOMESH_SOURCES
    src/MeshtasticIntegration.cpp
)

# Platform-specific sources
if(APPLE)
    set(FUEGOMESH_PLATFORM_SOURCES "")
    set(FUEGOMESH_PLATFORM_LIBS
        "-framework IOKit"
        "-framework CoreFoundation"
    )
elseif(WIN32)
    set(FUEGOMESH_PLATFORM_SOURCES "")
    set(FUEGOMESH_PLATFORM_LIBS
        setupapi
    )
else()
    set(FUEGOMESH_PLATFORM_SOURCES "")
    set(FUEGOMESH_PLATFORM_LIBS
        pthread
        rt
    )
endif()

# Create library
add_library(FuegoMeshtastic STATIC ${FUEGOMESH_SOURCES} ${FUEGOMESH_PLATFORM_SOURCES})

set_target_properties(FuegoMeshtastic PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "FuegoMeshtastic"
    ARCHIVE_OUTPUT_NAME "FuegoMeshtastic"
)

# Compiler options
if(MSVC)
    target_compile_options(FuegoMeshtastic PRIVATE /W3)
else()
    target_compile_options(FuegoMeshtastic PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-deprecated-builtins
        -O2
    )
    target_compile_features(FuegoMeshtastic PRIVATE cxx_std_17)
endif()

# Platform-specific definitions
if(WIN32)
    target_compile_definitions(FuegoMeshtastic PRIVATE
        _WIN32_WINNT=0x0601
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
elseif(APPLE)
    target_compile_definitions(FuegoMeshtastic PRIVATE
        __MACOSX__
        _DARWIN_C_SOURCE
    )
else()
    target_compile_definitions(FuegoMeshtastic PRIVATE
        _GNU_SOURCE
        _POSIX_C_SOURCE=200809L
    )
endif()

# Feature definitions
if(ENABLE_MESHTASTIC_SERIAL)
    target_compile_definitions(FuegoMeshtastic PRIVATE FUEGOMESH_SERIAL_ENABLED=1)
endif()

if(ENABLE_MESHTASTIC_TCP)
    target_compile_definitions(FuegoMeshtastic PRIVATE FUEGOMESH_TCP_ENABLED=1)
endif()

if(ENABLE_MESHTASTIC_BT)
    target_compile_definitions(FuegoMeshtastic PRIVATE FUEGOMESH_BT_ENABLED=1)
endif()

# Link libraries
target_link_libraries(FuegoMeshtastic PRIVATE
    Threads::Threads
    ${FUEGOMESH_PLATFORM_LIBS}
)

# Installation rules
install(TARGETS FuegoMeshtastic
    EXPORT FuegoMeshtasticTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/fuego/FuegoMeshtastic
    FILES_MATCHING PATTERN "*.h"
)

# Summary
message(STATUS "FuegoMeshtastic Configuration:")
message(STATUS "  Build: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Enable FuegoMeshtastic: ${ENABLE_FUEGOMESH}")
message(STATUS "  Serial transport: ${ENABLE_MESHTASTIC_SERIAL}")
message(STATUS "  TCP transport: ${ENABLE_MESHTASTIC_TCP}")
message(STATUS "  Bluetooth transport: ${ENABLE_MESHTASTIC_BT}")
