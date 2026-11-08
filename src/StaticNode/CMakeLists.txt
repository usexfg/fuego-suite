# Static node library for iOS / mobile embedding.
# Produces libFuegoNodeStatic.a with a C API surface.

option(BUILD_STATIC_NODE "Build the drop-in static node library" OFF)

if(NOT BUILD_STATIC_NODE)
  return()
endif()

add_library(FuegoNodeStatic STATIC
  fuego_node.cpp
)

target_include_directories(FuegoNodeStatic PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)

# Link the existing core pieces so the final .a is self-contained for the
# node path. Wallet / swap / mining targets are intentionally omitted.
target_link_libraries(FuegoNodeStatic PUBLIC
  CryptoNoteCore
  InProcessNode
  Crypto
  Common
  System
  Logging
  Serialization
  P2P
  Http
)

if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  target_compile_definitions(FuegoNodeStatic PRIVATE FUEGO_IOS=1)
  # Prefer smaller binary on device.
  target_compile_options(FuegoNodeStatic PRIVATE -Os)
endif()

install(TARGETS FuegoNodeStatic ARCHIVE DESTINATION lib)
install(FILES fuego_node.h DESTINATION include)
