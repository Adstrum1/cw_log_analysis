#pragma once

#include <string>
#include <vector>
#include "LogEntry.h"

enum class LogFormat {
    Auto,
    Text,
    Json,
    Csv,
};

class LogParser {
public:
    static LogFormat detectFormat(const std::string& path, const std::string& sample);
    static bool parseLine(const std::string& line, LogFormat format, const std::string& source, LogEntry& entry);

private:
    static std::string trim(const std::string& text);
    static std::vector<std::string> splitCsv(const std::string& line);
    static std::string extractJsonField(const std::string& text, const std::string& field);
    static LogEntry parseTextLine(const std::string& line, const std::string& source);
};
