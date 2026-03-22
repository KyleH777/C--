#pragma once

#include "tick_row.h"
#include "worker_pool.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

/// Represents a fully-processed result for one symbol.
/// Managed exclusively via std::unique_ptr — when the DataProcessor is
/// destroyed, all ProcessedResult objects are automatically freed.
/// No raw `new` or `delete` ever appears in client code.
struct ProcessedResult {
    std::string symbol;
    double      vwap       = 0.0;   // volume-weighted average price
    double      turnover   = 0.0;   // Σ(price × volume)
    uint64_t    total_vol  = 0;
    uint64_t    tick_count = 0;

    void finalise() {
        if (total_vol > 0)
            vwap = turnover / static_cast<double>(total_vol);
    }
};

/// DataProcessor: consumes TickRow objects from a WorkerPool and aggregates
/// per-symbol statistics.
///
/// ─── Concurrency Design ───────────────────────────────────────────────────
///
///  The fundamental challenge: multiple worker threads call process()
///  concurrently, and each call mutates the shared `results_` map.
///  Without synchronisation this is a DATA RACE (undefined behaviour in
///  C++, not merely "incorrect" — the compiler is free to assume it never
///  happens, leading to arbitrarily wrong code generation).
///
///  Our mitigation is a std::mutex (`mu_`) that guards ALL accesses to
///  `results_`.  We use std::lock_guard (RAII) so the mutex is released
///  even if an exception is thrown — this eliminates an entire class of
///  bugs where a `return` or `throw` skips an `unlock()` call.
///
///  ALTERNATIVE CONSIDERED: per-symbol fine-grained locking.  This would
///  allow concurrent updates to different symbols but adds complexity
///  (one mutex per symbol, or a concurrent hash map).  For financial tick
///  data where symbols are relatively few (thousands) but ticks per symbol
///  are millions, the coarse lock is simpler and sufficient because the
///  critical section is tiny (a few additions).  Profile before optimising.
/// ──────────────────────────────────────────────────────────────────────────
class DataProcessor {
public:
    /// Process a single tick.  Called from worker threads.
    ///
    /// THREAD SAFETY: This method is safe to call from any thread.
    /// The mutex serialises access to the results map.
    ///
    /// RACE CONDITION PREVENTION (step-by-step):
    ///   1. lock_guard acquires `mu_` — only ONE thread can be inside
    ///      this block at a time.  All other threads calling process()
    ///      block on the mutex until it is released.
    ///   2. We look up (or create) the unique_ptr for this symbol.
    ///   3. We mutate the pointed-to ProcessedResult.
    ///   4. lock_guard destructor releases `mu_` — the next blocked
    ///      thread (if any) wakes up and enters the critical section.
    ///
    /// Because the lock_guard is RAII, the mutex is released even if
    /// operator[] or make_unique throws (e.g., std::bad_alloc).
    void process(TickRow row) {
        std::lock_guard<std::mutex> lock(mu_);

        auto& ptr = results_[row.symbol];

        // First tick for this symbol — allocate via unique_ptr.
        // std::make_unique is preferred over raw `new` because:
        //   • It is exception-safe (no leak if the map insertion throws)
        //   • It makes ownership semantics explicit in the type system
        if (!ptr) {
            ptr = std::make_unique<ProcessedResult>();
            ptr->symbol = row.symbol;
        }

        // Accumulate running totals.  These fields are only ever accessed
        // under `mu_`, so no atomic operations are needed for them.
        ptr->turnover  += row.price * static_cast<double>(row.volume);
        ptr->total_vol += row.volume;
        ptr->tick_count++;
    }

    /// Finalise and return all results.  Must be called AFTER the
    /// WorkerPool has been stopped — i.e., no concurrent process() calls.
    ///
    /// We still acquire the lock for correctness: even though no workers
    /// are running, the mutex acquire acts as a memory fence, guaranteeing
    /// we see the latest writes from all worker threads.  On x86 this is
    /// a no-op in practice, but on ARM/POWER it prevents stale reads.
    ///
    /// Ownership of the unique_ptrs is MOVED out of the map, so after
    /// this call the DataProcessor holds nothing — the caller is now
    /// responsible for the lifetime of each ProcessedResult.
    std::vector<std::unique_ptr<ProcessedResult>> finalise() {
        std::lock_guard<std::mutex> lock(mu_);

        std::vector<std::unique_ptr<ProcessedResult>> out;
        out.reserve(results_.size());

        for (auto& [symbol, ptr] : results_) {
            ptr->finalise();
            out.push_back(std::move(ptr));
        }

        results_.clear();
        return out;
    }

    /// Number of distinct symbols seen so far.
    [[nodiscard]] std::size_t symbol_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return results_.size();
    }

private:
    // ── Shared mutable state ───────────────────────────────────────────
    //
    // `mu_` protects `results_`.  ANY code path that reads or writes
    // `results_` MUST hold `mu_`.  This invariant is enforced by making
    // `results_` private and only accessing it through methods that
    // acquire the lock.
    //
    // Using std::unique_ptr<ProcessedResult> rather than ProcessedResult
    // directly because:
    //   1. It demonstrates smart-pointer lifecycle management.
    //   2. It avoids object slicing if ProcessedResult is ever subclassed.
    //   3. Moves are O(1) regardless of ProcessedResult size.
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<ProcessedResult>> results_;
};
