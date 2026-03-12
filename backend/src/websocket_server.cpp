/**
 * WebSocketServer Implementation
 *
 * Simplified WebSocket server for dashboard communication.
 * In production, would use uWebSockets or Boost.Beast.
 */

#include "websocket_server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

namespace skyguard {

// WebSocket magic GUID for handshake
static const std::string WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// ============================================
// Constructor / Destructor
// ============================================

WebSocketServer::WebSocketServer(int port)
    : port_(port)
    , server_fd_(-1)
    , running_(false)
    , last_heartbeat_(Clock::now())
{}

WebSocketServer::~WebSocketServer() {
    stop();
}

// ============================================
// Lifecycle
// ============================================

bool WebSocketServer::start() {
    if (running_.load()) {
        return true;
    }

    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        log("Failed to create socket");
        return false;
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set non-blocking
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

    // Bind
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        log("Failed to bind to port");
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Listen
    if (listen(server_fd_, 10) < 0) {
        log("Failed to listen");
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_.store(true);
    server_thread_ = std::thread(&WebSocketServer::server_loop, this);

    std::ostringstream oss;
    oss << "WebSocket server listening on port " << port_;
    log(oss.str());

    return true;
}

void WebSocketServer::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto& [fd, _] : clients_) {
            close(fd);
        }
        clients_.clear();
        client_buffers_.clear();
    }

    // Close server socket
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }

    log("WebSocket server stopped");
}

// ============================================
// Server Loop
// ============================================

void WebSocketServer::server_loop() {
    std::vector<struct pollfd> poll_fds;

    while (running_.load()) {
        // Build poll fd list
        poll_fds.clear();

        // Server socket for accepting connections
        poll_fds.push_back({server_fd_, POLLIN, 0});

        // Client sockets for reading data
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (const auto& [fd, _] : clients_) {
                poll_fds.push_back({fd, POLLIN, 0});
            }
        }

        // Poll with timeout
        int ready = poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), 100);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal
            }
            log("Poll error");
            break;
        }

        if (ready == 0) {
            // Timeout - check for heartbeats
            auto now = Clock::now();
            if (now - last_heartbeat_ >= HEARTBEAT_INTERVAL) {
                send_heartbeats();
                prune_stale_connections();
                last_heartbeat_ = now;
            }
            continue;
        }

        // Process events
        for (size_t i = 0; i < poll_fds.size(); ++i) {
            if (poll_fds[i].revents == 0) {
                continue;
            }

            if (poll_fds[i].fd == server_fd_) {
                // New connection
                accept_connection();
            } else {
                // Client data or disconnect
                if (poll_fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    remove_client(poll_fds[i].fd);
                } else if (poll_fds[i].revents & POLLIN) {
                    process_client_data(poll_fds[i].fd);
                }
            }
        }
    }
}

void WebSocketServer::accept_connection() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = accept(server_fd_,
                           reinterpret_cast<struct sockaddr*>(&client_addr),
                           &addr_len);

    if (client_fd < 0) {
        return;
    }

    // Set non-blocking
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // Perform WebSocket handshake
    if (!handle_handshake(client_fd)) {
        close(client_fd);
        return;
    }

    // Add to clients
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_[client_fd] = Clock::now();
        client_buffers_[client_fd] = {};
    }

    std::ostringstream oss;
    oss << "Client connected from " << inet_ntoa(client_addr.sin_addr);
    log(oss.str());
}

bool WebSocketServer::handle_handshake(int client_fd) {
    // Read HTTP request
    char buffer[4096];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        return false;
    }
    buffer[bytes] = '\0';

    std::string request(buffer);

    // Find Sec-WebSocket-Key header
    std::string key_header = "Sec-WebSocket-Key: ";
    auto key_pos = request.find(key_header);
    if (key_pos == std::string::npos) {
        return false;
    }

    auto key_start = key_pos + key_header.length();
    auto key_end = request.find("\r\n", key_start);
    std::string client_key = request.substr(key_start, key_end - key_start);

    // Compute accept key
    std::string accept_key = compute_accept_key(client_key);

    // Send response
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
             << "\r\n";

    std::string resp_str = response.str();
    ssize_t sent = send(client_fd, resp_str.c_str(), resp_str.length(), 0);

    return sent == static_cast<ssize_t>(resp_str.length());
}

