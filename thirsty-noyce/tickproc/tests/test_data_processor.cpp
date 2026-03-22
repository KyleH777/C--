#include "data_processor.h"
#include "worker_pool.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

/// Helper: build a TickRow from raw values.
static TickRow make_tick(const char* symbol, double price, uint64_t volume) {
    TickRow row{};
    std::strncpy(row.symbol, symbol, sizeof(row.symbol) - 1);
    std::strncpy(row.timestamp, "2024-01-01T00:00:00", sizeof(row.timestamp) - 1);
    row.price  = price;
    row.volume = volume;
    return row;
}

static void test_single_threaded_aggregation() {
    DataProcessor dp;

    dp.process(make_tick("AAPL", 100.0, 10));
    dp.process(make_tick("AAPL", 200.0, 20));
    dp.process(make_tick("GOOG", 50.0,  5));

    auto results = dp.finalise();
    assert(results.size() == 2);

    // Find AAPL
    auto it = std::find_if(results.begin(), results.end(),
        [](const auto& r) { return r->symbol == "AAPL"; });
    assert(it != results.end());

    // VWAP = (100*10 + 200*20) / (10+20) = 5000/30 ≈ 166.667
    assert(std::abs((*it)->vwap - 166.6666666) < 0.001);
    assert((*it)->tick_count == 2);

    std::cout << "  single_threaded:   PASS\n";
}

static void test_multithreaded_with_worker_pool() {
    // This test verifies that the mutex in DataProcessor correctly
    // serialises concurrent process() calls from multiple worker threads.
    //
    // RACE CONDITION SCENARIO WITHOUT MUTEX:
    //   Thread A reads total_vol=0, computes total_vol+10=10
    //   Thread B reads total_vol=0, computes total_vol+20=20
    //   Thread A writes total_vol=10
    //   Thread B writes total_vol=20   ← Thread A's update is LOST
    //
    // With the mutex, only one thread can read-modify-write at a time,
    // so the final total_vol is always 10+20=30.

    DataProcessor dp;

    constexpr int kTicks = 50'000;

    // Create a worker pool that feeds ticks into the DataProcessor.
    // The handler captures `dp` by reference — this is safe because
    // we call pool.stop() before dp is used or destroyed.
    WorkerPool<TickRow> pool(
        [&dp](TickRow row) { dp.process(std::move(row)); },
        4,     // 4 worker threads
        1024   // bounded queue
    );
    pool.start();

    // Submit kTicks items for "TEST" symbol, each with price=100, volume=1
    for (int i = 0; i < kTicks; ++i)
        pool.submit(make_tick("TEST", 100.0, 1));

    pool.stop();

    auto results = dp.finalise();
    assert(results.size() == 1);
    assert(results[0]->symbol == "TEST");
    assert(results[0]->tick_count == static_cast<uint64_t>(kTicks));
    assert(results[0]->total_vol == static_cast<uint64_t>(kTicks));
    assert(std::abs(results[0]->vwap - 100.0) < 1e-9);

    assert(pool.tasks_completed() == static_cast<uint64_t>(kTicks));

    std::cout << "  multithreaded:     PASS\n";
}

static void test_unique_ptr_ownership_transfer() {
    // Demonstrates that unique_ptr ownership moves cleanly from
    // DataProcessor to the caller.  After finalise(), the processor
    // holds nothing — the caller owns the results.

    DataProcessor dp;
    dp.process(make_tick("MSFT", 300.0, 100));

    auto results = dp.finalise();
    assert(results.size() == 1);

    // Processor is now empty
    assert(dp.symbol_count() == 0);

    // Caller owns the result — accessing it is valid
    assert(results[0]->symbol == "MSFT");
    assert(results[0]->total_vol == 100);

    // When `results` goes out of scope, unique_ptrs automatically
    // delete all ProcessedResult objects — no manual cleanup needed.
    std::cout << "  ownership_transfer: PASS\n";
}

int main() {
    std::cout << "test_data_processor:\n";
    test_single_threaded_aggregation();
    test_multithreaded_with_worker_pool();
    test_unique_ptr_ownership_transfer();
    std::cout << "All data processor tests passed.\n";
    return 0;
}
