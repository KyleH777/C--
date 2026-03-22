#include "data_processor.h"
#include "mapped_file.h"
#include "pipeline.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#include <httplib.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════
// In-memory result store (loaded once, queried many times)
// ═══════════════════════════════════════════════════════════════════════════

struct AppState {
    std::vector<std::unique_ptr<ProcessedResult>> results;
    std::unordered_map<std::string, const ProcessedResult*> by_symbol;
    uint64_t rows_parsed   = 0;
    uint64_t rows_consumed = 0;
    double   load_time_s   = 0.0;
    std::string source_file;
};

static std::string to_json(const ProcessedResult& r) {
    std::ostringstream os;
    os << "{\"symbol\":\"" << r.symbol << "\""
       << ",\"vwap\":" << r.vwap
       << ",\"turnover\":" << r.turnover
       << ",\"total_volume\":" << r.total_vol
       << ",\"tick_count\":" << r.tick_count
       << "}";
    return os.str();
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv-file> [--port PORT]\n"
                  << "\n"
                  << "Loads a tick-data CSV, processes it, and serves\n"
                  << "VWAP results over a REST API.\n"
                  << "\n"
                  << "Endpoints:\n"
                  << "  GET /health           Health check\n"
                  << "  GET /symbols          List all symbols\n"
                  << "  GET /vwap?symbol=AAPL VWAP for one symbol\n"
                  << "  GET /vwap/all         VWAP for all symbols\n"
                  << "  GET /stats            Processing statistics\n";
        return EXIT_FAILURE;
    }

    std::filesystem::path csv_path = argv[1];
    int port = 8080;

    for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--port" && i + 1 < argc)
            port = std::atoi(argv[++i]);
    }

    // ── Load and process CSV ───────────────────────────────────────────
    AppState state;
    state.source_file = csv_path.filename().string();

    std::cerr << "Loading " << csv_path << "...\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    MappedFile file(csv_path);
    DataProcessor processor;

    Pipeline::Config cfg;
    Pipeline pipeline(cfg);
    auto stats = pipeline.run(file, [&](TickRow&& row) {
        processor.process(std::move(row));
    });

    state.results      = processor.finalise();
    state.rows_parsed   = stats.rows_parsed;
    state.rows_consumed = stats.rows_consumed;

    auto t1 = std::chrono::high_resolution_clock::now();
    state.load_time_s = std::chrono::duration<double>(t1 - t0).count();

    // Build lookup index
    for (const auto& r : state.results)
        state.by_symbol[r->symbol] = r.get();

    std::cerr << "Loaded " << state.rows_consumed << " rows ("
              << state.results.size() << " symbols) in "
              << state.load_time_s << "s\n";

    // ── HTTP server ────────────────────────────────────────────────────
    httplib::Server svr;

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    svr.Get("/symbols", [&state](const httplib::Request&, httplib::Response& res) {
        std::ostringstream os;
        os << "[";
        bool first = true;
        for (const auto& r : state.results) {
            if (!first) os << ",";
            os << "\"" << r->symbol << "\"";
            first = false;
        }
        os << "]";
        res.set_content(os.str(), "application/json");
    });

    svr.Get("/vwap", [&state](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("symbol")) {
            res.status = 400;
            res.set_content("{\"error\":\"missing ?symbol= parameter\"}", "application/json");
            return;
        }
        auto symbol = req.get_param_value("symbol");
        auto it = state.by_symbol.find(symbol);
        if (it == state.by_symbol.end()) {
            res.status = 404;
            res.set_content("{\"error\":\"symbol not found\"}", "application/json");
            return;
        }
        res.set_content(to_json(*it->second), "application/json");
    });

    svr.Get("/vwap/all", [&state](const httplib::Request&, httplib::Response& res) {
        std::ostringstream os;
        os << "[";
        bool first = true;
        for (const auto& r : state.results) {
            if (!first) os << ",";
            os << to_json(*r);
            first = false;
        }
        os << "]";
        res.set_content(os.str(), "application/json");
    });

    svr.Get("/stats", [&state](const httplib::Request&, httplib::Response& res) {
        std::ostringstream os;
        os << "{\"source_file\":\"" << state.source_file << "\""
           << ",\"rows_parsed\":" << state.rows_parsed
           << ",\"rows_consumed\":" << state.rows_consumed
           << ",\"symbols\":" << state.results.size()
           << ",\"load_time_s\":" << state.load_time_s
           << "}";
        res.set_content(os.str(), "application/json");
    });

    std::cerr << "Listening on http://localhost:" << port << "\n";
    svr.listen("0.0.0.0", port);

    return EXIT_SUCCESS;
}
