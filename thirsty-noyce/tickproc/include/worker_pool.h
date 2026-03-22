#pragma once

#include "thread_safe_queue.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

/// A dynamically-sized worker pool that pulls tasks from a ThreadSafeQueue.
///
/// Thread count is determined at runtime via std::thread::hardware_concurrency(),
/// ensuring we scale to the host machine without hard-coding thread counts.
///
/// ─── Race Condition Prevention Strategy ───────────────────────────────────
///
///  1. SHARED MUTABLE STATE is confined to:
///       • The ThreadSafeQueue (internally mutex-protected)
///       • The atomic counters (lock-free by design)
///     Workers never share raw pointers, references, or unprotected data.
///
///  2. TASK OWNERSHIP: Each task is moved (not copied) out of the queue via
///     std::optional<T>::value(), so exactly ONE worker owns each task at
///     any moment.  This eliminates data races on task payloads entirely.
///
///  3. SHUTDOWN ORDERING: We call queue_.shutdown() BEFORE joining threads.
///     shutdown() sets an internal flag AND notifies all blocked consumers,
///     guaranteeing no thread remains stuck in pop().  Without this ordering,
///     join() would deadlock because workers block on an empty queue forever.
///
///  4. ATOMIC COUNTERS use std::memory_order_relaxed because we only need
///     eventual visibility for statistics — no other operation depends on
///     their exact value at any point.  This avoids unnecessary memory
///     fences on architectures like ARM where they are expensive.
/// ──────────────────────────────────────────────────────────────────────────

template <typename Task>
class WorkerPool {
public:
    using TaskHandler = std::function<void(Task)>;

    /// Construct the pool but do NOT start threads yet.
    /// `handler` will be called once per task, on whatever thread pops it.
    ///
    /// SAFETY: `handler` must be thread-safe.  If it captures shared state,
    /// that state must be protected by a mutex or be atomic.  The pool
    /// itself guarantees that `handler` is never called concurrently on the
    /// SAME task, but it WILL be called concurrently on DIFFERENT tasks
    /// from multiple threads.
    explicit WorkerPool(TaskHandler handler,
                        unsigned num_threads = 0,
                        std::size_t queue_capacity = 8192)
        : handler_(std::move(handler))
        , queue_(queue_capacity)
    {
        // Dynamically determine thread count from hardware.
        // Fallback to 2 if hardware_concurrency() returns 0 (spec allows it).
        if (num_threads == 0)
            num_threads = std::max(2u, std::thread::hardware_concurrency());

        num_threads_ = num_threads;
    }

    /// Start all worker threads.  Each thread runs a consume loop:
    ///   while (auto task = queue_.pop()) { handler_(*task); }
    ///
    /// RACE CONDITION NOTE:  start() is NOT safe to call concurrently with
    /// itself or with submit()/stop().  The caller must ensure start() is
    /// called exactly once, before any submit() calls.  In practice, this
    /// is trivially satisfied because start() runs in the main thread
    /// during initialisation.
    void start() {
        workers_.reserve(num_threads_);
        for (unsigned i = 0; i < num_threads_; ++i) {
            workers_.emplace_back([this] {
                // ── Worker loop ────────────────────────────────────────
                // pop() blocks on a condition_variable until either:
                //   (a) a new task is available, or
                //   (b) the queue is shut down AND empty.
                //
                // CRITICAL: pop() returns std::nullopt ONLY when both
                // conditions are true: shutdown flag is set AND the
                // internal std::queue is empty.  This guarantees we
                // process every submitted task before exiting — no
                // data is silently dropped.
                while (auto task = queue_.pop()) {
                    handler_(std::move(*task));
                    tasks_completed_.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
    }

    /// Submit a task to the pool.  Blocks if the queue is at capacity
    /// (backpressure).
    ///
    /// THREAD SAFETY: safe to call from any thread, including multiple
    /// producer threads concurrently.  The queue's internal mutex serialises
    /// access to the underlying std::queue.
    void submit(Task task) {
        queue_.push(std::move(task));
        tasks_submitted_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Signal that no more tasks will be submitted, then block until
    /// every worker thread has finished processing remaining tasks.
    ///
    /// DEADLOCK PREVENTION:
    ///   1. shutdown() wakes ALL threads blocked in pop() via notify_all().
    ///   2. Each thread will drain remaining items, then pop() returns
    ///      std::nullopt, breaking the while-loop.
    ///   3. Only AFTER shutdown() do we join — so no thread can be stuck.
    ///
    ///   If we joined BEFORE calling shutdown(), workers blocking on an
    ///   empty queue would never wake up → deadlock.
    void stop() {
        queue_.shutdown();

        // join() blocks until each thread's function returns.
        // Since shutdown has been signalled, each thread will eventually
        // exit its consume loop and return.
        for (auto& t : workers_) {
            if (t.joinable())
                t.join();
        }
        workers_.clear();
    }

    ~WorkerPool() {
        // RAII safety net: if the user forgot to call stop(), we still
        // shut down cleanly.  This prevents the std::thread destructor
        // from calling std::terminate() on a joinable thread.
        if (!workers_.empty())
            stop();
    }

    // ── Non-copyable, non-movable (threads reference `this`) ───────────
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;

    [[nodiscard]] uint64_t tasks_submitted() const noexcept {
        return tasks_submitted_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64_t tasks_completed() const noexcept {
        return tasks_completed_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] unsigned thread_count() const noexcept {
        return num_threads_;
    }

private:
    TaskHandler                handler_;
    ThreadSafeQueue<Task>      queue_;
    std::vector<std::thread>   workers_;
    unsigned                   num_threads_ = 0;
    std::atomic<uint64_t>      tasks_submitted_{0};
    std::atomic<uint64_t>      tasks_completed_{0};
};
