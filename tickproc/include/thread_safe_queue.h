#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

/// Bounded, multi-producer / multi-consumer (MPMC) queue.
///
/// Producers call push() which blocks if the queue is full (backpressure).
/// Consumers call pop() which blocks until data is available or the queue
/// is shut down.  After shutdown(), pop() drains remaining items then
/// returns std::nullopt.
///
/// Bounding the queue is critical for 2 GB+ files: it caps memory to
/// (capacity * sizeof(T)) regardless of how fast the producer runs.
///
/// ─── How We Avoid Race Conditions ─────────────────────────────────────
///
///  INVARIANT: Every access to `queue_`, `done_`, and `capacity_` is
///  protected by `mu_`.  This is the single, global source of truth for
///  thread safety in this class.
///
///  WHY std::unique_lock (not lock_guard) in push/pop:
///    condition_variable::wait() must UNLOCK the mutex while the thread
///    sleeps, then RE-LOCK it before returning.  std::lock_guard does not
///    support unlock/re-lock, so we must use std::unique_lock.
///
///  WHY notify_all() in shutdown() (not notify_one()):
///    Multiple threads may be blocked in pop() or push().  notify_one()
///    wakes exactly one — the others remain stuck.  notify_all() wakes
///    every waiter so they can all observe `done_ == true` and exit.
///
///  SPURIOUS WAKEUP PROTECTION:
///    condition_variable::wait(lock, predicate) re-checks the predicate
///    after every wakeup.  If the wakeup was spurious (OS-level artifact),
///    the predicate returns false and the thread goes back to sleep.
///    Without the predicate form, a spurious wakeup would cause a thread
///    to pop from an empty queue → undefined behaviour.
/// ──────────────────────────────────────────────────────────────────────
template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(std::size_t capacity = 8192)
        : capacity_(capacity) {}

    /// Blocks if the queue is at capacity (backpressure).  No-op after shutdown.
    ///
    /// RACE CONDITION PREVENTION:
    ///   1. unique_lock acquires `mu_` — no other thread can read/write
    ///      `queue_` or `done_` concurrently.
    ///   2. wait() atomically releases `mu_` and sleeps until not_full_
    ///      is signalled.  "Atomically" is key: there is no window where
    ///      the mutex is released but the thread is not yet sleeping,
    ///      which would cause a lost wakeup.
    ///   3. On wakeup, wait() re-acquires `mu_` before returning, so
    ///      the push into `queue_` is still under the lock.
    void push(T item) {
        std::unique_lock lock(mu_);
        not_full_.wait(lock, [&] { return queue_.size() < capacity_ || done_; });
        if (done_) return;
        queue_.push(std::move(item));
        // notify_one() is sufficient here: only one consumer needs to
        // wake up per item.  notify_all() would work but wastes CPU
        // because N-1 threads would wake, check, and go back to sleep.
        not_empty_.notify_one();
    }

    /// Push a batch of items.  Holds the lock for the entire batch,
    /// reducing mutex acquisition overhead by up to batch_size×.
    ///
    /// TRADE-OFF: holding the lock longer increases latency for other
    /// threads waiting to push/pop.  In practice, batches of 256 items
    /// strike a good balance — the critical section is ~microseconds
    /// while the lock overhead saved is ~hundreds of microseconds.
    void push_batch(std::vector<T>& batch) {
        std::unique_lock lock(mu_);
        for (auto& item : batch) {
            not_full_.wait(lock, [&] { return queue_.size() < capacity_ || done_; });
            if (done_) return;
            queue_.push(std::move(item));
            not_empty_.notify_one();
        }
    }

    /// Returns std::nullopt only when the queue is empty AND shut down.
    ///
    /// CRITICAL ORDERING in the predicate:
    ///   `!queue_.empty() || done_`
    ///   We check empty FIRST.  If the queue has items, we consume them
    ///   even if shutdown has been called.  This guarantees no data loss.
    ///   Only when both conditions fail (queue empty AND not done) do we
    ///   go back to sleep.
    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock lock(mu_);
        not_empty_.wait(lock, [&] { return !queue_.empty() || done_; });
        // After waking: if the queue is empty, it means done_ == true
        // and no more items will ever arrive.  Return nullopt to signal
        // the consumer to exit its loop.
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }

    /// Signal that no more items will be pushed.
    ///
    /// SHUTDOWN PROTOCOL (order matters for correctness):
    ///   1. Acquire lock, set done_ = true, release lock.
    ///   2. notify_all() on BOTH condition variables.
    ///
    /// If we notified BEFORE setting done_, a thread could wake up,
    /// see done_ == false, and go back to sleep — then never wake again
    /// because the notify already happened.  Setting the flag first
    /// guarantees every woken thread sees the shutdown state.
    void shutdown() {
        {
            std::lock_guard lock(mu_);
            done_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    [[nodiscard]] bool is_shutdown() const {
        std::lock_guard lock(mu_);
        return done_;
    }

private:
    // ── All fields below are shared mutable state ──────────────────────
    // `mu_` is the single lock that protects them.  This "coarse-grained"
    // locking strategy is correct by construction: you cannot access any
    // field without holding `mu_`, so no data race is possible.
    mutable std::mutex mu_;

    // Two condition variables partition waiters by their intent:
    //   not_empty_  — consumers sleep here when the queue is empty
    //   not_full_   — producers sleep here when the queue is at capacity
    // Using separate CVs avoids thundering-herd: a push only wakes
    // consumers, not other producers; a pop only wakes producers.
    std::condition_variable not_empty_;
    std::condition_variable not_full_;

    std::queue<T> queue_;
    std::size_t capacity_;
    bool done_ = false;
};
