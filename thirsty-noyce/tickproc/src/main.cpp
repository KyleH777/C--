#include "mapped_file.h"
#include "pipeline.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <csv-file> [--no-header]\n"
              << "\n"
              << "Processes a financial tick-data CSV with the format:\n"
              << "  timestamp,symbol,price,volume\n"
              << "\n"
              << "Options:\n"
              << "  --no-header   The file has no header row\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    std::filesystem::path csv_path = argv[1];
    bool skip_header = true;

    for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--no-header")
            skip_header = false;
    }

    try {
        auto t0 = std::chrono::high_resolution_clock::now();

        // ── Memory-map the file ────────────────────────────────────────
        MappedFile file(csv_path);
        std::cerr << "Mapped " << (file.size() / (1024.0 * 1024.0))
                  << " MiB\n";

        // ── Configure pipeline ─────────────────────────────────────────
        Pipeline::Config cfg;
        cfg.skip_header = skip_header;
        Pipeline pipeline(cfg);

        // ── Consumer: mutex-protected VWAP aggregation per symbol ──────
        struct SymbolAgg {
            double   turnover  = 0.0;
            uint64_t total_vol = 0;
        };

        std::mutex agg_mu;
        std::unordered_map<std::string, SymbolAgg> aggs;

        auto stats = pipeline.run(file, [&](TickRow&& row) {
            std::lock_guard<std::mutex> lock(agg_mu);
            auto& agg = aggs[row.symbol];
            agg.turnover  += row.price * static_cast<double>(row.volume);
            agg.total_vol += row.volume;
        });

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        // ── Report ─────────────────────────────────────────────────────
        std::cerr << "Symbols:  " << aggs.size()          << "\n"
                  << "Parsed:   " << stats.rows_parsed    << " rows\n"
                  << "Consumed: " << stats.rows_consumed  << " rows\n"
                  << "Elapsed:  " << elapsed              << " s\n"
                  << "Throughput: "
                  << (file.size() / (1024.0 * 1024.0)) / elapsed
                  << " MiB/s\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
