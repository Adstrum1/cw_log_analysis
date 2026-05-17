#include "ReportGenerator.h"
#include <curl/curl.h>
#include <cairo/cairo.h>
#include <cairo/cairo-pdf.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <future>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <map>

using json = nlohmann::json;

ReportGenerator::ReportGenerator() = default;
ReportGenerator::~ReportGenerator() = default;

static size_t WriteCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    std::string* userp
) {
    userp->append(
        static_cast<char*>(contents),
        size * nmemb
    );

    return size * nmemb;
}

std::string ReportGenerator::generateLogsSummary(
    const std::vector<LogEntry>& entries
) {
    std::map<std::string, int> levelCounts;
    std::map<std::string, int> sourceCounts;

    int totalEntries = entries.size();

    for (const auto& entry : entries) {
        levelCounts[entry.level]++;
        sourceCounts[entry.source]++;
    }

    std::ostringstream oss;

    oss << "Log Report Summary\n";

    oss << "Total log entries: "
        << totalEntries
        << "\n\n";

    oss << "Level Distribution:\n";

    for (const auto& [level, count] : levelCounts) {

        int percentage =
            totalEntries > 0
            ? (count * 100 / totalEntries)
            : 0;

        oss << "  "
            << level
            << ": "
            << count
            << " ("
            << percentage
            << "%)\n";
    }

    oss << "\nTop Sources:\n";

    int count = 0;

    for (const auto& [source, cnt] : sourceCounts) {

        if (count++ >= 5)
            break;

        oss << "  "
            << source
            << ": "
            << cnt
            << " entries\n";
    }

    return oss.str();
}

std::string ReportGenerator::callOllamaAPI(
    const std::string& logsSummary
) {
    CURL* curl = curl_easy_init();

    if (!curl) {
        return "Failed to initialize CURL";
    }

    std::string readBuffer;

    json payload = {
        {"model", "llama3.2:1b"},
        {
            "prompt",
            "Analyze the following log summary and provide "
            "technical insights:\n\n" + logsSummary +
            "\n\nFocus on:\n"
            "- Error patterns\n"
            "- Critical issues\n"
            "- Security concerns\n"
            "- Recommendations"
        },
        {"stream", false}
    };

    std::string jsonPayload = payload.dump();

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "http://localhost:11434/api/generate"
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDS,
        jsonPayload.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        WriteCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &readBuffer
    );

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(
        headers,
        "Content-Type: application/json"
    );

    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {

        return std::string(
            "Ollama request failed: "
        ) + curl_easy_strerror(res);
    }

    try {

        auto parsed = json::parse(readBuffer);

        if (parsed.contains("response")) {

            return parsed["response"]
                .get<std::string>();
        }

    } catch (const std::exception& e) {

        return std::string(
            "JSON parse error: "
        ) + e.what();
    }

    return "No response from Ollama";
}

