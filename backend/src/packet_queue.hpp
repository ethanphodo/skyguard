#pragma once

/**
 * PacketQueue - Thread-Safe Bounded Queue
 *
 * Lock-free? No - we use mutex + condition_variable for simplicity.
 * For interview: "I chose mutex-based queue because:
 * 1. Correctness is easier to verify than lock-free
 * 2. Contention is low (single producer, multiple consumers)
 * 3. Condition variable provides efficient blocking
 *
 * A lock-free queue (e.g., moodycamel::ConcurrentQueue) could be
 * substituted if profiling shows contention."
 *
 * Features:
 * - Bounded size to prevent memory exhaustion
 * - Blocking pop with timeout
 * - Non-blocking try_pop
 * - Shutdown signaling
 */

#include "flight_packet.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

namespace skyguard {

template<typename T>
class BoundedQueue {
public:
    /**
     * Construct a bounded queue.
     * @param max_size Maximum number of elements (0 = unlimited)
     */
    explicit BoundedQueue(size_t max_size = constants::MAX_QUEUE_SIZE)
        : max_size_(max_size)
        , shutdown_(false)
    {}

    // Non-copyable, non-movable (contains mutex)
    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;

    // ========================================
    // Producer Operations
    // ========================================

    /**
     * Push an item to the queue.
     *
     * @param item Item to push (moved)
     * @return true if pushed, false if queue full or shutdown
     *
     * DESIGN: We drop packets if queue is full rather than blocking.
     * In real-time systems, it's better to lose old data than stall.
     */
    bool push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (shutdown_) {
            return false;
        }

        if (max_size_ > 0 && queue_.size() >= max_size_) {
            // Queue full - drop oldest to make room (or could drop new)
            // For interview: "I chose to drop new packets because
            // older packets are already being processed and represent
            // committed state."
            return false;
        }

        queue_.push(std::move(item));
        cv_.notify_one();
        return true;
    }

    // ========================================
    // Consumer Operations
    // ========================================

    /**
     * Pop an item, blocking until available or timeout.
     *
     * @param timeout Maximum time to wait
     * @return Item if available, nullopt if timeout or shutdown
     */
    template<typename Rep, typename Period>
    std::optional<T> pop(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock<std::mutex> lock(mutex_);

        // Wait for item or shutdown
        bool got_item = cv_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || shutdown_;
        });

        if (!got_item || shutdown_ || queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    /**
     * Pop an item, blocking indefinitely.
     *
     * @return Item if available, nullopt if shutdown
     */
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [this] {
            return !queue_.empty() || shutdown_;
        });

        if (shutdown_ || queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    /**
     * Try to pop without blocking.
     *
     * @return Item if available, nullopt if empty
     */
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    // ========================================
    // Control Operations
    // ========================================

    /**
     * Signal shutdown. Wakes all waiting consumers.
     * After shutdown, push() returns false and pop() returns nullopt.
     */
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

    /**
     * Check if shutdown has been signaled.
     */
    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

    /**
     * Clear all items from queue.
     * @return Number of items cleared
     */
    size_t clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = queue_.size();
        std::queue<T> empty;
        std::swap(queue_, empty);
        return count;
    }

    // ========================================
    // Status
    // ========================================

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t max_size() const {
        return max_size_;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t max_size_;
    bool shutdown_;
};

// Type alias for our specific use case
using PacketQueue = BoundedQueue<FlightPacket>;

} // namespace skyguard
