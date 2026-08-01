#pragma once

#include "foundation/types.h"
#include <string>
#include <vector>

namespace st::utils {

/// Convert DateTime to "YYYY-MM-DD" string
[[nodiscard]] std::string toDateString(DateTime dt);

/// Convert DateTime to "YYYY-MM-DD HH:MM:SS" string
[[nodiscard]] std::string toDateTimeString(DateTime dt);

/// Parse "YYYY-MM-DD" or "YYYYMMDD" string to DateTime
[[nodiscard]] DateTime parseDate(const std::string& s);

/// Parse "YYYY-MM-DD HH:MM:SS" to DateTime
[[nodiscard]] DateTime parseDateTime(const std::string& s);

/// Current date (00:00:00)
[[nodiscard]] DateTime today();

/// Current DateTime
[[nodiscard]] DateTime now();

/// Add N trading days (approximate: skips weekends for A-share)
[[nodiscard]] DateTime addTradingDays(DateTime dt, int n);

/// Number of trading days between two dates (approximate)
[[nodiscard]] int tradingDaysBetween(DateTime from, DateTime to);

/// Check if a date is a weekend (simple check, no holiday calendar)
[[nodiscard]] bool isWeekend(DateTime dt);

} // namespace st::utils
