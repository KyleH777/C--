#include "thread_safe_queue.h"

#include <cassert>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

static void test_single_thread() {
    ThreadSafeQueue<int> q(4);
    q.push(1);
    q.push(2);
    q.push(3);

    assert(q.pop().value() == 1);
    assert(q.pop().value() == 2);
    assert(q.pop().value() == 3);

    q.shutdown();
    assert(!q.pop().has_value());

    std::cout << "  single_thread:        PASS\n";
}

static void test_multi_producer_consumer() {
    constexpr int kItems = 100'000;
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;

    ThreadSafeQueue<int> q(1024);
    std::atomic<int64_t> consumed_sum{0};
    std::atomic<int>     consumed_count{0};

    // Consumers
    std::vector<std::thread> consumers;
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&] {
            while (auto val = q.pop()) {
                consumed_sum.fetch_add(*val, std::memory_order_relaxed);
                consumed_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Producers — each pushes kItems / kProducers sequential ints
    {
        std::vector<std::thread> producers;
        int per = kItems / kProducers;
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&, p, per] {
                int start = p * per;
                for (int i = start; i < start + per; ++i)
                    q.push(i);
            });
        }
        for (auto& t : producers)
            t.join();
    }

    q.shutdown();

    for (auto& t : consumers)
        t.join();

    assert(consumed_count.load() == kItems);

    // Verify sum: 0 + 1 + ... + (kItems-1)
    int64_t expected = static_cast<int64_t>(kItems - 1) * kItems / 2;
    assert(consumed_sum.load() == expected);

    std::cout << "  multi_prod_cons:      PASS\n";
}

static void test_bounded_backpressure() {
    // Capacity 2: producer must block when queue is full
    ThreadSafeQueue<int> q(2);

    q.push(10);
    q.push(20);

    // Queue is full — push in another thread, it should block
    std::atomic<bool> pushed{false};
    std::thread producer([&] {
        q.push(30);  // blocks until a pop() frees space
        pushed.store(true);
    });

    // Give the producer thread time to start and block.
    // Use a generous timeout for CI runners under load.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(!pushed.load());  // still blocked

    auto val = q.pop();      // frees one slot
    assert(val.value() == 10);

    // Producer should now unblock — wait generously for CI
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(pushed.load());

    // Drain remaining
    assert(q.pop().value() == 20);
    assert(q.pop().value() == 30);

    q.shutdown();
    producer.join();

    std::cout << "  bounded_backpressure: PASS\n";
}

int main() {
    std::cout << "test_queue:\n";
    test_single_thread();
    test_multi_producer_consumer();
    test_bounded_backpressure();
    std::cout << "All queue tests passed.\n";
    return 0;
}
