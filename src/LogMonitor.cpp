#include "LogMonitor.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <deque>

static constexpr int INOTIFY_BUFFER_SIZE = 4096;
static constexpr size_t MAX_INITIAL_LINES = 1000;

static std::vector<std::string> readFirstLines(std::ifstream& file, size_t maxLines) {
    std::vector<std::string> lines;
    file.seekg(0, std::ios::beg);
    std::string line;
    while (lines.size() < maxLines && std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

LogMonitor::LogMonitor()
    : m_inotifyFd(inotify_init1(IN_NONBLOCK)), m_running(false) {
}

LogMonitor::~LogMonitor() {
    stop();
    if (m_inotifyFd >= 0) {
        close(m_inotifyFd);
        m_inotifyFd = -1;
    }
}

void LogMonitor::setEntryCallback(std::function<void(LogEntry)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = std::move(callback);
}

void LogMonitor::setStatusCallback(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_statusCallback = std::move(callback);
}

bool LogMonitor::addSource(const std::string& path, LogFormat format) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    bool isDir = S_ISDIR(st.st_mode);
    int wd = inotify_add_watch(m_inotifyFd, path.c_str(), IN_MODIFY | IN_CREATE | IN_MOVED_TO);
    if (wd < 0) {
        return false;
    }

    auto source = std::make_shared<LogSource>();
    source->path = path;
    source->name = path;
    source->format = format;
    source->isDirectory = isDir;
    source->watchDescriptor = wd;
    source->offset = 0;

    if (!isDir && source->format == LogFormat::Auto) {
        source->format = LogParser::detectFormat(path, "");
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sources.push_back(source);
    }

    if (!m_running) {
        start();
    }

    return true;
}

void LogMonitor::start() {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) {
        return;
    }
    m_thread = std::thread(&LogMonitor::threadLoop, this);
}

void LogMonitor::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void LogMonitor::refreshAllSources() {
    if (!m_running) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& source : m_sources) {
        // Trigger a rescan of each source
        source->initialScanPending = true;
    }
}

bool LogMonitor::removeSource(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_sources.begin(); it != m_sources.end(); ++it) {
        if ((*it)->path == path) {
            if ((*it)->watchDescriptor >= 0) {
                inotify_rm_watch(m_inotifyFd, (*it)->watchDescriptor);
            }
            m_sources.erase(it);
            return true;
        }
    }
    return false;
}

void LogMonitor::resetAllSources() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& source : m_sources) {
        source->offset = 0;
        source->initialScanPending = true;
        source->childOffsets.clear();
    }
}

void LogMonitor::resetAllSourcesPartial() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& source : m_sources) {
        // Set offset to read last 1KB
        std::ifstream file(source->path, std::ios::binary);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            auto size = static_cast<off_t>(file.tellg());
            source->offset = (size > 1000) ? size - 1000 : 0;
        } else {
            source->offset = 0;
        }
        source->initialScanPending = true;
        source->childOffsets.clear();
    }
}

void LogMonitor::threadLoop() {
    while (m_running) {
        std::vector<std::shared_ptr<LogSource>> pendingSources;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& source : m_sources) {
                if (source->initialScanPending) {
                    source->initialScanPending = false;
                    pendingSources.push_back(source);
                }
            }
        }

        for (auto& source : pendingSources) {
            if (source && m_running) {
                scanSource(*source);
            }
        }

        // Use poll with timeout to avoid blocking forever
        struct pollfd fds[1];
        fds[0].fd = m_inotifyFd;
        fds[0].events = POLLIN;
        
        int pollResult = poll(fds, 1, 100); 
        
        if (!m_running) break;
        
        if (pollResult <= 0) {
            continue;
        }

        char buffer[INOTIFY_BUFFER_SIZE];
        ssize_t length = read(m_inotifyFd, buffer, sizeof(buffer));
        if (length <= 0) {
            continue;
        }

        if (!m_running) break;

        ssize_t index = 0;
        while (index < length && m_running) {
            auto* event = reinterpret_cast<struct inotify_event*>(buffer + index);
            std::shared_ptr<LogSource> sourceToScan;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& source : m_sources) {
                    if (source->watchDescriptor == event->wd) {
                        sourceToScan = source;
                        break;
                    }
                }
            }
            if (sourceToScan) {
                scanSource(*sourceToScan);
            }
            index += sizeof(struct inotify_event) + event->len;
        }
    }
}

void LogMonitor::scanSource(LogSource& source) {
    if (source.isDirectory) {
        scanDirectory(source);
        return;
    }

    scanFile(source, source.path, source.format);
}

void LogMonitor::scanDirectory(LogSource& source) {
    DIR* dir = opendir(source.path.c_str());
    if (!dir) {
        return;
    }

    while (auto* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        std::string itemPath = source.path + "/" + entry->d_name;
        struct stat st;
        if (stat(itemPath.c_str(), &st) != 0) {
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            continue;
        }

        LogFormat explicitFormat = source.format;
        if (explicitFormat == LogFormat::Auto) {
            explicitFormat = LogParser::detectFormat(itemPath, "");
        }

        scanFile(source, itemPath, explicitFormat);
    }
    closedir(dir);
}

void LogMonitor::scanFile(LogSource& source, const std::string& filePath, LogFormat explicitFormat) {
    off_t& offset = source.isDirectory ? source.childOffsets[filePath] : source.offset;
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    file.seekg(0, std::ios::end);
    auto currentSize = static_cast<off_t>(file.tellg());
    if (currentSize < offset) {
        offset = 0;
    }

    bool readFirstLinesMode = offset == 0 && currentSize > 0;
    if (readFirstLinesMode) {
        auto lines = ::readFirstLines(file, MAX_INITIAL_LINES);
        for (auto& line : lines) {
            LogEntry entry;
            if (LogParser::parseLine(line, explicitFormat, filePath, entry)) {
                sendEntry(std::move(entry));
            }
        }
        offset = currentSize;
        return;
    }

    file.seekg(offset, std::ios::beg);
    std::string line;
    while (std::getline(file, line)) {
        LogEntry entry;
        if (LogParser::parseLine(line, explicitFormat, filePath, entry)) {
            sendEntry(std::move(entry));
        }
    }

    offset = static_cast<off_t>(file.tellg());
}

void LogMonitor::sendEntry(LogEntry&& entry) {
    std::function<void(LogEntry)> callback;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callback = m_callback;
    }
    if (callback) {
        callback(std::move(entry));
    }
}
