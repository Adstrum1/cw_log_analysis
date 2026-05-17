#pragma once

#include "LogEntry.h"
#include <map>
#include <vector>
#include <string>
#include <ctime>
#include <algorithm>

struct ErrorFrequency {
    std::string message;
    int count = 0;
    std::string level;
    std::string source;
};

struct PeriodStatistics {
    int totalErrors = 0;
    int totalWarnings = 0;
    int totalInfo = 0;
    int totalDebug = 0;
    int totalTrace = 0;
    std::vector<ErrorFrequency> topErrors;
    std::vector<ErrorFrequency> topWarnings;
};

class LogAnalyzer {
public:
    LogAnalyzer();
    
    void addEntry(const LogEntry& entry);
    void clearData();
    
    std::vector<LogEntry> getAllEntries() const;
    
    // Error detection
    std::vector<ErrorFrequency> getRepeatedErrors(int topN = 10) const;
    std::vector<ErrorFrequency> getRepeatedWarnings(int topN = 10) const;
    
    // Statistics by period
    PeriodStatistics getDailyStatistics(const std::string& date = "") const;
    PeriodStatistics getWeeklyStatistics(int weekOffset = 0) const;
    PeriodStatistics getMonthlyStatistics(const std::string& yearMonth = "") const;
    
    // Overall statistics
    PeriodStatistics getOverallStatistics() const;
    
    // Timeline data for charts
    std::map<std::string, int> getErrorCountByHour() const;
    std::map<std::string, int> getErrorCountByDay() const;

private:
    struct StoredEntry {
        LogEntry entry;
        time_t timestamp;
    };
    
    std::vector<StoredEntry> m_entries;
    std::map<std::string, int> m_errorCounts; 
    std::map<std::string, int> m_warningCounts;  
    
    time_t parseTimestamp(const std::string& timeStr) const;
    std::string extractDate(const std::string& timeStr) const;
    std::string extractHour(const std::string& timeStr) const;
    std::string getCurrentDate() const;
    std::string getWeekStart(int weekOffset) const;
    std::string getCurrentYearMonth() const;
};
