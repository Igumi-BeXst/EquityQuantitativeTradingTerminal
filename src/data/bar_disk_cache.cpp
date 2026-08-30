#include "data/bar_disk_cache.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace st {

namespace {

constexpr uint32_t kMagic   = 0x53544231u;  // "STB1"
constexpr uint32_t kVersion = 1u;
constexpr uint64_t kMaxBarsPerFile = 100000;

} // namespace

BarDiskCache::BarDiskCache(std::string rootDir)
    : rootDir_(std::move(rootDir)) {}

std::string BarDiskCache::filePath(const StockCode& code, BarPeriod period) const {
    return rootDir_ + "/raw/" + code.fullCode() + ".daily.stb";
}

void BarDiskCache::save(const StockCode& code, BarPeriod period,
                        const std::vector<Bar>& bars) const {
    if (bars.empty()) return;
    const std::string path = filePath(code, period);
    const std::string dir = std::filesystem::path(path).parent_path().string();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;

    const uint32_t magic = kMagic;
    const uint32_t version = kVersion;
    const int32_t p = static_cast<int32_t>(period);
    const uint64_t count = static_cast<uint64_t>(bars.size());
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&p), sizeof(p));
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& b : bars) {
        const int64_t ts = static_cast<int64_t>(
            std::chrono::system_clock::to_time_t(b.time));
        const double open = b.open;
        const double high = b.high;
        const double low = b.low;
        const double close = b.close;
        const int64_t volume = static_cast<int64_t>(b.volume);
        const double amount = b.amount;
        const double turnover = b.turnoverRate;
        out.write(reinterpret_cast<const char*>(&ts), sizeof(ts));
        out.write(reinterpret_cast<const char*>(&open), sizeof(open));
        out.write(reinterpret_cast<const char*>(&high), sizeof(high));
        out.write(reinterpret_cast<const char*>(&low), sizeof(low));
        out.write(reinterpret_cast<const char*>(&close), sizeof(close));
        out.write(reinterpret_cast<const char*>(&volume), sizeof(volume));
        out.write(reinterpret_cast<const char*>(&amount), sizeof(amount));
        out.write(reinterpret_cast<const char*>(&turnover), sizeof(turnover));
    }
}

std::vector<Bar> BarDiskCache::loadAll(const StockCode& code,
                                       BarPeriod period) const {
    const std::string path = filePath(code, period);
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    uint32_t magic = 0, version = 0;
    int32_t storedPeriod = 0;
    uint64_t count = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&storedPeriod), sizeof(storedPeriod));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in || magic != kMagic || version != kVersion ||
        storedPeriod != static_cast<int32_t>(period) || count > kMaxBarsPerFile) {
        return {};
    }

    std::vector<Bar> bars;
    bars.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        int64_t ts = 0;
        double open = 0, high = 0, low = 0, close = 0, amount = 0, turnover = 0;
        int64_t volume = 0;
        in.read(reinterpret_cast<char*>(&ts), sizeof(ts));
        in.read(reinterpret_cast<char*>(&open), sizeof(open));
        in.read(reinterpret_cast<char*>(&high), sizeof(high));
        in.read(reinterpret_cast<char*>(&low), sizeof(low));
        in.read(reinterpret_cast<char*>(&close), sizeof(close));
        in.read(reinterpret_cast<char*>(&volume), sizeof(volume));
        in.read(reinterpret_cast<char*>(&amount), sizeof(amount));
        in.read(reinterpret_cast<char*>(&turnover), sizeof(turnover));
        if (!in) return {};
        Bar b;
        b.code = code;
        b.period = period;
        b.time = std::chrono::system_clock::from_time_t(
            static_cast<std::time_t>(ts));
        b.open = open;
        b.high = high;
        b.low = low;
        b.close = close;
        b.volume = static_cast<Volume>(volume);
        b.amount = amount;
        b.turnoverRate = turnover;
        bars.push_back(std::move(b));
    }
    return bars;
}

std::vector<Bar> BarDiskCache::load(const StockCode& code, BarPeriod period,
                                    DateTime start, DateTime end) const {
    std::vector<Bar> all = loadAll(code, period);
    if (all.empty()) return {};

    std::vector<Bar> out;
    out.reserve(all.size());
    for (const auto& b : all) {
        if (b.time >= start && b.time <= end) out.push_back(b);
    }
    return out;
}

} // namespace st
