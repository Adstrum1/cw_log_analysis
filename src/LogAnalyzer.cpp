#include "LogAnalyzer.h"
#include <sstream>
#include <chrono>
#include <iomanip>

LogAnalyzer::LogAnalyzer() = default;

void LogAnalyzer::addEntry(const LogEntry& entry) {
    m_entries.push_back({entry, time(nullptr)});
    
    if (entry.level == "ERROR") {
        m_errorCounts[entry.message]++;
    } else if (entry.level == "WARNING") {
        m_warningCounts[entry.message]++;
    }
}

void LogAnalyzer::clearData() {
    m_entries.clear();
    m_errorCounts.clear();
    m_warningCounts.clear();
}

std::vector<LogEntry> LogAnalyzer::getAllEntries() const {
    std::vector<LogEntry> result;
    for (const auto& storedEntry : m_entries) {
        result.push_back(storedEntry.entry);
    }
    return result;
}

std::vector<ErrorFrequency> LogAnalyzer::getRepeatedErrors(int topN) const {
    std::vector<ErrorFrequency> result;
    
    for (const auto& [msg, count] : m_errorCounts) {
        if (count > 1) {  // Only include errors that repeat
            result.push_back({msg, count, "ERROR", ""});
        }
    }
    
    std::sort(result.begin(), result.end(),
              [](const ErrorFrequency& a, const ErrorFrequency& b) {
                  return a.count > b.count;
              });
    
    if (result.size() > static_cast<size_t>(topN)) {
        result.resize(topN);
    }
    
    return result;
}

std::vector<ErrorFrequency> LogAnalyzer::getRepeatedWarnings(int topN) const {
    std::vector<ErrorFrequency> result;
    
    for (const auto& [msg, count] : m_warningCounts) {
        if (count > 1) {  // Only include warnings that repeat
            result.push_back({msg, count, "WARNING", ""});
        }
    }
    
    std::sort(result.begin(), result.end(),
              [](const ErrorFrequency& a, const ErrorFrequency& b) {
                  return a.count > b.count;
              });
    
    if (result.size() > static_cast<size_t>(topN)) {
        result.resize(topN);
    }
    
    return result;
}

std::string LogAnalyzer::extractDate(const std::string& timeStr) const {
    // Expected format: "2024-05-10 14:30:45" or similar
    if (timeStr.length() >= 10) {
        return timeStr.substr(0, 10);  // YYYY-MM-DD
    }
    return "";
}

std::string LogAnalyzer::extractHour(const std::string& timeStr) const {
    // Expected format: "2024-05-10 14:30:45"
    if (timeStr.length() >= 13) {
        return timeStr.substr(0, 13);  // "2024-05-10 14"
    }
    return "";
}

std::string LogAnalyzer::getCurrentDate() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d");
    return oss.str();
}

std::string LogAnalyzer::getCurrentYearMonth() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m");
    return oss.str();
}

std::string LogAnalyzer::getWeekStart(int weekOffset) const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    time -= (weekOffset * 7 * 86400);  
    
    std::tm* tm = std::localtime(&time);

    int daysBack = (tm->tm_wday == 0) ? 6 : tm->tm_wday - 1;
    time -= (daysBack * 86400);
    
    std::ostringstream oss;
    tm = std::localtime(&time);
    oss << std::put_time(tm, "%Y-%m-%d");
    return oss.str();
}

PeriodStatistics LogAnalyzer::getDailyStatistics(const std::string& date) const {
    std::string targetDate = date.empty() ? getCurrentDate() : date;
    PeriodStatistics stats;
    
    for (const auto& entry : m_entries) {
        std::string entryDate = extractDate(entry.entry.timestamp);
        if (entryDate == targetDate) {
            if (entry.entry.level == "ERROR") stats.totalErrors++;
            else if (entry.entry.level == "WARNING") stats.totalWarnings++;
            else if (entry.entry.level == "INFO") stats.totalInfo++;
            else if (entry.entry.level == "DEBUG") stats.totalDebug++;
            else if (entry.entry.level == "TRACE") stats.totalTrace++;
        }
    }
    
    auto errors = getRepeatedErrors(5);
    stats.topErrors = errors;
    
    return stats;
}

PeriodStatistics LogAnalyzer::getWeeklyStatistics(int weekOffset) const {
    std::string weekStart = getWeekStart(weekOffset);
    PeriodStatistics stats;
    
    for (const auto& entry : m_entries) {
        std::string entryDate = extractDate(entry.entry.timestamp);
        // Check if entry is within this week
        if (entryDate >= weekStart) {
            auto weekEnd = std::chrono::system_clock::now() + std::chrono::hours(24 * 7);
            // Simplified: just count entries from week start
            if (entry.entry.level == "ERROR") stats.totalErrors++;
            else if (entry.entry.level == "WARNING") stats.totalWarnings++;
            else if (entry.entry.level == "INFO") stats.totalInfo++;
            else if (entry.entry.level == "DEBUG") stats.totalDebug++;
            else if (entry.entry.level == "TRACE") stats.totalTrace++;
        }
    }
    
    auto errors = getRepeatedErrors(10);
    stats.topErrors = errors;
    
    return stats;
}

PeriodStatistics LogAnalyzer::getMonthlyStatistics(const std::string& yearMonth) const {
    std::string targetMonth = yearMonth.empty() ? getCurrentYearMonth() : yearMonth;
    PeriodStatistics stats;
    
    for (const auto& entry : m_entries) {
        std::string entryMonth = extractDate(entry.entry.timestamp).substr(0, 7);
        if (entryMonth == targetMonth) {
            if (entry.entry.level == "ERROR") stats.totalErrors++;
            else if (entry.entry.level == "WARNING") stats.totalWarnings++;
            else if (entry.entry.level == "INFO") stats.totalInfo++;
            else if (entry.entry.level == "DEBUG") stats.totalDebug++;
            else if (entry.entry.level == "TRACE") stats.totalTrace++;
        }
    }
    
    auto errors = getRepeatedErrors(15);
    stats.topErrors = errors;
    
    return stats;
}

PeriodStatistics LogAnalyzer::getOverallStatistics() const {
    PeriodStatistics stats;
    
    for (const auto& entry : m_entries) {
        if (entry.entry.level == "ERROR") stats.totalErrors++;
        else if (entry.entry.level == "WARNING") stats.totalWarnings++;
        else if (entry.entry.level == "INFO") stats.totalInfo++;
        else if (entry.entry.level == "DEBUG") stats.totalDebug++;
        else if (entry.entry.level == "TRACE") stats.totalTrace++;
    }
    
    auto errors = getRepeatedErrors(20);
    stats.topErrors = errors;
    
    return stats;
}

std::map<std::string, int> LogAnalyzer::getErrorCountByHour() const {
    std::map<std::string, int> hourCounts;
    
    for (const auto& entry : m_entries) {
        if (entry.entry.level == "ERROR") {
            std::string hour = extractHour(entry.entry.timestamp);
            if (!hour.empty()) {
                hourCounts[hour]++;
            }
        }
    }
    
    return hourCounts;
}

std::map<std::string, int> LogAnalyzer::getErrorCountByDay() const {
    std::map<std::string, int> dayCounts;
    
    for (const auto& entry : m_entries) {
        if (entry.entry.level == "ERROR") {
            std::string day = extractDate(entry.entry.timestamp);
            if (!day.empty()) {
                dayCounts[day]++;
            }
        }
    }
    
    return dayCounts;
}

time_t LogAnalyzer::parseTimestamp(const std::string& timeStr) const {
    std::tm tm = {};
    std::istringstream iss(timeStr);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return std::mktime(&tm);
}
