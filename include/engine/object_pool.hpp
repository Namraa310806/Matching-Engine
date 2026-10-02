#pragma once

#include <vector>
#include <cstddef>
#include <memory>
#include <algorithm>
#include <new>

namespace engine {

// Object pool for OrderNode allocation
// Provides fast allocation/deallocation via freelist with chunk-based growth
// Designed to minimize dynamic allocations on the hot path
// Uses raw memory buffers to avoid requiring default constructors
template <typename T>
class ObjectPool {
public:
    // Configuration for pool behavior
    struct Config {
        size_t initial_chunk_size = 1024;      // Initial nodes to allocate
        size_t max_chunk_size = 65536;         // Maximum chunk size (caps growth)
        double growth_factor = 2.0;            // Exponential growth factor
        bool enable_freelist = true;           // Enable freelist for recycling
    };

    explicit ObjectPool(const Config& config = Config{})
        : config_(config)
        , allocated_count_(0)
        , freed_count_(0)
        , pool_hits_(0)
        , pool_misses_(0)
    {
        allocate_chunk(config_.initial_chunk_size);
    }

    ~ObjectPool() {
        // Raw memory is automatically freed by vector
        // Caller is responsible for destroying constructed objects
    }

    // Allocate an object from the pool
    // Returns a pointer to uninitialized memory (caller must construct)
    T* allocate() {
        if (config_.enable_freelist && !freelist_.empty()) {
            T* ptr = freelist_.back();
            freelist_.pop_back();
            pool_hits_++;
            allocated_count_++;
            return ptr;
        }

        // No free nodes, need to allocate more
        pool_misses_++;
        return allocate_from_chunks();
    }

    // Allocate from chunks (when freelist is empty)
    T* allocate_from_chunks() {
        // Calculate next chunk size with exponential growth (capped)
        size_t next_chunk_size = config_.initial_chunk_size;
        if (!chunks_.empty()) {
            size_t last_size = chunks_.back().capacity;
            next_chunk_size = static_cast<size_t>(last_size * config_.growth_factor);
            next_chunk_size = std::min(next_chunk_size, config_.max_chunk_size);
        }

        allocate_chunk(next_chunk_size);

        // If freelist is enabled, take from it
        if (config_.enable_freelist) {
            if (!freelist_.empty()) {
                T* ptr = freelist_.back();
                freelist_.pop_back();
                allocated_count_++;
                return ptr;
            }
        } else {
            // Linear allocation within the new chunk
            Chunk& chunk = chunks_.back();
            if (chunk.used_count < chunk.capacity) {
                T* ptr = reinterpret_cast<T*>(&chunk.buffer[chunk.used_count * sizeof(T)]);
                chunk.used_count++;
                allocated_count_++;
                return ptr;
            }
        }

        // Should not reach here
        return nullptr;
    }

    // Return an object to the pool
    // Caller is responsible for calling destructor before returning
    void deallocate(T* ptr) {
        if (!ptr) return;

        if (config_.enable_freelist) {
            freelist_.push_back(ptr);
            freed_count_++;
        }
        // If freelist disabled, we don't recycle - chunks handle cleanup
    }

    // Statistics for benchmarking/analysis
    struct Stats {
        size_t allocated_count;      // Total allocations
        size_t freed_count;          // Total deallocations
        size_t pool_hits;            // Allocations from freelist
        size_t pool_misses;          // Allocations requiring new chunks
        size_t chunk_count;          // Number of chunks allocated
        size_t total_capacity;       // Total nodes across all chunks
        size_t freelist_size;        // Current freelist size
    };

    Stats stats() const {
        size_t total_capacity = 0;
        for (const auto& chunk : chunks_) {
            total_capacity += chunk.capacity;
        }
        return {
            allocated_count_,
            freed_count_,
            pool_hits_,
            pool_misses_,
            chunks_.size(),
            total_capacity,
            freelist_.size()
        };
    }

    // Reset pool state (clears freelist but keeps allocated chunks)
    void reset() {
        freelist_.clear();
        allocated_count_ = 0;
        freed_count_ = 0;
        pool_hits_ = 0;
        pool_misses_ = 0;
    }

    // Clear all resources (deallocates all chunks)
    void clear() {
        chunks_.clear();
        freelist_.clear();
        allocated_count_ = 0;
        freed_count_ = 0;
        pool_hits_ = 0;
        pool_misses_ = 0;
    }

private:
    struct Chunk {
        std::vector<char> buffer;  // Raw memory buffer
        size_t capacity;          // Number of objects this chunk can hold
        size_t used_count;        // Number of objects currently used (for linear alloc)
    };

    Config config_;
    std::vector<Chunk> chunks_;        // Chunks of pre-allocated memory
    std::vector<T*> freelist_;         // Free nodes for recycling
    size_t allocated_count_;
    size_t freed_count_;
    size_t pool_hits_;
    size_t pool_misses_;

    // Allocate a new chunk of nodes
    void allocate_chunk(size_t chunk_size) {
        Chunk chunk;
        chunk.buffer.resize(chunk_size * sizeof(T));
        chunk.capacity = chunk_size;
        chunk.used_count = 0;
        chunks_.push_back(std::move(chunk));

        // Add all nodes in chunk to freelist
        if (config_.enable_freelist) {
            for (size_t i = 0; i < chunk_size; ++i) {
                freelist_.push_back(reinterpret_cast<T*>(&chunks_.back().buffer[i * sizeof(T)]));
            }
        }
    }
};

} // namespace engine
