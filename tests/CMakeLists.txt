# Public audit snapshot: field-to-source mapping, decimal-string wire types for
# amounts above 2^53-1, and that a popped block leaves no residue in the values
# /get_public_audit reports. See docs/plans/2026-10-13-public-audit-api-dev-guide.md.
add_executable(public_audit_tests
    UnitTests/PublicAuditSnapshotTest.cpp
)
target_include_directories(public_audit_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(public_audit_tests PRIVATE
    CryptoNoteCore
    Common
    Crypto
    Logging
    gtest
    gtest_main
)
add_test(NAME public_audit_tests COMMAND public_audit_tests)

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

# Persistence of the published commitment key. The signer cannot choose between the
# owner-bound and legacy derivations without it, and it used to be lost on every
# wallet save/reload. Also covers loading pre-version-2 caches.
add_executable(commitment_persistence_tests
    UnitTests/CommitmentPersistenceTest.cpp
)
target_include_directories(commitment_persistence_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(commitment_persistence_tests PRIVATE
    Transfers
    CryptoNoteCore
    Serialization
    Logging
    Common
    Crypto
    gtest
    gtest_main
)
add_test(NAME commitment_persistence_tests COMMAND commitment_persistence_tests)

# Asset classification for commitment terms. Both the input and output side must
# use one table (Currency::classifyCommitmentTermAsset), or per-asset conservation can
# be broken. See docs/developer/v11-p0-commitment-keys-and-asset-accounting-guide.md section B.
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

# Intra-transaction key-image uniqueness. Uniqueness was checked only against chain
# state, so a transaction spending one key image twice passed validation and then
# failed the connect rollback, which erased inputs by type-blind boost::get and
# threw an uncaught boost::bad_get -- a remote daemon crash. Guards the
# checkKeyImagesUnique check across every key-image-carrying input type.
add_executable(key_image_uniqueness_tests
    UnitTests/KeyImageUniquenessTest.cpp
)
target_include_directories(key_image_uniqueness_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(key_image_uniqueness_tests PRIVATE
    CryptoNoteCore
    Crypto
    Common
    gtest
    gtest_main
)
add_test(NAME key_image_uniqueness_tests COMMAND key_image_uniqueness_tests)

# Dandelion++ stem routing. hop_count arrives from an untrusted peer, and the
# original bare `hop_count++` wrapped UINT32_MAX to 0, handing the sender a
# fresh stem budget every hop so the hop bound was not a bound. Also pins the
# stem/fluff decision boundaries. See CryptoNoteConfig.h
# (dandelionAdvanceHop, dandelionShouldStayInStem) and
# CryptoNoteProtocolHandler.cpp (handle_notify_new_transactions, handle_swap_offer).
add_executable(dandelion_stem_tests
    UnitTests/DandelionStemTest.cpp
)
target_include_directories(dandelion_stem_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(dandelion_stem_tests PRIVATE
    Common
    gtest
    gtest_main
)
add_test(NAME dandelion_stem_tests COMMAND dandelion_stem_tests)

add_executable(fuego_cost_index_tests
    UnitTests/FuegoCostIndexTest.cpp
)
target_include_directories(fuego_cost_index_tests PRIVATE ${CMAKE_SOURCE_DIR}/external/gtest/include)
target_link_libraries(fuego_cost_index_tests PRIVATE
    CryptoNoteCore
    Crypto
    Common
    gtest
    gtest_main
)
add_test(NAME fuego_cost_index_tests COMMAND fuego_cost_index_tests)
