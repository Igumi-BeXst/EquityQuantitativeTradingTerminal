find_package(ZLIB REQUIRED)

# ============================================
# Foundation — always builds, zero external deps except nlohmann-json
# ============================================
add_library(st_foundation STATIC
    foundation/stock_code.cpp
    foundation/bar.cpp
    foundation/utils/datetime.cpp
    foundation/utils/string_utils.cpp
    foundation/utils/json_utils.cpp
    foundation/utils/indicators.cpp
    foundation/utils/pinyin.cpp
    foundation/utils/csv.cpp
    foundation/scheduler/scheduled_task.cpp
    foundation/scheduler/scheduled_task_store.cpp
)
target_include_directories(st_foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(st_foundation PUBLIC nlohmann_json::nlohmann_json)

# ============================================
# Core, Data, Engine, UI — need Qt
# ============================================
if(BUILD_WITH_QT)

add_library(st_core STATIC
    core/event_bus.cpp
    core/thread_pool.cpp
    core/task_scheduler.cpp
    core/config_manager.cpp
    core/log_manager.cpp
    core/notification_service.cpp
    core/credential_store.cpp
    core/app_paths.cpp
)
target_link_libraries(st_core PUBLIC st_foundation spdlog::spdlog Qt6::Core crypt32)

add_library(st_data STATIC
    data/data_repository.cpp
    data/data_cache.cpp
    data/akshare_provider.cpp
    data/tencent_provider.cpp
    data/quote_poller.cpp
    data/stock_search_index.cpp
    data/data_sync_scheduler.cpp
    data/ai_data_source.cpp
    data/cn_encoding.cpp
    data/tdx/tdx_socket.cpp
    data/tdx/tdx_protocol.cpp
    data/tdx/tdx_models.cpp
    data/tdx/tdx_provider.cpp
    data/provider_factory.cpp
    data/multi_provider.cpp
    data/eastmoney_sector_provider.cpp
    data/eastmoney_funds_provider.cpp
)
target_link_libraries(st_data PUBLIC st_foundation st_core Qt6::Sql Qt6::Network nlohmann_json::nlohmann_json ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(st_data PUBLIC ws2_32)
endif()

add_library(st_engine STATIC
    engine/strategy/istrategy.cpp
    engine/strategy/strategy_manager.cpp
    engine/strategy/templates/turtle.cpp
    engine/strategy/templates/ma_cross_strategy.cpp
    engine/backtest/backtest_engine.cpp
    engine/backtest/order_matcher.cpp
    engine/backtest/fee_calculator.cpp
    engine/backtest/performance.cpp
    engine/paper_trade/paper_trade_engine.cpp
    engine/portfolio/portfolio_engine.cpp
    engine/screener/stock_screener.cpp
    engine/screener/factor_library.cpp
    engine/screener/condition_filter.cpp
    engine/screener/ranker.cpp
    engine/scheduler/screener_scope.cpp
    engine/market/market_engine.cpp
    engine/market/market_scanner.cpp
    engine/market/market_breadth.cpp
    engine/optimizer/grid_search.cpp
    engine/analyzer/strategy_comparator.cpp
    engine/analyzer/stress_test.cpp
    engine/analyzer/monte_carlo.cpp
    engine/analyzer/chip_distribution.cpp
    engine/analyzer/treemap.cpp
    engine/analyzer/overlay_analysis.cpp
    engine/analyzer/custom_index.cpp
    engine/analyzer/custom_index_store.cpp
    engine/journal/trade_journal.cpp
    engine/journal/trade_journal_store.cpp
)
target_link_libraries(st_engine PUBLIC st_data st_core st_foundation talib::talib)

add_library(st_intelligence STATIC
    intelligence/pattern/pattern_recognizer.cpp
    intelligence/advisor/strategy_advisor.cpp
    intelligence/screener/pattern_factor.cpp
    intelligence/sentiment/sentiment_analyzer.cpp
    intelligence/sentiment/eastmoney_news_provider.cpp
)
target_link_libraries(st_intelligence PUBLIC st_engine st_core st_foundation)

add_library(st_ui STATIC
    ui/theme_manager.cpp
    ui/shortcut_manager.cpp
    ui/preferences_dialog.cpp
    ui/resources.qrc
    ui/main_window.cpp
    ui/widgets/kline_chart.cpp
    ui/widgets/time_line_chart.cpp
    ui/widgets/central_chart_widget.cpp
    ui/widgets/overlay_dialog.cpp
    ui/widgets/equity_curve_widget.cpp
    ui/widgets/chart_layers.cpp
    ui/widgets/market_index_strip.cpp
    ui/widgets/crosshair.cpp
    ui/panels/stock_search_bar.cpp
    ui/panels/market_panel.cpp
    ui/panels/backtest_panel.cpp
    ui/panels/strategy_panel.cpp
    ui/panels/screener_panel.cpp
    ui/panels/paper_trade_panel.cpp
    ui/panels/optimization_panel.cpp
    ui/panels/strategy_compare_panel.cpp
    ui/panels/stress_test_panel.cpp
    ui/panels/quant_window.cpp
    ui/panels/funds_window.cpp
    ui/panels/journal_window.cpp
    ui/panels/task_window.cpp
    ui/panels/pattern_panel.cpp
    ui/panels/advisor_panel.cpp
    ui/panels/sentiment_panel.cpp
    ui/panels/log_panel.cpp
    ui/widgets/market_depth_widget.cpp
    ui/widgets/stock_key_data_widget.cpp
    ui/widgets/chip_panel.cpp
    ui/widgets/custom_index_editor.cpp
    ui/widgets/custom_index_panel.cpp
    ui/widgets/reminder_popup.cpp
    ui/panels/sector_panel.cpp
    ui/models/stock_list_model.cpp
    ui/models/trade_table_model.cpp
    ui/models/market_rank_model.cpp
    ui/models/screen_result_model.cpp
    ui/models/grid_search_table_model.cpp
    ui/models/comparison_table_model.cpp
    ui/models/pattern_table_model.cpp
)
target_link_libraries(st_ui PUBLIC st_engine st_intelligence Qt6::Widgets)

# GUI 子系统（WIN32）：启动时不弹控制台窗口（日志走 UI 面板 + 文件）
add_executable(StockTerminal WIN32 main.cpp)
target_link_libraries(StockTerminal PRIVATE st_ui st_engine st_data st_core st_foundation)

# 数据拉取测试工具（P4 验证用）
add_executable(fetch_test tools_fetch_test.cpp)
target_link_libraries(fetch_test PRIVATE st_data st_core st_foundation)

# 端到端回测验证工具（P4 验证用）
add_executable(backtest_demo tools_backtest_demo.cpp)
target_link_libraries(backtest_demo PRIVATE st_engine st_data st_core st_foundation)

# 精选股票池校验工具（P5 验证用：对比静态名称与腾讯实时名称）
add_executable(curated_check tools_curated_check.cpp)
target_link_libraries(curated_check PRIVATE st_data st_core st_foundation)

# QuotePoller 实时轮询验证工具（P5 验证用）
add_executable(quote_test tools_quote_test.cpp)
target_link_libraries(quote_test PRIVATE st_data st_core st_foundation)

# K线/分时接口 live 验证工具（P6 验证用）
add_executable(kline_test tools_kline_test.cpp)
target_link_libraries(kline_test PRIVATE st_data st_core st_foundation)

# 图表离屏渲染验证工具（P6 验证用）
add_executable(chart_render tools_chart_render.cpp)
target_link_libraries(chart_render PRIVATE st_ui st_engine st_data st_core st_foundation)

# TDX 实连验证工具（P9 验证用）
add_executable(tdx_live tools_tdx_live.cpp)
target_link_libraries(tdx_live PRIVATE st_data st_core st_foundation Qt6::Core)

# TDX 底层诊断工具（P9 开发期）
add_executable(tdx_diag tools_tdx_diag.cpp)
target_link_libraries(tdx_diag PRIVATE st_data st_core st_foundation Qt6::Core)

# TDX 市场异常探测工具（全A股批量报价异常检查）
add_executable(tdx_market_probe tools_tdx_market_probe.cpp)
target_link_libraries(tdx_market_probe PRIVATE st_data st_core st_foundation Qt6::Core)

# Intelligence 层实连验证工具（P9 验证用：形态识别/形态因子/优化建议/情绪桩）
add_executable(intelligence_demo tools_intelligence_demo.cpp)
target_link_libraries(intelligence_demo PRIVATE st_intelligence st_engine st_data st_core st_foundation Qt6::Core)

# 盘口五档实连诊断工具（P10 验证用）
add_executable(depth_probe tools_depth_probe.cpp)
target_link_libraries(depth_probe PRIVATE st_data st_core st_foundation Qt6::Core)

# 东财基本面实连校准工具（P10 第二轮验证用）
add_executable(fundamental_calib tools_fundamental_calib.cpp)
target_link_libraries(fundamental_calib PRIVATE st_data st_core st_foundation Qt6::Core)

# 引擎堆压力测试（无头定位越界写）
add_executable(heap_stress tools_heap_stress.cpp)
target_link_libraries(heap_stress PRIVATE st_intelligence st_engine st_data st_core st_foundation Qt6::Core)

# 筹码分布实连校准工具（P10 第三轮验证用）
add_executable(chip_calib tools_chip_calib.cpp)
target_link_libraries(chip_calib PRIVATE st_engine st_data st_core st_foundation Qt6::Core)

# 东财板块行情实连校准工具（P10 第四轮验证用）
add_executable(sector_calib tools_sector_calib.cpp)
target_link_libraries(sector_calib PRIVATE st_data st_core st_foundation Qt6::Core)

# 自定义指数实连校准工具（P10 第七轮验证用）
add_executable(custom_index_live tools_custom_index_live.cpp)
target_link_libraries(custom_index_live PRIVATE st_engine st_data st_core st_foundation Qt6::Core)

# 自定义指数图表离屏渲染验证（P10 第七轮开发用）
add_executable(custom_index_render tools_custom_index_render.cpp)
target_link_libraries(custom_index_render PRIVATE st_ui st_engine st_data st_core st_foundation Qt6::Core)

else()
# Without Qt: just build the Foundation CLI test runner
add_executable(StockTerminal main.cpp)
target_link_libraries(StockTerminal PRIVATE st_foundation)
endif()

# 量化工作台崩溃复现（调试用）
add_executable(quant_repro tools_quant_repro.cpp)
target_link_libraries(quant_repro PRIVATE st_ui st_engine st_data st_core st_foundation Qt6::Core)



# 资金数据实连校准工具（P10 第八轮验证用）
add_executable(funds_calib tools_funds_calib.cpp)
target_link_libraries(funds_calib PRIVATE st_data st_core st_foundation Qt6::Core)

