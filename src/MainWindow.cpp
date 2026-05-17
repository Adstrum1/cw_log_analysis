#include "MainWindow.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>

static std::string trimString(const std::string& text) {
    const std::string whitespace = " \t\r\n";
    auto left = text.find_first_not_of(whitespace);
    if (left == std::string::npos) return {};
    auto right = text.find_last_not_of(whitespace);
    return text.substr(left, right - left + 1);
}

MainWindow::ModelColumns::ModelColumns() {
    add(timestamp);
    add(level);
    add(source);
    add(message);
}

MainWindow::MainWindow() {
    set_title("Log Collector / Log Analysis");
    set_default_size(1100, 640);
    set_margin(10);

    setupLayout();

    m_addSourceButton.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::onAddSource));
    m_refreshButton.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::onReloadSources));
    m_showSourcesButton.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::onShowSources));
    m_reportButton.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::onGenerateReport));
    m_statisticsButton.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::showStatistics));
    m_searchEntry.set_placeholder_text("Search by keyword, level, or source...");
    m_searchEntry.signal_search_changed().connect(sigc::mem_fun(*this, &MainWindow::onSearchChanged));

    signal_close_request().connect(sigc::mem_fun(*this, &MainWindow::onCloseRequest), false);

    m_monitor.setEntryCallback([this](LogEntry entry) {
        onNewLogEntry(std::move(entry));
    });
    m_monitor.setStatusCallback([this](const std::string& status) {
        Glib::signal_idle().connect([this, status]() {
            m_statusLabel.set_text(status);
            return false;
        });
    });

    loadUserSources();
    setupAutoRefreshTimer();
}

MainWindow::~MainWindow() {
    m_monitor.stop();
}

void MainWindow::setupLayout() {
    set_child(m_mainBox);

    m_toolbar.set_homogeneous(false);
    m_toolbar.append(m_addSourceButton);
    m_toolbar.append(m_refreshButton);
    m_toolbar.append(m_showSourcesButton);
    m_toolbar.append(m_reportButton);
    m_toolbar.append(m_statisticsButton);
    m_toolbar.append(m_searchEntry);
    m_toolbar.append(m_statusLabel);
    m_mainBox.append(m_toolbar);

    // Filter box with level checkboxes
    m_filterBox.set_homogeneous(false);
    m_filterBox.append(m_filterLabel);
    m_filterBox.append(m_checkError);
    m_filterBox.append(m_checkWarning);
    m_filterBox.append(m_checkInfo);
    m_filterBox.append(m_checkDebug);
    m_filterBox.append(m_checkTrace);
    
    m_checkError.set_active(true);
    m_checkWarning.set_active(true);
    m_checkInfo.set_active(true);
    m_checkDebug.set_active(true);
    m_checkTrace.set_active(true);
    
    m_checkError.signal_toggled().connect(sigc::mem_fun(*this, &MainWindow::onLevelFilterChanged));
    m_checkWarning.signal_toggled().connect(sigc::mem_fun(*this, &MainWindow::onLevelFilterChanged));
    m_checkInfo.signal_toggled().connect(sigc::mem_fun(*this, &MainWindow::onLevelFilterChanged));
    m_checkDebug.signal_toggled().connect(sigc::mem_fun(*this, &MainWindow::onLevelFilterChanged));
    m_checkTrace.signal_toggled().connect(sigc::mem_fun(*this, &MainWindow::onLevelFilterChanged));
    
    m_mainBox.append(m_filterBox);

    m_refListStore = Gtk::ListStore::create(m_columns);
    m_refFilter = Gtk::TreeModelFilter::create(m_refListStore);
    m_refFilter->set_visible_func(sigc::mem_fun(*this, &MainWindow::filterVisible));
    m_treeView.set_model(m_refFilter);

    // Add columns 
    m_treeView.append_column("Timestamp", m_columns.timestamp);
    m_treeView.append_column("Level", m_columns.level);
    m_treeView.append_column("Source", m_columns.source);
    m_treeView.append_column("Message", m_columns.message);
    m_treeView.get_column(3)->set_expand(true);

    m_scrolledWindow.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    m_scrolledWindow.set_child(m_treeView);
    m_scrolledWindow.set_expand(true);
    m_scrolledWindow.set_hexpand(true);
    m_scrolledWindow.set_vexpand(true);
    m_scrolledWindow.set_min_content_height(400);
    m_mainBox.append(m_scrolledWindow);

    show();
}

