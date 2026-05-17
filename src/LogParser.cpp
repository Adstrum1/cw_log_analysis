#include "LogParser.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

static std::string makeNowTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&t, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string LogParser::trim(const std::string& text) {
    auto left = text.find_first_not_of(" \t\r\n");
    if (left == std::string::npos) return {};
    auto right = text.find_last_not_of(" \t\r\n");
    return text.substr(left, right - left + 1);
}

std::vector<std::string> LogParser::splitCsv(const std::string& line) {
    std::vector<std::string> result;
    std::string token;
    bool inQuotes = false;

    for (char ch : line) {
        if (ch == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (ch == ',' && !inQuotes) {
            result.push_back(trim(token));
            token.clear();
            continue;
        }
        token.push_back(ch);
    }
    if (!token.empty()) {
        result.push_back(trim(token));
    }
    return result;
}

std::string LogParser::extractJsonField(const std::string& text, const std::string& field) {
    auto pos = text.find('"' + field + '"');
    if (pos == std::string::npos) return {};
    pos = text.find(':', pos);
    if (pos == std::string::npos) return {};
    pos++;
    while (pos < text.size() && std::isspace((unsigned char)text[pos])) {
        pos++;
    }
    if (pos >= text.size()) return {};
    if (text[pos] == '"') {
        pos++;
        auto end = text.find('"', pos);
        if (end == std::string::npos) return {};
        return text.substr(pos, end - pos);
    }
    auto end = text.find_first_of(",}\n", pos);
    if (end == std::string::npos) end = text.size();
    return trim(text.substr(pos, end - pos));
}

LogFormat LogParser::detectFormat(const std::string& path, const std::string& sample) {
    auto lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".json") == 0) return LogFormat::Json;
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".csv") == 0) return LogFormat::Csv;
    if ((lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".log") == 0) ||
        (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".txt") == 0)) {
        return LogFormat::Text;
    }

    std::string trimmed = trim(sample);
    if (!trimmed.empty() && trimmed.front() == '{' && trimmed.back() == '}') {
        return LogFormat::Json;
    }
    if (!trimmed.empty() && trimmed.find(',') != std::string::npos) {
        return LogFormat::Csv;
    }
    return LogFormat::Text;
}

bool LogParser::parseLine(const std::string& line, LogFormat format, const std::string& source, LogEntry& entry) {
    switch (format) {
        case LogFormat::Json: {
            entry.timestamp = extractJsonField(line, "timestamp");
            entry.level = extractJsonField(line, "level");
            entry.source = extractJsonField(line, "source");
            entry.message = extractJsonField(line, "message");
            if (entry.timestamp.empty()) entry.timestamp = makeNowTimestamp();
            if (entry.source.empty()) entry.source = source;
            if (entry.message.empty()) entry.message = trim(line);
            if (entry.level.empty()) entry.level = "INFO";
            return true;
        }
        case LogFormat::Csv: {
            auto fields = splitCsv(line);
            if (fields.empty()) return false;
            entry.timestamp = fields.size() > 0 ? fields[0] : makeNowTimestamp();
            entry.level = fields.size() > 1 ? fields[1] : "INFO";
            entry.source = fields.size() > 2 ? fields[2] : source;
            entry.message = fields.size() > 3 ? fields[3] : (fields.size() > 0 ? fields.back() : trim(line));
            if (entry.timestamp.empty()) entry.timestamp = makeNowTimestamp();
            if (entry.source.empty()) entry.source = source;
            if (entry.level.empty()) entry.level = "INFO";
            return true;
        }
        case LogFormat::Text:
        default:
            entry = parseTextLine(line, source);
            return !entry.message.empty();
    }
}

LogEntry LogParser::parseTextLine(const std::string& line, const std::string& source) {
    LogEntry entry;
    entry.timestamp = makeNowTimestamp();
    entry.source = source;
    entry.level = "INFO";
    std::string trimmed = trim(line);
    if (trimmed.empty()) {
        entry.message = {};
        return entry;
    }

    const std::vector<std::string> levels = {"ERROR", "WARN", "WARNING", "INFO", "DEBUG", "TRACE"};
    for (auto& level : levels) {
        auto pos = trimmed.find(level);
        if (pos != std::string::npos) {
            if (level == "WARN") {
                entry.level = "WARNING";
            } else {
                entry.level = level;
            }
            break;
        }
    }

    if (trimmed.size() >= 19 && std::isdigit((unsigned char)trimmed[0]) && trimmed[4] == '-' && trimmed[7] == '-' && trimmed[10] == ' ') {
        entry.timestamp = trimmed.substr(0, 19);
        entry.message = trim(trimmed.substr(19));
    } else {
        entry.message = trimmed;
    }

    return entry;
}
