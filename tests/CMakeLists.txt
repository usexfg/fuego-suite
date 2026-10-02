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

# Spend-key-bound (v2) commitment keys; the vector is shared with the Rust SDK.
add_executable(commitment_keys_v2_tests UnitTests/CommitmentKeysV2Test.cpp)
target_include_directories(commitment_keys_v2_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(commitment_keys_v2_tests PRIVATE CryptoNoteCore gtest gtest_main)
add_test(NAME commitment_keys_v2_tests COMMAND commitment_keys_v2_tests)
