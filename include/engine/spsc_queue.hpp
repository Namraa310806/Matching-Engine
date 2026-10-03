#pragma once

#include <atomic>
#include <cstdint>
#include <new>

namespace engine {

// Single-Producer Single-Consumer (SPSC) lock-free ring buffer
//
// Architecture:
// - Producer thread owns the write position exclusively
// - Consumer thread owns the read position exclusively
// - No mutex or condition_variable inside the queue
// - Uses power-of-two capacity for efficient wraparound with bitmask
//
// Memory Ordering:
// - Producer enqueue: load read_index with acquire, store write_index with release
// - Consumer dequeue: load write_index with acquire, store read_index with release
// - Bidirectional acquire/release ensures proper synchronization of buffer access
//
// Key Invariants:
// - Only the producer calls enqueue()
// - Only the consumer calls dequeue()
// - Producer cannot see consumer's read position changes
// - Consumer cannot see producer's write position changes
// - Data publication is synchronized through bidirectional acquire/release
//
// Capacity: Fixed at construction time, power-of-two preferred
template<typename T, size_t Capacity>
class SpscQueue {
public:
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");

    SpscQueue()
        : write_index_(0)
        , read_index_(0)
    {
        // Pre-allocate ring buffer
        buffer_ = static_cast<T*>(::operator new[](Capacity * sizeof(T)));
    }

    ~SpscQueue() {
        // Destroy any remaining elements directly without requiring default constructor
        size_t current_read = read_index_.load(std::memory_order_relaxed);
        const size_t current_write = write_index_.load(std::memory_order_relaxed);

        while (current_read != current_write) {
            buffer_[current_read].~T();
            current_read = (current_read + 1) & mask_;
        }

        ::operator delete[](buffer_);
    }

    // Non-copyable, non-movable (queue has unique ownership of buffer)
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    // Enqueue a value (producer-only)
    // Returns true if successful, false if queue is full
    //
    // Memory ordering rationale:
    // - load(read_index_) with acquire: synchronizes with consumer's release store
    //   ensuring the slot we're about to write is no longer being read
    // - store write_index_ with release: ensures the written value is visible
    //   to consumer before the write index update becomes visible
    bool enqueue(const T& value) {
        const size_t current_write = write_index_.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) & mask_;

        // Check if queue is full
        // Acquire synchronizes with consumer's release on read_index
        const size_t current_read = read_index_.load(std::memory_order_acquire);
        if (next_write == current_read) {
            return false; // Queue is full
        }

        // Write the value at current_write position
        // Placement new to construct in pre-allocated memory
        new (&buffer_[current_write]) T(value);

        // Publish the write position with release semantics
        // This establishes a happens-before relationship with the consumer's acquire load
        // All writes to buffer_[current_write] happen-before this store
        write_index_.store(next_write, std::memory_order_release);

        return true;
    }

    // Enqueue a value (producer-only, move version)
    bool enqueue(T&& value) {
        const size_t current_write = write_index_.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) & mask_;

        // Check if queue is full
        // Acquire synchronizes with consumer's release on read_index
        const size_t current_read = read_index_.load(std::memory_order_acquire);
        if (next_write == current_read) {
            return false; // Queue is full
        }

        new (&buffer_[current_write]) T(std::move(value));

        write_index_.store(next_write, std::memory_order_release);

        return true;
    }

    // Dequeue a value (consumer-only)
    // Returns true if successful, false if queue is empty
    //
    // Memory ordering rationale:
    // - load(read_index_) with relaxed: consumer owns this index exclusively
    // - load(write_index_) with acquire: synchronizes with producer's release store
    //   ensuring the value at current_read is fully constructed before we read it
    // - store read_index_ with release: ensures that future producer sees our progress
    bool dequeue(T& value) {
        const size_t current_read = read_index_.load(std::memory_order_relaxed);

        // Check if queue is empty
        // Acquire synchronizes with producer's release store for the slot we're about to read
        const size_t current_write = write_index_.load(std::memory_order_acquire);
        if (current_read == current_write) {
            return false; // Queue is empty
        }

        // Read the value at current_read position
        // This happens-after the acquire, so we see the fully constructed value
        value = std::move(buffer_[current_read]);

        // Destroy the old value
        buffer_[current_read].~T();

        // Update read position with release to publish our progress to producer
        const size_t next_read = (current_read + 1) & mask_;
        read_index_.store(next_read, std::memory_order_release);

        return true;
    }

    // Check if queue is empty (consumer-only or producer-only)
    // Note: If called from producer, this is an approximation (consumer may have consumed)
    // If called from consumer, this is accurate
    bool empty() const {
        // Relaxed is sufficient for single-writer indices
        // In SPSC, only the consumer should rely on this for correctness
        return read_index_.load(std::memory_order_relaxed) ==
               write_index_.load(std::memory_order_relaxed);
    }

    // Check if queue is full (producer-only)
    // Note: Consumer should not call this as it's an approximation
    bool full() const {
        const size_t current_write = write_index_.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) & mask_;
        const size_t current_read = read_index_.load(std::memory_order_relaxed);
        return next_write == current_read;
    }

    // Get current size (approximate for producer, accurate for consumer)
    size_t size() const {
        const size_t write = write_index_.load(std::memory_order_relaxed);
        const size_t read = read_index_.load(std::memory_order_relaxed);
        return (write - read) & mask_;
    }

    // Get capacity
    static constexpr size_t capacity() {
        return Capacity;
    }

private:
    // Ring buffer storage (pre-allocated)
    T* buffer_;

    // Bitmask for wraparound (capacity - 1, since capacity is power of two)
    const size_t mask_ = Capacity - 1;

    // Producer-owned: write position (only producer modifies)
    // Consumer reads this with acquire to synchronize with producer's release
    std::atomic<size_t> write_index_;

    // Consumer-owned: read position (only consumer modifies)
    // Producer reads this with relaxed (no synchronization needed)
    std::atomic<size_t> read_index_;
};

} // namespace engine