void MainWindow::onAddSource() {
    auto dialog = std::make_shared<Gtk::Dialog>("Add Log Source", *this);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Add", Gtk::ResponseType::OK);

    Gtk::Box* contentArea = dialog->get_content_area();
    contentArea->set_margin_top(6);
    contentArea->set_margin_bottom(6);
    contentArea->set_margin_start(6);
    contentArea->set_margin_end(6);

    auto pathEntry = std::make_shared<Gtk::Entry>();
    pathEntry->set_placeholder_text("Path to log file or directory");
    
    auto formatCombo = std::make_shared<Gtk::ComboBoxText>();
    formatCombo->append("Auto");
    formatCombo->append("TXT/LOG");
    formatCombo->append("JSON");
    formatCombo->append("CSV");
    formatCombo->set_active_id("Auto");

    Gtk::Box form{Gtk::Orientation::VERTICAL, 6};
    form.append(*pathEntry);
    form.append(*formatCombo);
    contentArea->append(form);

    dialog->set_modal(true);
    
    dialog->signal_response().connect([this, dialog, pathEntry, formatCombo](int response_id) {
        dialog->hide();
        
        if (response_id != Gtk::ResponseType::OK) {
            return;
        }

        std::string rawPath = pathEntry->get_text();
        std::string path = trimString(rawPath);
        if (path.empty()) {
            return;
        }

        std::filesystem::path absolutePath = std::filesystem::absolute(path);
        if (!absolutePath.empty()) {
            path = absolutePath.string();
        }

        LogFormat format = LogFormat::Auto;
        auto selected = formatCombo->get_active_text();
        if (selected == "TXT/LOG") format = LogFormat::Text;
        else if (selected == "JSON") format = LogFormat::Json;
        else if (selected == "CSV") format = LogFormat::Csv;

        if (!m_monitor.addSource(path, format)) {
            auto errorDialog = std::make_shared<Gtk::MessageDialog>(*this, "Unable to add source.\nPlease check the path and permissions.", false, Gtk::MessageType::ERROR, Gtk::ButtonsType::OK);
            errorDialog->set_modal(true);
            errorDialog->signal_response().connect([errorDialog](int) {
                errorDialog->hide();
            });
            errorDialog->show();
        } else {

            if (std::find(m_userSources.begin(), m_userSources.end(), path) == m_userSources.end()) {
                m_userSources.push_back(path);
                saveUserSources();  
            }
        }
    });

    dialog->show();
}

static std::filesystem::path getWorkspaceRoot() {
    std::filesystem::path sourceFilePath(__FILE__);
    return sourceFilePath.parent_path().parent_path();
}

static std::filesystem::path getUserSourcesFilePath() {
    return getWorkspaceRoot() / "user_sources.txt";
}

void MainWindow::loadUserSources() {
    std::ifstream configFile(getUserSourcesFilePath());
    if (!configFile.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(configFile, line)) {
        std::string trimmed = trimString(line);
        if (trimmed.empty()) {
            continue;
        }
        try {
            std::string absPath = std::filesystem::absolute(trimmed).string();
            if (std::filesystem::exists(absPath)) {
                if (std::find(m_userSources.begin(), m_userSources.end(), absPath) == m_userSources.end()) {
                    m_monitor.addSource(absPath, LogFormat::Auto);
                    m_userSources.push_back(absPath);
                }
            }
        } catch (const std::exception&) {

        }
    }
}

void MainWindow::saveUserSources() {
    std::ofstream configFile(getUserSourcesFilePath());
    if (!configFile.is_open()) {
        return;
    }

    for (const auto& source : m_userSources) {
        configFile << source << std::endl;
    }
}

std::string MainWindow::getUserSourcesFilePath() const {
    return ::getUserSourcesFilePath().string();
}