bool ReportGenerator::createPDFWithAnalysis(
    const std::string& outputPath,
    const std::vector<LogEntry>& entries,
    const std::string& analysis
) {
    constexpr double PAGE_WIDTH = 595;
    constexpr double PAGE_HEIGHT = 842;

    constexpr double LEFT_MARGIN = 50;
    constexpr double TOP_MARGIN = 50;
    constexpr double PAGE_BOTTOM = 780;

    constexpr double MAX_TEXT_WIDTH = 500;
    constexpr double LINE_HEIGHT = 18;

    cairo_surface_t* surface =
        cairo_pdf_surface_create(
            outputPath.c_str(),
            PAGE_WIDTH,
            PAGE_HEIGHT
        );

    if (!surface ||
        cairo_surface_status(surface)
            != CAIRO_STATUS_SUCCESS) {

        if (surface)
            cairo_surface_destroy(surface);

        return false;
    }

    cairo_t* cr = cairo_create(surface);

    auto setupPageStyle = [&]() {

        cairo_set_source_rgb(cr, 0, 0, 0);

        cairo_select_font_face(
            cr,
            "Sans",
            CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_NORMAL
        );

        cairo_set_font_size(cr, 12);
    };

    auto newPage = [&](double& y) {

        cairo_show_page(cr);

        setupPageStyle();

        y = TOP_MARGIN;
    };

    setupPageStyle();

    double y = TOP_MARGIN;

    cairo_set_font_size(cr, 24);

    cairo_move_to(cr, LEFT_MARGIN, y);

    cairo_show_text(
        cr,
        "Log Analysis Report"
    );

    y += 30;

    auto now = std::time(nullptr);

    auto tm = std::localtime(&now);

    std::ostringstream timeStr;

    timeStr << std::put_time(
        tm,
        "%Y-%m-%d %H:%M:%S"
    );

    cairo_set_font_size(cr, 11);

    std::string generated =
        "Generated: " + timeStr.str();

    cairo_move_to(cr, LEFT_MARGIN, y);

    cairo_show_text(
        cr,
        generated.c_str()
    );

    y += 40;

    cairo_set_font_size(cr, 18);

    cairo_move_to(cr, LEFT_MARGIN, y);

    cairo_show_text(
        cr,
        "AI Analysis"
    );

    y += 30;

    cairo_set_font_size(cr, 12);

    std::istringstream analysisStream(analysis);

    std::string originalLine;

    while (std::getline(analysisStream, originalLine)) {

        std::istringstream words(originalLine);

        std::string word;

        std::string currentLine;

        while (words >> word) {

            std::string testLine;

            if (currentLine.empty()) {
                testLine = word;
            } else {
                testLine = currentLine + " " + word;
            }

            cairo_text_extents_t extents;

            cairo_text_extents(
                cr,
                testLine.c_str(),
                &extents
            );

            if (extents.width > MAX_TEXT_WIDTH) {

                if (y > PAGE_BOTTOM) {
                    newPage(y);
                }

                cairo_move_to(
                    cr,
                    LEFT_MARGIN,
                    y
                );

                cairo_show_text(
                    cr,
                    currentLine.c_str()
                );

                y += LINE_HEIGHT;

                currentLine = word;

            } else {

                currentLine = testLine;
            }
        }

        if (!currentLine.empty()) {

            if (y > PAGE_BOTTOM) {
                newPage(y);
            }

            cairo_move_to(
                cr,
                LEFT_MARGIN,
                y
            );

            cairo_show_text(
                cr,
                currentLine.c_str()
            );

            y += LINE_HEIGHT;
        }

        y += 4;
    }

    y += 20;

    if (y > PAGE_BOTTOM) {
        newPage(y);
    }

    cairo_set_font_size(cr, 18);

    cairo_move_to(cr, LEFT_MARGIN, y);

    cairo_show_text(
        cr,
        "Log Statistics"
    );

    y += 30;

    cairo_set_font_size(cr, 12);

    std::map<std::string, int> levelCounts;

    for (const auto& entry : entries) {
        levelCounts[entry.level]++;
    }

    for (const auto& [level, count] : levelCounts) {

        if (y > PAGE_BOTTOM) {
            newPage(y);
        }

        std::string text =
            level +
            ": " +
            std::to_string(count) +
            " entries";

        cairo_move_to(
            cr,
            LEFT_MARGIN,
            y
        );

        cairo_show_text(
            cr,
            text.c_str()
        );

        y += LINE_HEIGHT;
    }

    y += 20;

    if (y > PAGE_BOTTOM) {
        newPage(y);
    }

    std::string totalText =
        "Total log entries: " +
        std::to_string(entries.size());

    cairo_move_to(
        cr,
        LEFT_MARGIN,
        y
    );

    cairo_show_text(
        cr,
        totalText.c_str()
    );

    cairo_destroy(cr);

    cairo_surface_finish(surface);

    cairo_surface_destroy(surface);

    return true;
}

void ReportGenerator::generatePDFReport(
    const std::vector<LogEntry>& entries,
    const std::string& outputPath,
    ReportCallback callback
) {
    std::thread(
        [this, entries, outputPath, callback]() {

            try {

                std::string summary =
                    generateLogsSummary(entries);

                auto futureAnalysis =
                    std::async(
                        std::launch::async,

                        [this, summary]() {

                            return callOllamaAPI(
                                summary
                            );
                        }
                    );

                std::string analysis;

                if (
                    futureAnalysis.wait_for(
                        std::chrono::seconds(30)
                    ) == std::future_status::timeout
                ) {

                    analysis =
                        "AI analysis timeout exceeded.";

                } else {

                    analysis =
                        futureAnalysis.get();
                }

                bool success =
                    createPDFWithAnalysis(
                        outputPath,
                        entries,
                        analysis
                    );

                if (callback) {

                    callback(
                        success,

                        success
                        ? "Report generated successfully: "
                            + outputPath
                        : "Failed to create PDF"
                    );
                }

            } catch (const std::exception& e) {

                if (callback) {

                    callback(
                        false,
                        std::string("Error: ")
                            + e.what()
                    );
                }
            }

        }
    ).detach();
}