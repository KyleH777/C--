#include "csv_parser.h"
#include "data_processor.h"
#include "mapped_file.h"
#include "pipeline.h"
#include "tick_row.h"
#include "worker_pool.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════
// Timing helpers
// ═══════════════════════════════════════════════════════════════════════════

struct BenchResult {
    std::string label;
    unsigned    threads;
    uint64_t    rows;
    double      elapsed_s;
    double      mib_per_s;
    double      mrows_per_s;
};

using Clock = std::chrono::high_resolution_clock;

template <typename Fn>
double time_it(Fn&& fn) {
    auto t0 = Clock::now();
    fn();
    auto t1 = Clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark: single-threaded baseline
// ═══════════════════════════════════════════════════════════════════════════

static BenchResult bench_single_threaded(const MappedFile& file, double file_mib) {
    uint64_t row_count = 0;
    double   turnover  = 0.0;
    uint64_t total_vol = 0;

    bool header_skipped = false;
    double elapsed = time_it([&] {
        CsvParser::for_each_row(file.span(), [&](const TickRowView& row) {
            if (!header_skipped) { header_skipped = true; return; }
            turnover  += row.price * static_cast<double>(row.volume);
            total_vol += row.volume;
            ++row_count;
        });
    });

    return {
        "Single-threaded (direct parse)",
        1,
        row_count,
        elapsed,
        file_mib / elapsed,
        static_cast<double>(row_count) / 1e6 / elapsed
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark: multi-threaded Pipeline (producer-consumer)
// ═══════════════════════════════════════════════════════════════════════════

static BenchResult bench_pipeline(const MappedFile& file, double file_mib,
                                  unsigned producers, unsigned consumers) {
    Pipeline::Config cfg;
    cfg.producer_threads = producers;
    cfg.consumer_threads = consumers;
    cfg.skip_header      = true;
    cfg.queue_capacity   = 32768;

    DataProcessor dp;

    Pipeline::Stats stats;
    double elapsed = time_it([&] {
        Pipeline pipeline(cfg);
        stats = pipeline.run(file, [&](TickRow&& row) {
            dp.process(std::move(row));
        });
    });

    unsigned total_threads = producers + consumers;
    return {
        std::to_string(producers) + "P/" + std::to_string(consumers)
            + "C Pipeline",
        total_threads,
        stats.rows_consumed.load(),
        elapsed,
        file_mib / elapsed,
        static_cast<double>(stats.rows_consumed.load()) / 1e6 / elapsed
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark: multi-threaded WorkerPool (parse + process)
// ═══════════════════════════════════════════════════════════════════════════

static BenchResult bench_worker_pool(const MappedFile& file, double file_mib,
                                     unsigned num_workers) {
    DataProcessor dp;

    // Parse all rows single-threaded into a vector, then feed them
    // through the WorkerPool.  This isolates the pool's throughput
    // from parsing speed.
    std::vector<TickRow> all_rows;
    all_rows.reserve(1'000'000);

    bool header_skipped = false;
    CsvParser::for_each_row(file.span(), [&](const TickRowView& v) {
        if (!header_skipped) { header_skipped = true; return; }
        all_rows.push_back(TickRow::from_view(v));
    });

    uint64_t row_count = all_rows.size();

    double elapsed = time_it([&] {
        WorkerPool<TickRow> pool(
            [&dp](TickRow row) { dp.process(std::move(row)); },
            num_workers, 32768);
        pool.start();
        for (auto& row : all_rows)
            pool.submit(row);
        pool.stop();
    });

    return {
        "WorkerPool (" + std::to_string(num_workers) + " threads)",
        num_workers,
        row_count,
        elapsed,
        file_mib / elapsed,
        static_cast<double>(row_count) / 1e6 / elapsed
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Table printer
// ═══════════════════════════════════════════════════════════════════════════

static void print_table(const std::vector<BenchResult>& results, double file_mib) {
    // Column widths
    constexpr int wLabel   = 34;
    constexpr int wThreads = 8;
    constexpr int wRows    = 14;
    constexpr int wTime    = 12;
    constexpr int wMiB     = 12;
    constexpr int wMRows   = 12;
    constexpr int wSpeedup = 9;

    int total_w = wLabel + wThreads + wRows + wTime + wMiB + wMRows + wSpeedup + 8;

    std::cout << "\n";
    std::cout << std::string(total_w, '=') << "\n";
    std::cout << "  TICKPROC BENCHMARK RESULTS"
              << "   |   File: " << std::fixed << std::setprecision(1)
              << file_mib << " MiB"
              << "   |   CPU threads: "
              << std::thread::hardware_concurrency() << "\n";
    std::cout << std::string(total_w, '=') << "\n";

    // Header
    std::cout << std::left
              << std::setw(wLabel)   << "  Benchmark"
              << std::right
              << std::setw(wThreads) << "Threads"
              << std::setw(wRows)    << "Rows"
              << std::setw(wTime)    << "Time (s)"
              << std::setw(wMiB)    << "MiB/s"
              << std::setw(wMRows)   << "MRows/s"
              << std::setw(wSpeedup) << "Speedup"
              << "\n";
    std::cout << std::string(total_w, '-') << "\n";

    double baseline = results.empty() ? 1.0 : results[0].elapsed_s;

    for (const auto& r : results) {
        double speedup = baseline / r.elapsed_s;

        std::cout << std::left
                  << "  " << std::setw(wLabel - 2) << r.label
                  << std::right
                  << std::setw(wThreads) << r.threads
                  << std::setw(wRows)    << r.rows
                  << std::setw(wTime)    << std::fixed << std::setprecision(3) << r.elapsed_s
                  << std::setw(wMiB)    << std::fixed << std::setprecision(1) << r.mib_per_s
                  << std::setw(wMRows)   << std::fixed << std::setprecision(2) << r.mrows_per_s
                  << std::setw(wSpeedup - 1) << std::fixed << std::setprecision(2) << speedup
                  << "x\n";
    }

    std::cout << std::string(total_w, '=') << "\n\n";
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv-file>\n"
                  << "\n"
                  << "Runs single-threaded vs multi-threaded benchmarks\n"
                  << "and prints a comparison table.\n";
        return EXIT_FAILURE;
    }

    std::filesystem::path csv_path = argv[1];

    try {
        MappedFile file(csv_path);
        double file_mib = static_cast<double>(file.size()) / (1024.0 * 1024.0);

        std::cerr << "Mapped " << std::fixed << std::setprecision(1)
                  << file_mib << " MiB  (" << csv_path.filename().string() << ")\n";
        std::cerr << "Hardware threads: "
                  << std::thread::hardware_concurrency() << "\n\n";

        std::vector<BenchResult> results;

        // ── 1. Single-threaded baseline ────────────────────────────────
        std::cerr << "[1/5] Running single-threaded baseline...\n";
        results.push_back(bench_single_threaded(file, file_mib));

        // ── 2. Pipeline: 2P/2C ─────────────────────────────────────────
        std::cerr << "[2/5] Running Pipeline (2P/2C)...\n";
        results.push_back(bench_pipeline(file, file_mib, 2, 2));

        // ── 3. Pipeline: auto (hw_concurrency) ─────────────────────────
        unsigned hw = std::max(2u, std::thread::hardware_concurrency());
        unsigned half = hw / 2;
        std::cerr << "[3/5] Running Pipeline (" << half << "P/"
                  << (hw - half) << "C)...\n";
        results.push_back(bench_pipeline(file, file_mib, half, hw - half));

        // ── 4. WorkerPool: 2 threads ───────────────────────────────────
        std::cerr << "[4/5] Running WorkerPool (2 threads)...\n";
        results.push_back(bench_worker_pool(file, file_mib, 2));

        // ── 5. WorkerPool: hw_concurrency threads ──────────────────────
        std::cerr << "[5/5] Running WorkerPool (" << hw << " threads)...\n";
        results.push_back(bench_worker_pool(file, file_mib, hw));

        // ── Print results table ────────────────────────────────────────
        print_table(results, file_mib);

    } catch (const std::system_error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