void MainWindow::onShowSources() {
    struct SourcesColumns : public Gtk::TreeModel::ColumnRecord {
        SourcesColumns() { add(path); }
        Gtk::TreeModelColumn<std::string> path;
    };

    auto columns = std::make_shared<SourcesColumns>();
    auto model = Gtk::ListStore::create(*columns);
    for (const auto& source : m_userSources) {
        auto row = *(model->append());
        row[columns->path] = source;
    }

    auto dialog = std::make_shared<Gtk::Dialog>("Manage Log Sources", *this);
    dialog->add_button("Close", Gtk::ResponseType::CLOSE);
    dialog->set_modal(true);
    dialog->set_default_size(800, 400);

    Gtk::Box* contentArea = dialog->get_content_area();
    contentArea->set_margin_top(6);
    contentArea->set_margin_bottom(6);
    contentArea->set_margin_start(6);
    contentArea->set_margin_end(6);

    auto* treeView = Gtk::make_managed<Gtk::TreeView>();
    treeView->set_model(model);
    treeView->append_column("Source Path", columns->path);
    treeView->set_headers_visible(true);
    treeView->set_vexpand(true);
    treeView->set_hexpand(true);
    contentArea->append(*treeView);

    auto* deleteButton = Gtk::make_managed<Gtk::Button>("Delete selected source");
    deleteButton->set_sensitive(false);
    auto* buttonBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    buttonBox->append(*deleteButton);
    contentArea->append(*buttonBox);

    auto selection = treeView->get_selection();
    selection->signal_changed().connect([deleteButton, selection]() {
        deleteButton->set_sensitive(static_cast<bool>(selection->get_selected()));
    });

    deleteButton->signal_clicked().connect([this, dialog, model, treeView, columns, selection]() {
        auto iter = selection->get_selected();
        if (!iter) return;

        auto row = *iter;
        std::string sourcePath = row[columns->path];
        if (sourcePath.empty()) return;

        if (m_monitor.removeSource(sourcePath)) {
            m_userSources.erase(std::remove(m_userSources.begin(), m_userSources.end(), sourcePath), m_userSources.end());
            saveUserSources();
            model->erase(iter);
            m_statusLabel.set_text("Removed source: " + sourcePath);
        } else {
            m_statusLabel.set_text("Failed to remove source: " + sourcePath);
        }
    });

    dialog->signal_response().connect([dialog](int response_id) {
        if (response_id == Gtk::ResponseType::CLOSE) {
            dialog->hide();
        }
    });

    dialog->show();
}

void MainWindow::onGenerateReport() {
    if (m_analyzer.getAllEntries().empty()) {
        m_statusLabel.set_text("No log entries to generate report");
        return;
    }

    m_statusLabel.set_text("Generating report... please wait");
    m_reportButton.set_sensitive(false);

    std::vector<LogEntry> entries = m_analyzer.getAllEntries();

    m_reportGenerator.generatePDFReport(
        entries,
        "log_report.pdf",
        [this](bool success, const std::string& message) {
            Glib::signal_idle().connect([this, success, message]() {
                m_statusLabel.set_text(message);
                m_reportButton.set_sensitive(true);
                return false;
            });
        }
    );
}

bool MainWindow::onAutoRefresh() {
    if (!m_running) return false;
    m_monitor.refreshAllSources();
    return true;  
}

void MainWindow::setupAutoRefreshTimer() {

    m_refreshConnection = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &MainWindow::onAutoRefresh),
        10000  
    );
}

bool MainWindow::onCloseRequest() {

    m_running = false;
    
    if (m_refreshConnection.connected()) {
        m_refreshConnection.disconnect();
    }
    
    if (m_idleConnection.connected()) {
        m_idleConnection.disconnect();
    }
    
    saveUserSources();
    
    m_monitor.stop();
    
    return false;
}

void MainWindow::onSearchChanged() {
    m_filterText = m_searchEntry.get_text();
    m_refFilter->refilter();
}

void MainWindow::onReloadSources() {
    m_statusLabel.set_text("Reloading log sources...");
    m_analyzer.clearData();
    m_refListStore->clear();
    m_monitor.resetAllSourcesPartial();
    m_monitor.refreshAllSources();

    Glib::signal_timeout().connect(
        [this]() {
            updateStatisticsView();
            m_statusLabel.set_text("Log sources reloaded.");
            return false;
        },
        500  
    );
}

