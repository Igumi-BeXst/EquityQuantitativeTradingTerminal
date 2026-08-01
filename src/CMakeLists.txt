# ============================================
# Foundation — always builds, zero external deps except nlohmann-json
# ============================================
add_library(st_foundation STATIC
    foundation/stock_code.cpp
    foundation/utils/datetime.cpp
    foundation/utils/string_utils.cpp
    foundation/utils/json_utils.cpp
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
)
target_link_libraries(st_core PUBLIC st_foundation spdlog::spdlog Qt6::Core)

add_library(st_data STATIC
    data/data_repository.cpp
    data/data_cache.cpp
    data/data_sync_scheduler.cpp
    data/ai_data_source.cpp
)
target_link_libraries(st_data PUBLIC st_foundation st_core Qt6::Sql httplib::httplib)

add_library(st_engine STATIC
    engine/strategy/strategy_manager.cpp
    engine/backtest/backtest_engine.cpp
    engine/backtest/order_matcher.cpp
    engine/backtest/fee_calculator.cpp
    engine/backtest/performance.cpp
    engine/paper_trade/paper_trade_engine.cpp
    engine/portfolio/portfolio_engine.cpp
    engine/market/market_engine.cpp
)
target_link_libraries(st_engine PUBLIC st_data st_core st_foundation)

add_library(st_ui STATIC
    ui/main_window.cpp
    ui/widgets/kline_chart.cpp
    ui/widgets/time_line_chart.cpp
    ui/widgets/chart_layers.cpp
    ui/widgets/market_index_strip.cpp
    ui/widgets/crosshair.cpp
    ui/panels/stock_search_bar.cpp
    ui/panels/market_panel.cpp
    ui/panels/backtest_panel.cpp
    ui/panels/strategy_panel.cpp
    ui/panels/screener_panel.cpp
    ui/panels/log_panel.cpp
    ui/models/stock_list_model.cpp
)
target_link_libraries(st_ui PUBLIC st_engine Qt6::Widgets)

add_executable(StockTerminal main.cpp)
target_link_libraries(StockTerminal PRIVATE st_ui st_engine st_data st_core st_foundation)

else()
# Without Qt: just build the Foundation CLI test runner
add_executable(StockTerminal main.cpp)
target_link_libraries(StockTerminal PRIVATE st_foundation)
endif()
