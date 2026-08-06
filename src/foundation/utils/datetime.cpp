#include "foundation/utils/datetime.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace st::utils {

using namespace std::chrono;

static std::time_t to_time_t(DateTime dt) {
    return system_clock::to_time_t(dt);
}

std::string toDateString(DateTime dt) {
    auto t = to_time_t(dt);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d");
    return oss.str();
}

std::string toDateTimeString(DateTime dt) {
    auto t = to_time_t(dt);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

DateTime parseDate(const std::string& s) {
    std::tm tm = {};
    std::string normalized = s;
    // Accept "YYYYMMDD" format
    if (s.size() == 8 && std::isdigit(s[0])) {
        normalized = s.substr(0, 4) + "-" + s.substr(4, 2) + "-" + s.substr(6, 2);
    }
    std::istringstream iss(normalized);
    iss >> std::get_time(&tm, "%Y-%m-%d");
    if (iss.fail()) {
        return DateTime{}; // invalid
    }
    auto tp = system_clock::from_time_t(std::mktime(&tm));
    return tp;
}

DateTime parseDateTime(const std::string& s) {
    std::tm tm = {};
    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        return DateTime{};
    }
    return system_clock::from_time_t(std::mktime(&tm));
}

DateTime parseMinuteTime(const std::string& s) {
    // "yyyyMMddHHmm" (12 位)
    if (s.size() != 12) return DateTime{};
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return DateTime{};
    }
    std::tm tm = {};
    tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(s.substr(4, 2)) - 1;
    tm.tm_mday = std::stoi(s.substr(6, 2));
    tm.tm_hour = std::stoi(s.substr(8, 2));
    tm.tm_min  = std::stoi(s.substr(10, 2));
    tm.tm_sec  = 0;
    return system_clock::from_time_t(std::mktime(&tm));
}

DateTime today() {
    auto now = system_clock::now();
    auto t = to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    tm_buf.tm_hour = 0;
    tm_buf.tm_min = 0;
    tm_buf.tm_sec = 0;
    return system_clock::from_time_t(std::mktime(&tm_buf));
}

DateTime now() {
    return system_clock::now();
}

bool isWeekend(DateTime dt) {
    auto t = to_time_t(dt);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    return tm_buf.tm_wday == 0 || tm_buf.tm_wday == 6;
}

DateTime addTradingDays(DateTime dt, int n) {
    if (n <= 0) return dt;
    int added = 0;
    auto current = dt;
    while (added < n) {
        current += hours(24);
        if (!isWeekend(current)) {
            added++;
        }
    }
    return current;
}

int tradingDaysBetween(DateTime from, DateTime to) {
    int count = 0;
    auto current = from;
    while (current < to) {
        if (!isWeekend(current)) count++;
        current += hours(24);
    }
    return count;
}

} // namespace st::utils
