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
