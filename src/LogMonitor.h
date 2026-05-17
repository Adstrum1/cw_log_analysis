#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "LogEntry.h"
#include "LogParser.h"

struct LogSource {
    std::string path;
    std::string name;
    LogFormat format = LogFormat::Auto;
    bool isDirectory = false;
    int watchDescriptor = -1;
    off_t offset = 0;
    bool initialScanPending = true;
    std::map<std::string, off_t> childOffsets;
};

class LogMonitor {
public:
    LogMonitor();
    ~LogMonitor();

    bool addSource(const std::string& path, LogFormat format = LogFormat::Auto);
    void start();
    void stop();
    void refreshAllSources();
    void resetAllSources();
    void resetAllSourcesPartial();
    bool removeSource(const std::string& path);
    void setEntryCallback(std::function<void(LogEntry)> callback);
    void setStatusCallback(std::function<void(const std::string&)> callback);

private:
    void threadLoop();
    void scanSource(LogSource& source);
    void scanDirectory(LogSource& source);
    void scanFile(LogSource& source, const std::string& filePath, LogFormat explicitFormat);
    void sendEntry(LogEntry&& entry);

    int m_inotifyFd;
    std::vector<std::shared_ptr<LogSource>> m_sources;
    std::thread m_thread;
    std::mutex m_mutex;
    std::atomic<bool> m_running;
    std::function<void(LogEntry)> m_callback;
    std::function<void(const std::string&)> m_statusCallback;
};