void MainWindow::scheduleFlush() {

    if (!m_idleConnection.connected()) {
        m_idleConnection = Glib::signal_idle().connect(sigc::mem_fun(*this, &MainWindow::onIdleFlush));
    }
}

bool MainWindow::onIdleFlush() {
    std::vector<LogEntry> pending;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        pending.swap(m_pendingEntries);
    }
    for (auto& entry : pending) {
        m_analyzer.addEntry(entry);
        auto row = *(m_refListStore->append());
        row[m_columns.timestamp] = entry.timestamp;
        row[m_columns.level] = entry.level;
        row[m_columns.source] = entry.source;
        row[m_columns.message] = entry.message;
    }
    m_refFilter->refilter();
    return false;
}

bool MainWindow::filterVisible(const Gtk::TreeModel::const_iterator& iter) {
    if (!iter) return false;

    auto row = *iter;
    std::string timestamp = row[m_columns.timestamp];
    std::string level = row[m_columns.level];
    std::string source = row[m_columns.source];
    std::string message = row[m_columns.message];
    
    bool levelMatch = false;
    if (level.find("ERROR") != std::string::npos && m_filterError) levelMatch = true;
    if (level.find("WARN") != std::string::npos && m_filterWarning) levelMatch = true;
    if (level.find("INFO") != std::string::npos && m_filterInfo) levelMatch = true;
    if (level.find("DEBUG") != std::string::npos && m_filterDebug) levelMatch = true;
    if (level.find("TRACE") != std::string::npos && m_filterTrace) levelMatch = true;
    
    if (!levelMatch) return false;
    
    if (m_filterText.empty()) return true;

    std::string value = timestamp + " " + level + " " + source + " " + message;
    return value.find(m_filterText) != std::string::npos;
}

void MainWindow::onLevelFilterChanged() {
    m_filterError = m_checkError.get_active();
    m_filterWarning = m_checkWarning.get_active();
    m_filterInfo = m_checkInfo.get_active();
    m_filterDebug = m_checkDebug.get_active();
    m_filterTrace = m_checkTrace.get_active();
    
    m_refFilter->refilter();
}

void MainWindow::queueEntry(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingEntries.push_back(entry);
    scheduleFlush();
}

void MainWindow::onNewLogEntry(LogEntry entry) {
    queueEntry(entry);
}

void MainWindow::showStatistics() {
    if (m_statsWindow) {
        m_statsWindow->present();
        updateStatisticsView();
        return;
    }

    m_statsWindow = new Gtk::Window();
    m_statsWindow->set_title("Log Statistics");
    m_statsWindow->set_default_size(800, 600);
    m_statsWindow->set_transient_for(*this);

    m_statsNotebook = new Gtk::Notebook();
    m_statsWindow->set_child(*m_statsNotebook);

    // Repeated Errors tab
    auto* repeatedErrorsBox = new Gtk::Box(Gtk::Orientation::VERTICAL, 5);
    auto* repeatedErrorsScrolled = new Gtk::ScrolledWindow();
    auto* repeatedErrorsTreeView = new Gtk::TreeView();
    
    auto repeatedStore = Gtk::ListStore::create(m_repeatedColumns);
    repeatedErrorsTreeView->set_model(repeatedStore);
    repeatedErrorsTreeView->append_column("Message", m_repeatedColumns.message);
    repeatedErrorsTreeView->append_column("Count", m_repeatedColumns.count);
    repeatedErrorsTreeView->append_column("Level", m_repeatedColumns.level);
    
    repeatedErrorsScrolled->set_child(*repeatedErrorsTreeView);
    repeatedErrorsScrolled->set_vexpand(true);  // Allow vertical expansion
    repeatedErrorsBox->append(*repeatedErrorsScrolled);
    m_statsNotebook->append_page(*repeatedErrorsBox, "Repeated Errors");

    // Daily Statistics tab
    auto* dailyBox = new Gtk::Box(Gtk::Orientation::VERTICAL, 5);
    auto* dailyLabel = new Gtk::Label("Daily Statistics (current day)");
    dailyBox->append(*dailyLabel);
    auto* dailyDrawingArea = new Gtk::DrawingArea();
    dailyDrawingArea->set_content_width(400);
    dailyDrawingArea->set_content_height(200);
    dailyDrawingArea->set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
        this->drawDailyChart(cr, width, height);
    });
    dailyBox->append(*dailyDrawingArea);
    m_statsNotebook->append_page(*dailyBox, "Daily");

    // Weekly Statistics tab
    auto* weeklyBox = new Gtk::Box(Gtk::Orientation::VERTICAL, 5);
    auto* weeklyLabel = new Gtk::Label("Weekly Statistics (sum of last 7 days)");
    weeklyBox->append(*weeklyLabel);
    auto* weeklyDrawingArea = new Gtk::DrawingArea();
    weeklyDrawingArea->set_content_width(400);
    weeklyDrawingArea->set_content_height(200);
    weeklyDrawingArea->set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
        this->drawWeeklyChart(cr, width, height);
    });
    weeklyBox->append(*weeklyDrawingArea);
    m_statsNotebook->append_page(*weeklyBox, "Weekly");

    // Monthly Statistics tab
    auto* monthlyBox = new Gtk::Box(Gtk::Orientation::VERTICAL, 5);
    auto* monthlyLabel = new Gtk::Label("Monthly Statistics (sum of current month)");
    monthlyBox->append(*monthlyLabel);
    auto* monthlyDrawingArea = new Gtk::DrawingArea();
    monthlyDrawingArea->set_content_width(400);
    monthlyDrawingArea->set_content_height(200);
    monthlyDrawingArea->set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
        this->drawMonthlyChart(cr, width, height);
    });
    monthlyBox->append(*monthlyDrawingArea);
    m_statsNotebook->append_page(*monthlyBox, "Monthly");

    m_statsWindow->signal_close_request().connect([this]() {
        delete m_statsWindow;
        m_statsWindow = nullptr;
        m_statsNotebook = nullptr;
        return true;
    }, false);

    m_statsWindow->show();
    updateStatisticsView();
}

