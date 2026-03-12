#pragma once

/**
 * ProcessingEngine - Multi-Threaded Packet Processor
 *
 * Consumes packets from queue, maintains flight state, and runs
 * conflict detection. Similar architecture to StashDeck's analyzer pool.
 *
 * Design:
 * - Worker threads consume from shared queue
 * - Flight state table protected by read-write lock
 * - Conflict detection runs periodically
 * - Alerts pushed to callback
 *
 * For interview: "This is the same worker pool pattern I used in
 * StashDeck for audio analysis - N workers, shared state with
 * read-write lock, event-driven alerts."
 */

#include "types.hpp"
#include "packet_queue.hpp"
#include "flight_packet.hpp"
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <functional>

namespace skyguard {

// Forward declaration
class AIValidator;

/**
 * Worker status for monitoring
 */
struct WorkerStatus {
    size_t id;
    bool is_busy;
    FlightId current_flight;  // 0 if idle
};

/**
 * ProcessingEngine class
 */
class ProcessingEngine {
public:
    // Callback types
    using AlertCallback = std::function<void(const ConflictAlert&)>;
    using LogCallback = std::function<void(const std::string&)>;
    using MetricsCallback = std::function<void(const SystemMetrics&)>;

    /**
     * Construct processing engine.
     *
     * @param num_workers Number of worker threads
     * @param queue Shared packet queue (from receiver)
     */
    ProcessingEngine(size_t num_workers, std::shared_ptr<PacketQueue> queue);

    /**
     * Destructor - graceful shutdown.
     */
    ~ProcessingEngine();

    // Non-copyable
    ProcessingEngine(const ProcessingEngine&) = delete;
    ProcessingEngine& operator=(const ProcessingEngine&) = delete;

    // ========================================
    // Lifecycle
    // ========================================

    /**
     * Start all worker threads.
     */
    void start();

    /**
     * Stop all workers (graceful shutdown).
     */
    void stop();

    bool is_running() const { return running_.load(); }

    // ========================================
    // Callbacks
    // ========================================

    void set_alert_callback(AlertCallback cb) { alert_callback_ = std::move(cb); }
    void set_log_callback(LogCallback cb) { log_callback_ = std::move(cb); }
    void set_metrics_callback(MetricsCallback cb) { metrics_callback_ = std::move(cb); }

    // ========================================
    // Status & Metrics
    // ========================================

    /**
     * Get current worker statuses.
     */
    std::vector<WorkerStatus> get_worker_status() const;

    /**
     * Get current system metrics.
     */
    SystemMetrics get_metrics() const;

    /**
     * Get all current flight states (for dashboard).
     */
    std::vector<FlightState> get_all_flights() const;

    /**
     * Get active conflicts.
     */
    std::vector<ConflictAlert> get_active_conflicts() const;

private:
    // ========================================
    // Internal Methods
    // ========================================

    /**
     * Worker thread main loop.
     */
    void worker_loop(size_t worker_id);

    /**
     * Process a single packet.
     */
    void process_packet(const FlightPacket& packet, size_t worker_id);

    /**
     * Run conflict detection on all flight pairs.
     * Called periodically from one worker.
     */
    void run_conflict_detection();

    /**
     * Prune stale flights from state table.
     */
    void prune_stale_flights();

    /**
     * Emit metrics periodically.
     */
    void emit_metrics();

    void log(const std::string& message);

    // ========================================
    // Data Members
    // ========================================

    size_t num_workers_;
    std::shared_ptr<PacketQueue> queue_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_;

    // Flight state table (protected by read-write lock)
    std::unordered_map<FlightId, FlightState> flight_table_;
    mutable std::shared_mutex table_mutex_;

    // Worker status (each worker updates its own slot)
    mutable std::mutex worker_status_mutex_;
    std::vector<WorkerStatus> worker_status_;

    // Active conflicts
    mutable std::mutex conflicts_mutex_;
    std::unordered_map<uint64_t, ConflictAlert> active_conflicts_;

    // Conflict detection throttle
    std::atomic<uint64_t> packets_since_conflict_check_{0};
    static constexpr uint64_t CONFLICT_CHECK_INTERVAL = 100;  // Every 100 packets

    // AI Validator (conflict detection logic)
    std::unique_ptr<AIValidator> validator_;

    // Callbacks
    AlertCallback alert_callback_;
    LogCallback log_callback_;
    MetricsCallback metrics_callback_;

    // Metrics
    TimePoint start_time_;
    std::atomic<uint64_t> total_packets_processed_{0};
};

} // namespace skyguard
