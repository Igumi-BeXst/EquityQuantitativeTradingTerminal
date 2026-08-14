# Foundation tests — always build
add_executable(test_foundation
    test_foundation/test_main.cpp
    test_foundation/test_stock_code.cpp
    test_foundation/test_datetime.cpp
    test_foundation/test_string_utils.cpp
    test_foundation/test_json_utils.cpp
    test_foundation/test_indicators.cpp
    test_foundation/test_pinyin.cpp
    test_foundation/test_csv.cpp
    test_foundation/test_csv_export.cpp
    test_foundation/test_scheduled_task.cpp
    test_foundation/test_watchlist_store.cpp
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
        test_core/test_log_manager.cpp
    )
    target_link_libraries(test_core PRIVATE st_core GTest::gtest GTest::gtest_main)
    gtest_discover_tests(test_core)

    add_executable(test_data
        test_data/test_main.cpp
        test_data/test_repository.cpp
        test_data/test_cache.cpp
        test_data/test_search_index.cpp
        test_data/test_tencent_provider.cpp
        test_data/test_tdx_protocol.cpp
        test_data/test_tdx_provider.cpp
        test_data/test_multi_provider.cpp
        test_data/test_akshare_provider.cpp
        test_data/test_sector_provider.cpp
        test_data/test_funds_provider.cpp
        test_data/test_sector_constituents.cpp
    )
    target_compile_definitions(test_data PRIVATE
        ST_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    target_link_libraries(test_data PRIVATE st_data GTest::gtest GTest::gtest_main)
    gtest_discover_tests(test_data)

    add_executable(test_engine
        test_engine/test_main.cpp
        test_engine/test_backtest_engine.cpp
        test_engine/test_performance.cpp
        test_engine/test_fee_calculator.cpp
        test_engine/test_paper_trade.cpp
        test_engine/test_market.cpp
        test_engine/test_factors.cpp
        test_engine/test_screener.cpp
        test_engine/test_grid_search.cpp
        test_engine/test_analyzer.cpp
        test_engine/test_chip_distribution.cpp
        test_engine/test_treemap.cpp
        test_engine/test_overlay_analysis.cpp
        test_engine/test_range_statistics.cpp
        test_engine/test_custom_index.cpp
        test_engine/test_custom_index_store.cpp
        test_engine/test_trade_journal.cpp
        test_engine/test_trade_mark.cpp
        test_engine/test_trade_journal_store.cpp
        test_engine/test_screener_scope.cpp
    )
    target_link_libraries(test_engine PRIVATE st_engine st_data GTest::gtest GTest::gtest_main)
    gtest_discover_tests(test_engine)

    add_executable(test_intelligence
        test_intelligence/test_main.cpp
        test_intelligence/test_pattern_recognizer.cpp
        test_intelligence/test_strategy_advisor.cpp
        test_intelligence/test_pattern_factor.cpp
        test_intelligence/test_sentiment_analyzer.cpp
        test_intelligence/test_eastmoney_news_provider.cpp
        test_intelligence/test_composite_signal.cpp
    )
    target_link_libraries(test_intelligence PRIVATE st_intelligence st_engine st_data GTest::gtest GTest::gtest_main)
    gtest_discover_tests(test_intelligence)
endif()
