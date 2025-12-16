# TUI (Terminal User Interface) for Fuego
# Go-based terminal UI for controlling fuegod and walletd

cmake_minimum_required(VERSION 3.16)

# Check if Go is available
find_program(GO_EXECUTABLE go)
if(NOT GO_EXECUTABLE)
    message(WARNING "Go executable not found. TUI will not be built.")
    return()
endif()

message(STATUS "Found Go: ${GO_EXECUTABLE}")

set(TUI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(TUI_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(TUI_BUILD_DIR ${TUI_BINARY_DIR}/build)

# Function to check Go version
function(check_go_version)
    execute_process(
        COMMAND ${GO_EXECUTABLE} version
        OUTPUT_VARIABLE GO_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Go version: ${GO_VERSION_OUTPUT}")

    # Extract version number
    string(REGEX MATCH "go([0-9]+)\\.([0-9]+)" GO_VERSION ${GO_VERSION_OUTPUT})
    if(NOT GO_VERSION)
        message(FATAL_ERROR "Failed to parse Go version")
    endif()

    set(GO_MAJOR ${CMAKE_MATCH_1})
    set(GO_MINOR ${CMAKE_MATCH_2})

    # Require Go 1.18 or higher
    if(${GO_MAJOR} LESS 1 OR (${GO_MAJOR} EQUAL 1 AND ${GO_MINOR} LESS 18))
        message(FATAL_ERROR "Go 1.18 or higher is required. Found Go ${GO_MAJOR}.${GO_MINOR}")
    endif()
endfunction()

check_go_version()

# Custom command to build the TUI
add_custom_command(
    OUTPUT ${TUI_BUILD_DIR}/fuego-tui
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TUI_BUILD_DIR}
    COMMAND ${GO_EXECUTABLE} mod tidy
    COMMAND ${CMAKE_COMMAND} -E env
            ${GO_EXECUTABLE} build -o ${TUI_BUILD_DIR}/fuego-tui
    WORKING_DIRECTORY ${TUI_SOURCE_DIR}
    DEPENDS ${TUI_SOURCE_DIR}/*.go ${TUI_SOURCE_DIR}/go.mod ${TUI_SOURCE_DIR}/go.sum
    COMMENT "Building Fuego TUI"
)

# Custom target for the TUI
add_custom_target(fuego-tui ALL
    DEPENDS ${TUI_BUILD_DIR}/fuego-tui
    COMMENT "Fuego TUI built"
)

# Install target for the TUI
install(PROGRAMS ${TUI_BUILD_DIR}/fuego-tui
    DESTINATION bin
    COMPONENT Runtime
    RENAME fuego-tui
)

# Add TUI to the list of components
set(FUEGO_COMPONENTS ${FUEGO_COMPONENTS} TUI PARENT_SCOPE)

# Add TUI documentation
install(FILES ${TUI_SOURCE_DIR}/README.md
    DESTINATION share/doc/fuego/tui
    COMPONENT Development
    OPTIONAL
)

# Add test target if needed
option(BUILD_TUI_TESTS "Build TUI tests" OFF)
if(BUILD_TUI_TESTS)
    add_custom_target(test_tui
        COMMAND ${CMAKE_COMMAND} -E env GOOS=${CMAKE_SYSTEM_NAME}
                ${GO_EXECUTABLE} test
        WORKING_DIRECTORY ${TUI_SOURCE_DIR}
        COMMENT "Running TUI tests"
    )
endif()
