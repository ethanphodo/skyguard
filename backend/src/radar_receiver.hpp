#pragma once

/**
 * RadarReceiver - UDP Socket Listener
 *
 * Receives simulated radar data via UDP and enqueues for processing.
 *
 * Design for real-time:
 * - Runs on high-priority thread (SCHED_FIFO)
 * - Minimal processing in receive loop
 * - Non-blocking enqueue (drops if full)
 *
 * For interview: "I used UDP because:
 * 1. Real radar systems use UDP for low latency
 * 2. Packet loss is acceptable - we'll get the next ping
 * 3. No connection setup overhead
 * 4. Multicast support for multiple receivers"
 */

#include "types.hpp"
#include "packet_queue.hpp"
#include <atomic>
#include <thread>
#include <memory>
#include <functional>
#include <string>
#include <tuple>

namespace skyguard {

/**
 * Statistics tracked by the receiver
 */
struct ReceiverStats {
    std::atomic<uint64_t> packets_received{0};
    std::atomic<uint64_t> packets_valid{0};
    std::atomic<uint64_t> packets_invalid{0};
    std::atomic<uint64_t> packets_dropped{0};  // Queue full
    std::atomic<uint64_t> bytes_received{0};

    // Calculate packets per second (call periodically)
    double packets_per_second(Duration elapsed) const {
        return static_cast<double>(packets_received) / elapsed.count();
    }
};

/**
 * RadarReceiver class
 */
class RadarReceiver {
public:
    // Callback for logging/metrics (optional)
    using LogCallback = std::function<void(const std::string&)>;

    /**
     * Construct a radar receiver.
     *
     * @param bind_address Address to bind to (e.g., "0.0.0.0")
     * @param port UDP port to listen on
     * @param queue Shared queue to push packets to
     */
    RadarReceiver(const std::string& bind_address, int port, std::shared_ptr<PacketQueue> queue);

    /**
     * Destructor - ensures clean shutdown.
     * RAII: Socket is closed, thread is joined.
     */
    ~RadarReceiver();

    // Non-copyable (owns resources)
    RadarReceiver(const RadarReceiver&) = delete;
    RadarReceiver& operator=(const RadarReceiver&) = delete;

    // ========================================
    // Lifecycle
    // ========================================

    /**
     * Start the receiver thread.
     * @return true if started successfully
     */
    bool start();

    /**
     * Stop the receiver (graceful shutdown).
     * Blocks until receiver thread exits.
     */
    void stop();

    /**
     * Check if receiver is running.
     */
    bool is_running() const { return running_.load(); }

    // ========================================
    // Configuration
    // ========================================

    /**
     * Set thread priority (must call before start).
     * @param priority SCHED_FIFO priority (1-99)
     */
    void set_thread_priority(int priority) { thread_priority_ = priority; }

    /**
     * Set logging callback.
     */
    void set_log_callback(LogCallback cb) { log_callback_ = std::move(cb); }

    // ========================================
    // Statistics
    // ========================================

    const ReceiverStats& stats() const { return stats_; }

    /**
     * Get stats as tuple (for structured bindings in main).
     * @return tuple of (received, dropped, invalid)
     */
    std::tuple<uint64_t, uint64_t, uint64_t> get_stats() const {
        return {
            stats_.packets_received.load(),
            stats_.packets_dropped.load(),
            stats_.packets_invalid.load()
        };
    }

private:
    // ========================================
    // Internal Methods
    // ========================================

    /**
     * Main receive loop - runs on dedicated thread.
     */
    void receive_loop();

    /**
     * Create and bind UDP socket.
     * @return Socket file descriptor, or -1 on error
     */
    int create_socket();

    /**
     * Set real-time thread priority.
     * Requires CAP_SYS_NICE capability on Linux.
     */
    void configure_thread_priority();

    /**
     * Log a message via callback (if set).
     */
    void log(const std::string& message);

    // ========================================
    // Data Members
    // ========================================

    std::string bind_address_;
    int port_;
    int socket_fd_;
    int thread_priority_;
    std::atomic<bool> running_;
    std::thread receiver_thread_;
    std::shared_ptr<PacketQueue> queue_;
    ReceiverStats stats_;
    LogCallback log_callback_;
};

} // namespace skyguard