std::string WebSocketServer::compute_accept_key(const std::string& client_key) {
    // Concatenate with GUID
    std::string combined = client_key + WS_GUID;

    // SHA-1 hash
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()),
         combined.length(), hash);

    // Base64 encode
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    BIO_write(bio, hash, SHA_DIGEST_LENGTH);
    BIO_flush(bio);

    BUF_MEM* buffer_ptr;
    BIO_get_mem_ptr(bio, &buffer_ptr);

    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(bio);

    return result;
}

void WebSocketServer::process_client_data(int client_fd) {
    uint8_t buffer[4096];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);

    if (bytes <= 0) {
        remove_client(client_fd);
        return;
    }

    // Update activity time
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_[client_fd] = Clock::now();
    }

    // Decode frame
    std::string message = decode_frame(buffer, static_cast<size_t>(bytes));

    // Handle control frames
    if (buffer[0] == 0x88) {
        // Close frame
        remove_client(client_fd);
        return;
    }

    if (buffer[0] == 0x89) {
        // Ping - send pong
        uint8_t pong[2] = {0x8A, 0x00};  // Pong with no payload
        send(client_fd, pong, 2, 0);
        return;
    }

    // Text frames are handled (could parse JSON commands from dashboard)
    // For now, we just acknowledge receipt
}

std::string WebSocketServer::decode_frame(const uint8_t* data, size_t len) {
    if (len < 2) return "";

    // Get payload length
    size_t payload_len = data[1] & 0x7F;
    size_t header_len = 2;

    if (payload_len == 126) {
        if (len < 4) return "";
        payload_len = (static_cast<size_t>(data[2]) << 8) | static_cast<size_t>(data[3]);
        header_len = 4;
    } else if (payload_len == 127) {
        if (len < 10) return "";
        // 64-bit length - not handling for this demo
        return "";
    }

    // Check if masked
    bool masked = data[1] & 0x80;
    size_t mask_offset = header_len;

    if (masked) {
        header_len += 4;
    }

    if (len < header_len + payload_len) return "";

    // Decode payload
    std::string result(payload_len, '\0');
    const uint8_t* payload = data + header_len;

    if (masked) {
        const uint8_t* mask = data + mask_offset;
        for (size_t i = 0; i < payload_len; ++i) {
            result[i] = payload[i] ^ mask[i % 4];
        }
    } else {
        std::memcpy(&result[0], payload, payload_len);
    }

    return result;
}

std::vector<uint8_t> WebSocketServer::encode_frame(const std::string& data) {
    std::vector<uint8_t> frame;

    // FIN + text opcode
    frame.push_back(0x81);

    // Payload length (no mask for server->client)
    if (data.length() < 126) {
        frame.push_back(static_cast<uint8_t>(data.length()));
    } else if (data.length() < 65536) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((data.length() >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(data.length() & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((data.length() >> (i * 8)) & 0xFF));
        }
    }

    // Payload
    frame.insert(frame.end(), data.begin(), data.end());

    return frame;
}

// ============================================
// Broadcasting
// ============================================

void WebSocketServer::broadcast(const std::string& json) {
    auto frame = encode_frame(json);

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& [fd, _] : clients_) {
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    }
}

void WebSocketServer::broadcast_flights(const std::vector<FlightState>& flights) {
    broadcast(flights_to_json(flights));
}

void WebSocketServer::broadcast_alert(const ConflictAlert& alert) {
    broadcast(alert_to_json(alert));
}

