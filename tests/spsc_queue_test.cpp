#include <gtest/gtest.h>
#include <engine/spsc_queue.hpp>
#include <thread>
#include <vector>
#include <set>
#include <atomic>
#include <mutex>

using namespace engine;

// ============================================================================
// Basic enqueue/dequeue tests
// ============================================================================

// Test: Enqueue and dequeue a sequence of values
TEST(SpscQueueTest, EnqueueDequeueSequence) {
    SpscQueue<int, 64> queue;

    // Enqueue values
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(queue.enqueue(i));
    }

    // Dequeue and verify order
    for (int i = 0; i < 10; ++i) {
        int value;
        EXPECT_TRUE(queue.dequeue(value));
        EXPECT_EQ(value, i);
    }

    // Queue should be empty
    EXPECT_TRUE(queue.empty());
}

// Test: Dequeue from empty queue returns false
TEST(SpscQueueTest, EmptyBehavior) {
    SpscQueue<int, 64> queue;

    int value;
    EXPECT_FALSE(queue.dequeue(value));
    EXPECT_TRUE(queue.empty());
}

// Test: Enqueue to full queue returns false
TEST(SpscQueueTest, FullBehavior) {
    SpscQueue<int, 16> queue;

    // Fill queue to capacity-1 (ring buffer needs one empty slot to distinguish full from empty)
    for (size_t i = 0; i < queue.capacity() - 1; ++i) {
        EXPECT_TRUE(queue.enqueue(static_cast<int>(i)));
    }

    // Next enqueue should fail
    EXPECT_FALSE(queue.enqueue(999));
    EXPECT_TRUE(queue.full());

    // Verify all values are still in queue
    for (size_t i = 0; i < queue.capacity() - 1; ++i) {
        int value;
        EXPECT_TRUE(queue.dequeue(value));
        EXPECT_EQ(value, static_cast<int>(i));
    }
}

// ============================================================================
// Wraparound tests
// ============================================================================

// Test: Producer/consumer indices wrap around correctly
TEST(SpscQueueTest, Wraparound) {
    SpscQueue<int, 16> queue;

    // Force multiple wraparounds by enqueueing and dequeueing many times
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Enqueue
        for (int i = 0; i < 10; ++i) {
            EXPECT_TRUE(queue.enqueue(iteration * 10 + i));
        }

        // Dequeue
        for (int i = 0; i < 10; ++i) {
            int value;
            EXPECT_TRUE(queue.dequeue(value));
            EXPECT_EQ(value, iteration * 10 + i);
        }
    }

    EXPECT_TRUE(queue.empty());
}

// Test: Wraparound at capacity boundary
TEST(SpscQueueTest, WraparoundAtCapacity) {
    SpscQueue<int, 8> queue;

    // Fill to capacity - 1 (ring buffer needs one empty slot)
    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(queue.enqueue(i));
    }

    // Dequeue some
    for (int i = 0; i < 3; ++i) {
        int value;
        EXPECT_TRUE(queue.dequeue(value));
        EXPECT_EQ(value, i);
    }

    // Enqueue more (this will wrap around) - only 3 slots available
    for (int i = 7; i < 10; ++i) {
        EXPECT_TRUE(queue.enqueue(i));
    }

    // Dequeue remaining and verify
    for (int i = 3; i < 10; ++i) {
        int value;
        EXPECT_TRUE(queue.dequeue(value));
        EXPECT_EQ(value, i);
    }

    EXPECT_TRUE(queue.empty());
}

// ============================================================================
// FIFO ordering tests
// ============================================================================

// Test: Values are dequeued in exactly the order they were enqueued
TEST(SpscQueueTest, FIFOOrdering) {
    SpscQueue<int, 64> queue;

    std::vector<int> input = {5, 10, 15, 20, 25, 30, 35, 40};

    for (int val : input) {
        EXPECT_TRUE(queue.enqueue(val));
    }

    std::vector<int> output;
    int value;
    while (queue.dequeue(value)) {
        output.push_back(value);
    }

    EXPECT_EQ(output, input);
}

// ============================================================================
// Concurrent stress test
// ============================================================================

