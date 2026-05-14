set(BURN_DEPOSIT_VALIDATION_SERVICE_SOURCES
    BurnDepositValidationService.cpp
)

# Create the static library
add_library(BurnDepositValidationService STATIC ${BURN_DEPOSIT_VALIDATION_SERVICE_SOURCES})

# Include directories for this library
target_include_directories(BurnDepositValidationService
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

# Link dependencies using keyword form
target_link_libraries(BurnDepositValidationService
    PUBLIC
        CryptoNoteCore
        Common
        Crypto
        Logging
        EldernodeIndexManager
)
