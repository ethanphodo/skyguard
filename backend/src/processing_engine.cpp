/**
 * ProcessingEngine Implementation
 *
 * Worker pool for packet processing and conflict detection.
 */

#include "processing_engine.hpp"
#include "ai_validator.hpp"
#include <sstream>
#include <algorithm>

namespace skyguard {

// ============================================
// Constructor / Destructor
// ============================================

ProcessingEngine::ProcessingEngine(size_t num_workers, std::shared_ptr<PacketQueue> queue)
    : num_workers_(num_workers)
    , queue_(std::move(queue))
    , running_(false)
    , validator_(std::make_unique<AIValidator>())
{
    // Pre-allocate worker status
    worker_status_.resize(num_workers_);
    for (size_t i = 0; i < num_workers_; ++i) {
        worker_status_[i] = {i, false, 0};
    }
}

ProcessingEngine::~ProcessingEngine() {
    stop();
}

// ============================================
// Lifecycle
// ============================================

void ProcessingEngine::start() {
    if (running_.load()) {
        return;
    }

    running_.store(true);
    start_time_ = Clock::now();

    // Spawn worker threads
    workers_.reserve(num_workers_);
    for (size_t i = 0; i < num_workers_; ++i) {
        workers_.emplace_back(&ProcessingEngine::worker_loop, this, i);
    }

    std::ostringstream oss;
    oss << "ProcessingEngine started with " << num_workers_ << " workers";
    log(oss.str());
}

void ProcessingEngine::stop() {
    if (!running_.load()) {
        return;
    }

    log("ProcessingEngine stopping...");
    running_.store(false);

    // Wake all workers (queue shutdown will return nullopt)
    queue_->shutdown();

    // Join all threads
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    log("ProcessingEngine stopped");
}

// ============================================
// Worker Loop
// ============================================

void ProcessingEngine::worker_loop(size_t worker_id) {
    std::ostringstream oss;
    oss << "Worker " << worker_id << " started";
    log(oss.str());

    while (running_.load()) {
        // Wait for packet (with timeout for shutdown check)
        auto packet_opt = queue_->pop(std::chrono::milliseconds(100));

        if (!packet_opt.has_value()) {
            // Timeout or shutdown
            continue;
        }

        // Update worker status
        {
            std::lock_guard<std::mutex> lock(worker_status_mutex_);
            worker_status_[worker_id].is_busy = true;
            worker_status_[worker_id].current_flight = packet_opt->id();
        }

        // Process the packet
        process_packet(*packet_opt, worker_id);

        // Update worker status
        {
            std::lock_guard<std::mutex> lock(worker_status_mutex_);
            worker_status_[worker_id].is_busy = false;
            worker_status_[worker_id].current_flight = 0;
        }

        // Increment processed count
        total_packets_processed_.fetch_add(1, std::memory_order_relaxed);

        // Periodically run conflict detection
        uint64_t count = packets_since_conflict_check_.fetch_add(1, std::memory_order_relaxed);
        if (count >= CONFLICT_CHECK_INTERVAL) {
            // Only one worker should run conflict detection
            uint64_t expected = count + 1;
            if (packets_since_conflict_check_.compare_exchange_strong(
                    expected, 0, std::memory_order_acq_rel)) {
                run_conflict_detection();
                prune_stale_flights();
                emit_metrics();
            }
        }
    }

    oss.str("");
    oss << "Worker " << worker_id << " exited";
    log(oss.str());
}

// ============================================
// Packet Processing
// ============================================

void ProcessingEngine::process_packet(const FlightPacket& packet, size_t /*worker_id*/) {
    FlightId id = packet.id();

    // Update flight state table
    // Using write lock since we're modifying
    {
        std::unique_lock<std::shared_mutex> lock(table_mutex_);

        auto it = flight_table_.find(id);
        if (it == flight_table_.end()) {
            // New flight - insert
            FlightState state;
            state.update(packet);
            flight_table_.emplace(id, std::move(state));
        } else {
            // Existing flight - update
            it->second.update(packet);
        }
    }
}

// ============================================
// Conflict Detection
// ============================================

void ProcessingEngine::run_conflict_detection() {
    // Get snapshot of current flights (read lock)
    std::vector<FlightState> flights;
    {
        std::shared_lock<std::shared_mutex> lock(table_mutex_);
        flights.reserve(flight_table_.size());
        for (const auto& [id, state] : flight_table_) {
            if (!state.is_stale()) {
                flights.push_back(state);
            }
        }
    }

    if (flights.size() < 2) {
        return;  // Need at least 2 flights to have a conflict
    }

    // Check all pairs (O(n²) - acceptable for ~100 flights)
    // For interview: "In production with 1000+ flights, I'd use
    // spatial partitioning (octree/grid) to reduce to O(n log n)"
    std::vector<ConflictAlert> new_alerts;

    for (size_t i = 0; i < flights.size(); ++i) {
        for (size_t j = i + 1; j < flights.size(); ++j) {
            auto alert_opt = validator_->check_conflict(flights[i], flights[j]);
            if (alert_opt.has_value()) {
                new_alerts.push_back(std::move(*alert_opt));
            }
        }
    }

    // Update active conflicts
    {
        std::lock_guard<std::mutex> lock(conflicts_mutex_);

        // Mark resolved conflicts
        std::vector<uint64_t> resolved_ids;
        for (const auto& pair : active_conflicts_) {
            uint64_t cid = pair.first;
            bool still_active = std::any_of(new_alerts.begin(), new_alerts.end(),
                [cid](const ConflictAlert& a) {
                    return a.conflict_id() == cid;
                });
            if (!still_active) {
                resolved_ids.push_back(cid);
            }
        }

        // Remove resolved
        for (uint64_t id : resolved_ids) {
            active_conflicts_.erase(id);
            // Could emit "conflict resolved" event here
        }

        // Add/update active conflicts
        for (auto& alert : new_alerts) {
            uint64_t cid = alert.conflict_id();
            auto it = active_conflicts_.find(cid);
            if (it == active_conflicts_.end()) {
                // New conflict - emit alert
                if (alert_callback_) {
                    alert_callback_(alert);
                }
                active_conflicts_.emplace(cid, std::move(alert));
            } else {
                // Update existing
                it->second = std::move(alert);
            }
        }
    }
}

void ProcessingEngine::prune_stale_flights() {
    std::unique_lock<std::shared_mutex> lock(table_mutex_);

    auto it = flight_table_.begin();
    while (it != flight_table_.end()) {
        if (it->second.is_stale(60.0)) {  // 60 seconds
            it = flight_table_.erase(it);
        } else {
            ++it;
        }
    }
}

void ProcessingEngine::emit_metrics() {
    if (!metrics_callback_) {
        return;
    }

    SystemMetrics metrics = get_metrics();
    metrics_callback_(metrics);
}

// ============================================
// Status & Metrics
// ============================================

std::vector<WorkerStatus> ProcessingEngine::get_worker_status() const {
    std::lock_guard<std::mutex> lock(worker_status_mutex_);
    return worker_status_;
}

SystemMetrics ProcessingEngine::get_metrics() const {
    SystemMetrics metrics;

    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<Duration>(now - start_time_);

    metrics.uptime_seconds = elapsed.count();
    metrics.workers_total = num_workers_;

    // Count busy workers
    {
        std::lock_guard<std::mutex> lock(worker_status_mutex_);
        metrics.workers_busy = static_cast<size_t>(std::count_if(
            worker_status_.begin(), worker_status_.end(),
            [](const WorkerStatus& ws) { return ws.is_busy; }
        ));
    }

    // Queue depth
    metrics.queue_depth = queue_->size();

    // Flight count
    {
        std::shared_lock<std::shared_mutex> lock(table_mutex_);
        metrics.active_flights = flight_table_.size();
    }

    // Conflict count
    {
        std::lock_guard<std::mutex> lock(conflicts_mutex_);
        metrics.active_conflicts = active_conflicts_.size();
    }

    // Packets per second (approximate)
    if (elapsed.count() > 0) {
        metrics.packets_per_second = static_cast<size_t>(
            static_cast<double>(total_packets_processed_.load()) / elapsed.count()
        );
    }

    return metrics;
}

std::vector<FlightState> ProcessingEngine::get_all_flights() const {
    std::shared_lock<std::shared_mutex> lock(table_mutex_);

    std::vector<FlightState> result;
    result.reserve(flight_table_.size());
    for (const auto& [id, state] : flight_table_) {
        result.push_back(state);
    }
    return result;
}

std::vector<ConflictAlert> ProcessingEngine::get_active_conflicts() const {
    std::lock_guard<std::mutex> lock(conflicts_mutex_);

    std::vector<ConflictAlert> result;
    result.reserve(active_conflicts_.size());
    for (const auto& [id, alert] : active_conflicts_) {
        result.push_back(alert);
    }
    return result;
}

// ============================================
// Logging
// ============================================

void ProcessingEngine::log(const std::string& message) {
    if (log_callback_) {
        log_callback_("[ProcessingEngine] " + message);
    }
}

} // namespace skyguard