// Test: Producer and consumer run concurrently with large number of messages
TEST(SpscQueueTest, ConcurrentStress) {
    SpscQueue<int, 1024> queue;
    const int num_messages = 10000;
    std::atomic<int> consumed_count{0};

    // Consumer thread
    auto consumer = [&]() {
        int value;
        while (consumed_count.load() < num_messages) {
            if (queue.dequeue(value)) {
                consumed_count.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    };

    // Producer thread
    auto producer = [&]() {
        for (int i = 0; i < num_messages; ++i) {
            while (!queue.enqueue(i)) {
                std::this_thread::yield();
            }
        }
    };

    std::thread t1(producer);
    std::thread t2(consumer);

    t1.join();
    t2.join();

    EXPECT_EQ(consumed_count.load(), num_messages);
    EXPECT_TRUE(queue.empty());
}

// ============================================================================
// No-loss/no-duplication tests
// ============================================================================

// Test: Every message is received exactly once with unique sequence numbers
TEST(SpscQueueTest, NoLossNoDuplication) {
    struct Message {
        int sequence;
        int data;
    };

    SpscQueue<Message, 1024> queue;
    const int num_messages = 5000;
    std::atomic<int> consumed_count{0};
    std::atomic<int> min_sequence{num_messages};
    std::atomic<int> max_sequence{-1};

    // Consumer thread
    auto consumer = [&]() {
        Message msg;
        while (consumed_count.load() < num_messages) {
            if (queue.dequeue(msg)) {
                int seq = msg.sequence;
                consumed_count.fetch_add(1);
                // Track min/max to verify range
                int current_min = min_sequence.load();
                while (seq < current_min && !min_sequence.compare_exchange_weak(current_min, seq)) {
                    current_min = min_sequence.load();
                }
                int current_max = max_sequence.load();
                while (seq > current_max && !max_sequence.compare_exchange_weak(current_max, seq)) {
                    current_max = max_sequence.load();
                }
            } else {
                std::this_thread::yield();
            }
        }
    };

    // Producer thread
    auto producer = [&]() {
        for (int i = 0; i < num_messages; ++i) {
            Message msg{i, i * 2};
            while (!queue.enqueue(msg)) {
                std::this_thread::yield();
            }
        }
    };

    std::thread t1(producer);
    std::thread t2(consumer);

    t1.join();
    t2.join();

    // Verify expected count
    EXPECT_EQ(consumed_count.load(), num_messages);
    EXPECT_EQ(min_sequence.load(), 0);
    EXPECT_EQ(max_sequence.load(), num_messages - 1);
    EXPECT_TRUE(queue.empty());
}

// ============================================================================
// Move semantics test
// ============================================================================

// Test: Queue works with move-only types
TEST(SpscQueueTest, MoveOnlyType) {
    struct MoveOnly {
        int value;
        MoveOnly(int v) : value(v) {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&& other) : value(other.value) {}
        MoveOnly& operator=(MoveOnly&& other) {
            value = other.value;
            return *this;
        }
    };

    SpscQueue<MoveOnly, 64> queue;

    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(queue.enqueue(MoveOnly(i)));
    }

    for (int i = 0; i < 10; ++i) {
        MoveOnly value(0);
        EXPECT_TRUE(queue.dequeue(value));
        EXPECT_EQ(value.value, i);
    }

    EXPECT_TRUE(queue.empty());
}

// ============================================================================
// Size/capacity tests
// ============================================================================

// Test: Size and capacity methods work correctly
TEST(SpscQueueTest, SizeAndCapacity) {
    SpscQueue<int, 32> queue;

    EXPECT_EQ(queue.capacity(), 32);
    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.empty());

    // Enqueue some values
    for (int i = 0; i < 10; ++i) {
        queue.enqueue(i);
        EXPECT_EQ(queue.size(), static_cast<size_t>(i + 1));
    }

    EXPECT_FALSE(queue.empty());

    // Dequeue some values
    for (int i = 0; i < 5; ++i) {
        int value;
        queue.dequeue(value);
        EXPECT_EQ(queue.size(), static_cast<size_t>(10 - i - 1));
    }

    EXPECT_EQ(queue.size(), 5);
    EXPECT_FALSE(queue.empty());

    // Dequeue remaining
    for (int i = 0; i < 5; ++i) {
        int value;
        queue.dequeue(value);
    }

    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.empty());
}

// ============================================================================
// Complex type test
// ============================================================================

// Test: Queue works with complex types (std::string)
TEST(SpscQueueTest, ComplexType) {
    SpscQueue<std::string, 64> queue;

    std::vector<std::string> inputs = {
        "hello", "world", "test", "queue", "spsc"
    };

    for (const auto& s : inputs) {
        EXPECT_TRUE(queue.enqueue(s));
    }

    std::vector<std::string> outputs;
    std::string value;
    while (queue.dequeue(value)) {
        outputs.push_back(value);
    }

    EXPECT_EQ(outputs, inputs);
    EXPECT_TRUE(queue.empty());
}
