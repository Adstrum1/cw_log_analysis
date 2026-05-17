#pragma once

#include <string>
#include <vector>
#include <functional>

#include "LogEntry.h"

class ReportGenerator {
public:
    ReportGenerator();
    ~ReportGenerator();

    using ReportCallback =
        std::function<void(bool success, const std::string& message)>;

    void generatePDFReport(
        const std::vector<LogEntry>& entries,
        const std::string& outputPath,
        ReportCallback callback
    );

private:
    std::string callOllamaAPI(const std::string& logsSummary);

    std::string generateLogsSummary(
        const std::vector<LogEntry>& entries
    );

    bool createPDFWithAnalysis(
        const std::string& outputPath,
        const std::vector<LogEntry>& entries,
        const std::string& analysis
    );
};