void MainWindow::updateStatisticsView() {
    if (!m_statsWindow || !m_statsNotebook) return;

    // Update Repeated Errors
    auto* page0 = m_statsNotebook->get_nth_page(0);
    if (page0) {
        auto* box = dynamic_cast<Gtk::Box*>(page0);
        if (box) {
            auto* scrolled = dynamic_cast<Gtk::ScrolledWindow*>(box->get_first_child());
            if (scrolled) {
                auto* treeView = dynamic_cast<Gtk::TreeView*>(scrolled->get_child());
                if (treeView) {
                    auto store = std::dynamic_pointer_cast<Gtk::ListStore>(treeView->get_model());
                    store->clear();
                    
                    auto repeatedErrors = m_analyzer.getRepeatedErrors(20);
                    for (const auto& err : repeatedErrors) {
                        auto row = *store->append();
                        row[m_repeatedColumns.message] = err.message;
                        row[m_repeatedColumns.count] = err.count;
                        row[m_repeatedColumns.level] = err.level;
                    }
                }
            }
        }
    }

    // Update Daily Stats
    auto* page1 = m_statsNotebook->get_nth_page(1);
    if (page1) {
        auto* box = dynamic_cast<Gtk::Box*>(page1);
        if (box) {
            auto* label = dynamic_cast<Gtk::Label*>(box->get_first_child());
            if (label) {
                auto stats = m_analyzer.getDailyStatistics();
                std::ostringstream oss;
                oss << "Daily Statistics:\n";
                oss << "Errors: " << stats.totalErrors << "\n";
                oss << "Warnings: " << stats.totalWarnings << "\n";
                oss << "Info: " << stats.totalInfo << "\n";
                label->set_text(oss.str());
            }
        }
    }

    // Update Weekly and Monthly
    auto* page2 = m_statsNotebook->get_nth_page(2);
    if (page2) {
        auto* box = dynamic_cast<Gtk::Box*>(page2);
        if (box) {
            auto* label = dynamic_cast<Gtk::Label*>(box->get_first_child());
            if (label) {
                auto stats = m_analyzer.getWeeklyStatistics();
                std::ostringstream oss;
                oss << "Weekly Statistics:\n";
                oss << "Errors: " << stats.totalErrors << "\n";
                oss << "Warnings: " << stats.totalWarnings << "\n";
                oss << "Info: " << stats.totalInfo << "\n";
                label->set_text(oss.str());
            }
        }
    }

    auto* page3 = m_statsNotebook->get_nth_page(3);
    if (page3) {
        auto* box = dynamic_cast<Gtk::Box*>(page3);
        if (box) {
            auto* label = dynamic_cast<Gtk::Label*>(box->get_first_child());
            if (label) {
                auto stats = m_analyzer.getMonthlyStatistics();
                std::ostringstream oss;
                oss << "Monthly Statistics:\n";
                oss << "Errors: " << stats.totalErrors << "\n";
                oss << "Warnings: " << stats.totalWarnings << "\n";
                oss << "Info: " << stats.totalInfo << "\n";
                label->set_text(oss.str());
            }
        }
    }
}

