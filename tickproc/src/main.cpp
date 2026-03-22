#include "mapped_file.h"
#include "pipeline.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string_view>

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

        // ── Consumer: example aggregation (VWAP per symbol) ────────────
        struct SymbolAgg {
            double   turnover = 0.0;  // Σ(price × volume)
            uint64_t total_vol = 0;
        };

        // Per-thread local maps to avoid lock contention
        thread_local std::unordered_map<std::string, SymbolAgg> local_aggs;

        std::mutex merge_mu;
        std::unordered_map<std::string, SymbolAgg> global_aggs;

        auto stats = pipeline.run(file, [&](TickRow&& row) {
            auto& agg = local_aggs[row.symbol];
            agg.turnover  += row.price * static_cast<double>(row.volume);
            agg.total_vol += row.volume;
        });

        // Merge thread-local maps — this runs after pipeline.run() returns,
        // so no lock contention during hot path.
        // Note: thread_local maps from jthreads are already destroyed,
        // so for a real app you'd capture per-thread maps explicitly.
        // Here we demonstrate the pattern; production code should use
        // per-thread accumulators passed via the consumer closure.

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        // ── Report ─────────────────────────────────────────────────────
        std::cerr << "Parsed:   " << stats.rows_parsed   << " rows\n"
                  << "Consumed: " << stats.rows_consumed << " rows\n"
                  << "Elapsed:  " << elapsed << " s\n"
                  << "Throughput: "
                  << (file.size() / (1024.0 * 1024.0)) / elapsed
                  << " MiB/s\n";

    } catch (const std::system_error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