void WebSocketServer::broadcast_resolution(FlightId flight1, FlightId flight2) {
    broadcast(resolution_to_json(flight1, flight2));
}

void WebSocketServer::broadcast_metrics(const SystemMetrics& metrics) {
    broadcast(metrics_to_json(metrics));
}

// ============================================
// JSON Serialization
// ============================================

std::string WebSocketServer::flights_to_json(const std::vector<FlightState>& flights) {
    std::ostringstream oss;
    oss << R"({"type":"flights","data":[)";

    for (size_t i = 0; i < flights.size(); ++i) {
        const auto& f = flights[i];
        if (i > 0) oss << ",";
        oss << "{"
            << R"("id":)" << f.id << ","
            << R"("lat":)" << std::fixed << std::setprecision(6) << f.latitude << ","
            << R"("lon":)" << f.longitude << ","
            << R"("alt":)" << std::setprecision(0) << f.altitude << ","
            << R"("hdg":)" << f.heading << ","
            << R"("spd":)" << f.ground_speed
            << "}";
    }

    oss << "]}";
    return oss.str();
}

std::string WebSocketServer::alert_to_json(const ConflictAlert& alert) {
    std::ostringstream oss;
    oss << R"({"type":"alert","data":{)"
        << R"("flight1":)" << alert.flight1 << ","
        << R"("flight2":)" << alert.flight2 << ","
        << R"("timeToCpa":)" << std::fixed << std::setprecision(1) << alert.time_to_cpa << ","
        << R"("minSeparation":)" << alert.min_separation << ","
        << R"("altDiff":)" << std::setprecision(0) << alert.altitude_difference << ","
        << R"("severity":")" << (alert.severity == AlertSeverity::CRITICAL ? "critical" : "warning") << R"(",)"
        << R"("recommendation":")" << alert.recommendation << R"(")"
        << "}}";
    return oss.str();
}

std::string WebSocketServer::resolution_to_json(FlightId f1, FlightId f2) {
    std::ostringstream oss;
    oss << R"({"type":"resolved","data":{)"
        << R"("flight1":)" << f1 << ","
        << R"("flight2":)" << f2
        << "}}";
    return oss.str();
}

std::string WebSocketServer::metrics_to_json(const SystemMetrics& metrics) {
    std::ostringstream oss;
    oss << R"({"type":"metrics","data":{)"
        << R"("uptime":)" << static_cast<int>(metrics.uptime_seconds) << ","
        << R"("activeFlights":)" << metrics.active_flights << ","
        << R"("activeConflicts":)" << metrics.active_conflicts << ","
        << R"("packetsPerSecond":)" << metrics.packets_per_second << ","
        << R"("queueDepth":)" << metrics.queue_depth << ","
        << R"("workersBusy":)" << metrics.workers_busy << ","
        << R"("workersTotal":)" << metrics.workers_total
        << "}}";
    return oss.str();
}

// ============================================
// Connection Management
// ============================================

void WebSocketServer::remove_client(int client_fd) {
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(client_fd);
        client_buffers_.erase(client_fd);
    }
    close(client_fd);
    log("Client disconnected");
}

void WebSocketServer::send_heartbeats() {
    // Send ping to all clients
    uint8_t ping[2] = {0x89, 0x00};

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& [fd, _] : clients_) {
        send(fd, ping, 2, MSG_NOSIGNAL);
    }
}

void WebSocketServer::prune_stale_connections() {
    auto now = Clock::now();
    std::vector<int> stale;

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto& [fd, last_activity] : clients_) {
            if (now - last_activity >= CLIENT_TIMEOUT) {
                stale.push_back(fd);
            }
        }
    }

    for (int fd : stale) {
        remove_client(fd);
        log("Removed stale connection");
    }
}

size_t WebSocketServer::client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

void WebSocketServer::log(const std::string& message) {
    if (log_callback_) {
        log_callback_("[WebSocket] " + message);
    }
}

} // namespace skyguard
