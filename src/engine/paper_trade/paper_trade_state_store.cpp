#include "engine/paper_trade/paper_trade_state_store.h"
#include "foundation/utils/datetime.h"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace st {

void to_json(nlohmann::json& j, const Bar& b) {
    j = {
        {"code",       b.code.fullCode()},
        {"time",       utils::toDateTimeString(b.time)},
        {"period",     static_cast<int>(b.period)},
        {"open",       b.open},
        {"high",       b.high},
        {"low",        b.low},
        {"close",      b.close},
        {"volume",     b.volume},
        {"amount",     b.amount},
        {"turnover",   b.turnoverRate}
    };
}

void from_json(const nlohmann::json& j, Bar& b) {
    b.code = StockCode(j.at("code").get<std::string>());
    b.time = utils::parseDateTime(j.at("time").get<std::string>());
    b.period = static_cast<BarPeriod>(j.at("period").get<int>());
    j.at("open").get_to(b.open);
    j.at("high").get_to(b.high);
    j.at("low").get_to(b.low);
    j.at("close").get_to(b.close);
    j.at("volume").get_to(b.volume);
    j.at("amount").get_to(b.amount);
    b.turnoverRate = j.value("turnover", 0.0);
}

void to_json(nlohmann::json& j, const Position& p) {
    j = {
        {"code",         p.code.fullCode()},
        {"quantity",     p.quantity},
        {"avgCost",      p.avgCost},
        {"currentPrice", p.currentPrice},
        {"marketValue",  p.marketValue},
        {"costBasis",    p.costBasis},
        {"profitLoss",   p.profitLoss},
        {"profitLossPct", p.profitLossPct},
        {"available",    p.available},
        {"todayBuy",     p.todayBuy},
        {"holdDays",     p.holdDays}
    };
}

void from_json(const nlohmann::json& j, Position& p) {
    p.code = StockCode(j.at("code").get<std::string>());
    j.at("quantity").get_to(p.quantity);
    j.at("avgCost").get_to(p.avgCost);
    j.at("currentPrice").get_to(p.currentPrice);
    j.at("marketValue").get_to(p.marketValue);
    j.at("costBasis").get_to(p.costBasis);
    j.at("profitLoss").get_to(p.profitLoss);
    j.at("profitLossPct").get_to(p.profitLossPct);
    j.at("available").get_to(p.available);
    j.at("todayBuy").get_to(p.todayBuy);
    j.at("holdDays").get_to(p.holdDays);
}

void to_json(nlohmann::json& j, const Trade& t) {
    j = {
        {"id",         t.id},
        {"orderId",    t.orderId},
        {"code",       t.code.fullCode()},
        {"direction",  static_cast<int>(t.direction)},
        {"price",      t.price},
        {"volume",     t.volume},
        {"amount",     t.amount},
        {"commission", t.commission},
        {"stampTax",   t.stampTax},
        {"otherFees",  t.otherFees},
        {"totalFee",   t.totalFee},
        {"time",       utils::toDateTimeString(t.time)},
        {"strategyId", t.strategyId}
    };
}

void from_json(const nlohmann::json& j, Trade& t) {
    j.at("id").get_to(t.id);
    j.at("orderId").get_to(t.orderId);
    t.code = StockCode(j.at("code").get<std::string>());
    t.direction = static_cast<Direction>(j.at("direction").get<int>());
    j.at("price").get_to(t.price);
    j.at("volume").get_to(t.volume);
    j.at("amount").get_to(t.amount);
    j.at("commission").get_to(t.commission);
    j.at("stampTax").get_to(t.stampTax);
    j.at("otherFees").get_to(t.otherFees);
    j.at("totalFee").get_to(t.totalFee);
    t.time = utils::parseDateTime(j.at("time").get<std::string>());
    j.at("strategyId").get_to(t.strategyId);
}

void to_json(nlohmann::json& j, const PaperTradeEngineState& s) {
    j = {
        {"initialCapital",  s.initialCapital},
        {"cash",            s.cash},
        {"currentTradeDate", s.currentTradeDate},
        {"positions",       s.positions},
        {"trades",          s.trades},
        {"history",         nlohmann::json::object()}
    };
    for (const auto& [code, bars] : s.history) {
        j["history"][code] = bars;
    }
}

void from_json(const nlohmann::json& j, PaperTradeEngineState& s) {
    j.at("initialCapital").get_to(s.initialCapital);
    j.at("cash").get_to(s.cash);
    s.currentTradeDate = j.value("currentTradeDate", std::string());
    j.at("positions").get_to(s.positions);
    j.at("trades").get_to(s.trades);
    s.history.clear();
    if (j.contains("history") && j.at("history").is_object()) {
        for (auto it = j.at("history").begin(); it != j.at("history").end(); ++it) {
            s.history[it.key()] = it.value().get<std::vector<Bar>>();
        }
    }
}

void to_json(nlohmann::json& j, const PaperTradeState& s) {
    j = {
        {"strategyId", s.strategyId},
        {"p1",         s.p1},
        {"p2",         s.p2},
        {"capital",    s.capital},
        {"slippage",   s.slippage},
        {"symbols",    s.symbols},
        {"engine",     s.engine}
    };
}

void from_json(const nlohmann::json& j, PaperTradeState& s) {
    j.at("strategyId").get_to(s.strategyId);
    j.at("p1").get_to(s.p1);
    j.at("p2").get_to(s.p2);
    s.capital = j.value("capital", 0.0);
    s.slippage = j.value("slippage", 0.0);
    j.at("symbols").get_to(s.symbols);
    j.at("engine").get_to(s.engine);
}

bool PaperTradeStateStore::save(const std::string& path, const PaperTradeState& state) const {
    try {
        std::filesystem::path p(path);
        if (!p.parent_path().empty() && !std::filesystem::exists(p.parent_path())) {
            std::filesystem::create_directories(p.parent_path());
        }
        nlohmann::json j = state;
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2);
        return static_cast<bool>(ofs);
    } catch (const std::exception&) {
        return false;
    }
}

bool PaperTradeStateStore::load(const std::string& path, PaperTradeState& state) const {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    try {
        auto j = nlohmann::json::parse(ifs);
        j.get_to(state);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace st
