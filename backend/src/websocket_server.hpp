#pragma once

/**
 * WebSocketServer - Real-Time Dashboard Communication
 *
 * Provides WebSocket endpoint for the React dashboard to receive:
 * - Live flight positions
 * - Conflict alerts
 * - System metrics
 *
 * For interview: "This is similar to how I handle real-time updates
 * in StashDeck - pushing state changes to connected clients rather
 * than having them poll. The key is efficient JSON serialization
 * and only sending deltas when possible."
 *
 * Design:
 * - Single-threaded event loop (using poll/select)
 * - JSON messages over WebSocket
 * - Broadcast to all connected clients
 * - Heartbeat/ping to detect stale connections
 */

#include "types.hpp"
#include "flight_packet.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>

namespace skyguard {

// Forward declarations
struct ClientConnection;

/**
 * Message types sent to dashboard
 */
enum class MessageType {
    FLIGHTS_UPDATE,     // Array of current flight positions
    CONFLICT_ALERT,     // New or updated conflict
    CONFLICT_RESOLVED,  // Conflict has been resolved
    METRICS,            // System metrics update
    HEARTBEAT           // Keep-alive ping
};

/**
 * WebSocket server for dashboard communication.
 *
 * NOTE: This is a simplified implementation for demonstration.
 * In production, I'd use a mature library like:
 * - libwebsockets
 * - uWebSockets
 * - Boost.Beast
 *
 * For interview: "I'm showing the architecture here. In production,
 * I'd use uWebSockets for performance - it handles the WebSocket
 * protocol complexity and scales well."
 */
class WebSocketServer {
public:
    using LogCallback = std::function<void(const std::string&)>;

    /**
     * Construct server.
     *
     * @param port TCP port to listen on
     */
    explicit WebSocketServer(int port);

    /**
     * Destructor - graceful shutdown.
     */
    ~WebSocketServer();

    // Non-copyable
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // ========================================
    // Lifecycle
    // ========================================

    /**
     * Start the server.
     *
     * @return true if started successfully
     */
    bool start();

    /**
     * Stop the server.
     */
    void stop();

    bool is_running() const { return running_.load(); }

    // ========================================
    // Broadcasting
    // ========================================

    /**
     * Broadcast flight positions to all clients.
     *
     * @param flights Current flight states
     */
    void broadcast_flights(const std::vector<FlightState>& flights);

    /**
     * Broadcast conflict alert.
     *
     * @param alert The conflict alert
     */
    void broadcast_alert(const ConflictAlert& alert);

    /**
     * Broadcast conflict resolution.
     *
     * @param flight1 First flight ID
     * @param flight2 Second flight ID
     */
    void broadcast_resolution(FlightId flight1, FlightId flight2);

    /**
     * Broadcast system metrics.
     *
     * @param metrics Current metrics
     */
    void broadcast_metrics(const SystemMetrics& metrics);

    // ========================================
    // Status
    // ========================================

    /**
     * Get number of connected clients.
     */
    size_t client_count() const;

    /**
     * Set log callback.
     */
    void set_log_callback(LogCallback cb) { log_callback_ = std::move(cb); }

private:
    // ========================================
    // Internal Methods
    // ========================================

    /**
     * Server main loop.
     */
    void server_loop();

    /**
     * Accept new connection.
     */
    void accept_connection();

    /**
     * Handle WebSocket handshake.
     */
    bool handle_handshake(int client_fd);

    /**
     * Process incoming data from client.
     */
    void process_client_data(int client_fd);

    /**
     * Send data to client.
     */
    bool send_to_client(int client_fd, const std::string& data);

    /**
     * Broadcast to all clients.
     */
    void broadcast(const std::string& json);

    /**
     * Remove disconnected client.
     */
    void remove_client(int client_fd);

    /**
     * Send heartbeat to all clients.
     */
    void send_heartbeats();

    /**
     * Prune stale connections.
     */
    void prune_stale_connections();

    void log(const std::string& message);

    // ========================================
    // JSON Serialization
    // ========================================
    // Simple JSON builders - in production I'd use nlohmann/json or rapidjson

    std::string flights_to_json(const std::vector<FlightState>& flights);
    std::string alert_to_json(const ConflictAlert& alert);
    std::string resolution_to_json(FlightId f1, FlightId f2);
    std::string metrics_to_json(const SystemMetrics& metrics);

    // ========================================
    // WebSocket Protocol
    // ========================================

    /**
     * Encode data as WebSocket frame.
     *
     * WebSocket frame format (simplified):
     * - Byte 0: FIN + opcode (0x81 for text)
     * - Byte 1: Mask bit + payload length
     * - Bytes 2-N: Extended length (if needed)
     * - Payload data
     */
    std::vector<uint8_t> encode_frame(const std::string& data);

    /**
     * Decode WebSocket frame from client.
     *
     * Client frames are masked, we need to unmask.
     */
    std::string decode_frame(const uint8_t* data, size_t len);

    /**
     * Compute WebSocket accept key from client key.
     */
    std::string compute_accept_key(const std::string& client_key);

    // ========================================
    // Data Members
    // ========================================

    int port_;
    int server_fd_;
    std::atomic<bool> running_;
    std::thread server_thread_;

    // Connected clients: fd -> last activity time
    mutable std::mutex clients_mutex_;
    std::unordered_map<int, TimePoint> clients_;

    // Pending data for partial reads
    std::unordered_map<int, std::vector<uint8_t>> client_buffers_;

    LogCallback log_callback_;

    // Heartbeat interval
    static constexpr auto HEARTBEAT_INTERVAL = std::chrono::seconds(30);
    static constexpr auto CLIENT_TIMEOUT = std::chrono::seconds(90);
    TimePoint last_heartbeat_;
};

} // namespace skyguard
