# Alias registration bugfix coverage (persistence + reorg-undo + the fee
# destination check). See AliasIndex.h / AliasIndex.cpp and Blockchain.cpp's
# BlockCacheSerializer / popBlock / alias-registration fee check.
add_executable(alias_index_tests
    UnitTests/AliasIndexPersistenceTest.cpp
    UnitTests/AliasFeeDestinationTest.cpp
)
target_include_directories(alias_index_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(alias_index_tests PRIVATE
    CryptoNoteCore
    gtest
    gtest_main
)
add_test(NAME alias_index_tests COMMAND alias_index_tests)

# Owner-bound commitment derivation: sender/view-only material must not sign,
# the scanner must attribute outputs to the matching key, and pre-v11 outputs
# must stay discoverable. See docs/developer/v11-p0-commitment-keys-and-asset-accounting-guide.md
add_executable(commitment_owner_bound_tests
    UnitTests/CommitmentOwnerBoundTest.cpp
)
target_include_directories(commitment_owner_bound_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(commitment_owner_bound_tests PRIVATE
    CryptoNoteCore
    Crypto
    Common
    gtest
    gtest_main
)
add_test(NAME commitment_owner_bound_tests COMMAND commitment_owner_bound_tests)

# Asset classification for commitment terms. Both the input and output side must
# use one table, or per-asset conservation can be broken by decoy reordering.
# See docs/developer/v11-p0-commitment-keys-and-asset-accounting-guide.md section B.
add_executable(asset_classification_tests
    UnitTests/AssetClassificationTest.cpp
)
target_include_directories(asset_classification_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(asset_classification_tests PRIVATE
    CryptoNoteCore
    Crypto
    Common
    gtest
    gtest_main
)
add_test(NAME asset_classification_tests COMMAND asset_classification_tests)

# Aggregate uint64 overflow in input-money accumulation. getTransactionInputAmount
# guards each input's own add, but the running total across inputs wrapped, and a
# wrapped total reads as "funded" to every caller that compares it against outputs.
add_executable(input_amount_overflow_tests
    UnitTests/InputAmountOverflowTest.cpp
)
target_include_directories(input_amount_overflow_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(input_amount_overflow_tests PRIVATE
    CryptoNoteCore
    Crypto
    Common
    Logging
    gtest
    gtest_main
)
add_test(NAME input_amount_overflow_tests COMMAND input_amount_overflow_tests)
