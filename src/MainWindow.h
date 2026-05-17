#pragma once

#include <gtkmm.h>
#include <mutex>
#include <vector>
#include "LogEntry.h"
#include "LogMonitor.h"
#include "LogAnalyzer.h"
#include "ReportGenerator.h"

class MainWindow : public Gtk::ApplicationWindow {
public:
    MainWindow();
    ~MainWindow() override;

private:
    class ModelColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        ModelColumns();
        Gtk::TreeModelColumn<std::string> timestamp;
        Gtk::TreeModelColumn<std::string> level;
        Gtk::TreeModelColumn<std::string> source;
        Gtk::TreeModelColumn<std::string> message;
    } m_columns;

    class RepeatedColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        RepeatedColumns() {
            add(message);
            add(count);
            add(level);
        }
        Gtk::TreeModelColumn<std::string> message;
        Gtk::TreeModelColumn<int> count;
        Gtk::TreeModelColumn<std::string> level;
    } m_repeatedColumns;

    void setupLayout();
    void loadUserSources();
    void saveUserSources();
    std::string getUserSourcesFilePath() const;
    void onAddSource();
    void onReloadSources();
    void onShowSources();
    void onGenerateReport();
    void onSearchChanged();
    void onLevelFilterChanged();
    void updateStatusLabel();
    bool filterVisible(const Gtk::TreeModel::const_iterator& iter);
    void queueEntry(const LogEntry& entry);
    void flushPendingEntries();
    void onNewLogEntry(LogEntry entry);
    bool onCloseRequest();
    bool onAutoRefresh();
    void setupAutoRefreshTimer();
    void showStatistics();
    void updateStatisticsView();
    void drawDailyChart(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void drawWeeklyChart(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void drawMonthlyChart(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void drawBarChart(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height, int errors, int warnings, int info);

    Gtk::Box m_mainBox{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box m_toolbar{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Box m_filterBox{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Button m_addSourceButton{"Add source"};
    Gtk::Button m_refreshButton{"Reload logs"};
    Gtk::Button m_showSourcesButton{"Show sources"};
    Gtk::Button m_reportButton{"Generate Report"};
    Gtk::Button m_statisticsButton{"Statistics"};
    Gtk::SearchEntry m_searchEntry;
    Gtk::Label m_statusLabel{"Ready."};
    Gtk::Label m_filterLabel{"Filter by level:"};
    
    // Level filter checkboxes
    Gtk::CheckButton m_checkError{"Error"};
    Gtk::CheckButton m_checkWarning{"Warning"};
    Gtk::CheckButton m_checkInfo{"Info"};
    Gtk::CheckButton m_checkDebug{"Debug"};
    Gtk::CheckButton m_checkTrace{"Trace"};
    
    Gtk::ScrolledWindow m_scrolledWindow;
    Gtk::TreeView m_treeView;
    Glib::RefPtr<Gtk::ListStore> m_refListStore;
    Glib::RefPtr<Gtk::TreeModelFilter> m_refFilter;

    LogMonitor m_monitor;
    LogAnalyzer m_analyzer;
    ReportGenerator m_reportGenerator;
    sigc::connection m_idleConnection;
    sigc::connection m_refreshConnection;
    std::mutex m_pendingMutex;
    std::vector<LogEntry> m_pendingEntries;
    std::string m_filterText;
    std::vector<std::string> m_userSources;
    bool m_running = true;
    bool m_filterError = true;
    bool m_filterWarning = true;
    bool m_filterInfo = true;
    bool m_filterDebug = true;
    bool m_filterTrace = true;
    
    // Statistics UI
    Gtk::Window* m_statsWindow = nullptr;
    Gtk::Notebook* m_statsNotebook = nullptr;
    
    bool onIdleFlush();
    void scheduleFlush();
};