void MainWindow::drawDailyChart(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    auto stats = m_analyzer.getDailyStatistics();
    drawBarChart(cr, width, height, stats.totalErrors, stats.totalWarnings, stats.totalInfo);
}

void MainWindow::drawWeeklyChart(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    auto stats = m_analyzer.getWeeklyStatistics();
    drawBarChart(cr, width, height, stats.totalErrors, stats.totalWarnings, stats.totalInfo);
}

void MainWindow::drawMonthlyChart(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    auto stats = m_analyzer.getMonthlyStatistics();
    drawBarChart(cr, width, height, stats.totalErrors, stats.totalWarnings, stats.totalInfo);
}

void MainWindow::drawBarChart(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height, int errors, int warnings, int info) {

    cr->set_source_rgb(1, 1, 1);
    cr->paint();

    cr->set_source_rgb(0, 0, 0);
    cr->set_line_width(2);
    cr->move_to(50, 20);
    cr->line_to(50, height - 50);
    cr->line_to(width - 20, height - 50);
    cr->stroke();

    double barWidth = (width - 100) / 9.0;  
    double maxVal = std::max({errors, warnings, info, 1});
    double scale = (height - 80) / maxVal;

    // Errors bar (red)
    cr->set_source_rgb(1, 0, 0);
    double eHeight = std::max(errors * scale, 1.0);  
    cr->rectangle(60, height - 50 - eHeight, barWidth, eHeight);
    cr->fill();
    cr->set_source_rgb(0, 0, 0);
    cr->set_line_width(1);
    cr->rectangle(60, height - 50 - eHeight, barWidth, eHeight);
    cr->stroke();

    // Warnings bar (yellow)
    cr->set_source_rgb(1, 1, 0);
    double wHeight = std::max(warnings * scale, 1.0);
    cr->rectangle(60 + barWidth * 3, height - 50 - wHeight, barWidth, wHeight);
    cr->fill();
    cr->set_source_rgb(0, 0, 0);
    cr->rectangle(60 + barWidth * 3, height - 50 - wHeight, barWidth, wHeight);
    cr->stroke();

    // Info bar (blue)
    cr->set_source_rgb(0, 0, 1);
    double iHeight = std::max(info * scale, 1.0);
    cr->rectangle(60 + barWidth * 6, height - 50 - iHeight, barWidth, iHeight);
    cr->fill();
    cr->set_source_rgb(0, 0, 0);
    cr->rectangle(60 + barWidth * 6, height - 50 - iHeight, barWidth, iHeight);
    cr->stroke();

    // Labels and values
    cr->set_source_rgb(0, 0, 0);
    cr->set_font_size(12);

    // Errors
    cr->move_to(60 + barWidth / 2 - 10, height - 30);
    cr->show_text("Errors");
    cr->move_to(60 + barWidth / 2 - 5, height - 50 - eHeight - 5);
    cr->show_text(std::to_string(errors));

    // Warnings
    cr->move_to(60 + barWidth * 3 + barWidth / 2 - 15, height - 30);
    cr->show_text("Warnings");
    cr->move_to(60 + barWidth * 3 + barWidth / 2 - 5, height - 50 - wHeight - 5);
    cr->show_text(std::to_string(warnings));

    // Info
    cr->move_to(60 + barWidth * 6 + barWidth / 2 - 10, height - 30);
    cr->show_text("Info");
    cr->move_to(60 + barWidth * 6 + barWidth / 2 - 5, height - 50 - iHeight - 5);
    cr->show_text(std::to_string(info));
}
