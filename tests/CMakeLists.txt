# Foundation tests — always build
add_executable(test_foundation
    test_foundation/test_main.cpp
    test_foundation/test_stock_code.cpp
    test_foundation/test_datetime.cpp
    test_foundation/test_string_utils.cpp
    test_foundation/test_json_utils.cpp
)
target_link_libraries(test_foundation PRIVATE st_foundation GTest::gtest GTest::gtest_main)
gtest_discover_tests(test_foundation)

# Remaining tests need Qt
if(BUILD_WITH_QT)
    add_executable(test_core
        test_core/test_main.cpp
        test_core/test_config_manager.cpp
        test_core/test_thread_pool.cpp
        test_core/test_credential_store.cpp
        test_core/test_app_paths.cpp
    )
    target_link_libraries(test_core PRIVATE st_core GTest::gtest GTest::gtest_main)
    gtest_discover_tests(test_core)

    add_executable(test_data
        test_data/test_main.cpp
        test_data/test_repository.cpp
        test_data/test_cache.cpp
        test_data/test_search_index.cpp
    )
    target_link_libraries(test_data PRIVATE st_data GTest::gtest GTest::gtest_main)
    gtest_discover_tests(test_data)

    add_executable(test_engine
        test_engine/test_main.cpp
        test_engine/test_fee_calculator.cpp
        test_engine/test_performance.cpp
        test_engine/test_backtest_engine.cpp
        test_engine/test_paper_trade.cpp
    )
    target_link_libraries(test_engine PRIVATE st_engine st_data GTest::gtest GTest::gtest_main)
    gtest_discover_tests(test_engine)
endif()
