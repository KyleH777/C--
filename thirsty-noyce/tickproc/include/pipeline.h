#pragma once

#include "csv_parser.h"
#include "mapped_file.h"
#include "thread_safe_queue.h"
#include "tick_row.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <thread>
#include <vector>

/// Producer-Consumer pipeline for processing memory-mapped CSV data.
///
///   ┌──────────┐        ┌───────────────────┐        ┌──────────┐
///   │ Producer │──push──▶ ThreadSafeQueue<T> │──pop──▶│ Consumer │
///   │ (parse)  │        │   bounded, MPMC    │        │ (sink)   │
///   └──────────┘        └───────────────────┘        └──────────┘
///
/// Multiple producers parse chunks in parallel.
/// Multiple consumers drain the queue and run user-defined logic.
/// The queue is bounded so producers back-pressure when consumers lag.
class Pipeline {
public:
    /// Returned to the caller after the pipeline finishes.
    struct Stats {
        uint64_t rows_parsed   = 0;
        uint64_t rows_consumed = 0;
    };

    using ConsumerFn = std::function<void(TickRow&&)>;

    struct Config {
        unsigned    producer_threads = 0; // 0 = auto (hardware_concurrency / 2)
        unsigned    consumer_threads = 0; // 0 = auto
        std::size_t queue_capacity   = 16384;
        bool        skip_header      = true;
        Config() = default;
    };

    Pipeline() : Pipeline(Config{}) {}

    explicit Pipeline(Config cfg)
        : cfg_(cfg)
        , queue_(cfg.queue_capacity)
    {
        if (cfg_.producer_threads == 0 || cfg_.consumer_threads == 0) {
            unsigned hw = std::max(2u, std::thread::hardware_concurrency());
            if (cfg_.producer_threads == 0) cfg_.producer_threads = hw / 2;
            if (cfg_.consumer_threads == 0) cfg_.consumer_threads = hw - cfg_.producer_threads;
            // Ensure at least 1 of each
            cfg_.producer_threads = std::max(1u, cfg_.producer_threads);
            cfg_.consumer_threads = std::max(1u, cfg_.consumer_threads);
        }
    }

    /// Run the full pipeline synchronously.  Returns when all data is
    /// parsed and consumed.
    Stats run(const MappedFile& file, ConsumerFn consumer) {
        std::atomic<uint64_t> rows_parsed{0};
        std::atomic<uint64_t> rows_consumed{0};

        auto chunks = CsvParser::partition(file.span(), cfg_.producer_threads);

        // ── Launch consumers ───────────────────────────────────────────
        std::vector<std::thread> consumers;
        consumers.reserve(cfg_.consumer_threads);
        for (unsigned i = 0; i < cfg_.consumer_threads; ++i) {
            consumers.emplace_back([&] {
                while (auto row = queue_.pop()) {
                    consumer(std::move(*row));
                    rows_consumed.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        // ── Launch producers ───────────────────────────────────────────
        {
            std::vector<std::thread> producers;
            producers.reserve(chunks.size());

            for (std::size_t i = 0; i < chunks.size(); ++i) {
                producers.emplace_back([&, i] {
                    auto chunk = chunks[i];
                    bool first_chunk = (i == 0);

                    // Thread-local batch buffer to reduce queue lock contention
                    constexpr std::size_t kBatchSize = 256;
                    std::vector<TickRow> batch;
                    batch.reserve(kBatchSize);

                    bool header_skipped = false;
                    CsvParser::for_each_row(chunk, [&](const TickRowView& view) {
                        // Skip CSV header (only appears in the first chunk)
                        if (cfg_.skip_header && first_chunk && !header_skipped) {
                            header_skipped = true;
                            return;
                        }

                        batch.push_back(TickRow::from_view(view));
                        rows_parsed.fetch_add(1, std::memory_order_relaxed);

                        if (batch.size() >= kBatchSize) {
                            queue_.push_batch(batch);
                            batch.clear();
                        }
                    });

                    // Flush remaining
                    if (!batch.empty()) {
                        queue_.push_batch(batch);
                        batch.clear();
                    }
                });
            }
            // Join all producers before signalling shutdown
            for (auto& t : producers)
                t.join();
        }

        // All producers done — signal consumers to drain and exit
        queue_.shutdown();

        // Join all consumers
        for (auto& t : consumers)
            t.join();

        return Stats{rows_parsed.load(), rows_consumed.load()};
    }

private:
    Config                  cfg_;
    ThreadSafeQueue<TickRow> queue_;
};
