/**
 * RadarReceiver Implementation
 *
 * UDP socket handling with real-time thread priority.
 */

#include "radar_receiver.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <sstream>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#endif

namespace skyguard {

// ============================================
// Constructor / Destructor
// ============================================

RadarReceiver::RadarReceiver(const std::string& bind_address, int port, std::shared_ptr<PacketQueue> queue)
    : bind_address_(bind_address)
    , port_(port)
    , socket_fd_(-1)
    , thread_priority_(constants::RECEIVER_THREAD_PRIORITY)
    , running_(false)
    , queue_(std::move(queue))
{}

RadarReceiver::~RadarReceiver() {
    stop();
}

// ============================================
// Lifecycle
// ============================================

bool RadarReceiver::start() {
    if (running_.load()) {
        log("Receiver already running");
        return false;
    }

    // Create and bind socket
    socket_fd_ = create_socket();
    if (socket_fd_ < 0) {
        return false;
    }

    // Start receiver thread
    running_.store(true);
    receiver_thread_ = std::thread(&RadarReceiver::receive_loop, this);

    std::ostringstream oss;
    oss << "RadarReceiver started on UDP port " << port_;
    log(oss.str());

    return true;
}

void RadarReceiver::stop() {
    if (!running_.load()) {
        return;
    }

    log("RadarReceiver stopping...");

    // Signal shutdown
    running_.store(false);

    // Close socket to unblock recv()
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }

    // Wait for thread to exit
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }

    log("RadarReceiver stopped");
}

// ============================================
// Socket Setup
// ============================================

int RadarReceiver::create_socket() {
    // Create UDP socket
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        log("Failed to create socket: " + std::string(strerror(errno)));
        return -1;
    }

    // Allow address reuse (helps with quick restarts)
    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        log("Warning: setsockopt(SO_REUSEADDR) failed");
    }

    // Increase receive buffer size for burst handling
    int recv_buf_size = 1024 * 1024;  // 1 MB
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size)) < 0) {
        log("Warning: setsockopt(SO_RCVBUF) failed");
    }

    // Bind to address and port
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    // Parse bind address (use INADDR_ANY if empty or "0.0.0.0")
    if (bind_address_.empty() || bind_address_ == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, bind_address_.c_str(), &addr.sin_addr) != 1) {
            log("Invalid bind address: " + bind_address_);
            ::close(fd);
            return -1;
        }
    }

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        log("Failed to bind to port " + std::to_string(port_) + ": " + strerror(errno));
        ::close(fd);
        return -1;
    }

    return fd;
}

// ============================================
// Thread Priority (Real-Time)
// ============================================

void RadarReceiver::configure_thread_priority() {
#ifdef __linux__
    // Set SCHED_FIFO (real-time) scheduling policy
    // Requires CAP_SYS_NICE capability or root
    struct sched_param param;
    param.sched_priority = thread_priority_;

    int result = pthread_setschedparam(
        pthread_self(),
        SCHED_FIFO,
        &param
    );

    if (result != 0) {
        // Not fatal - we'll run at normal priority
        std::ostringstream oss;
        oss << "Warning: Could not set real-time priority (error " << result << "). "
            << "Run with CAP_SYS_NICE for real-time scheduling.";
        log(oss.str());
    } else {
        std::ostringstream oss;
        oss << "Receiver thread set to SCHED_FIFO priority " << thread_priority_;
        log(oss.str());
    }
#else
    // macOS and others: real-time scheduling works differently
    log("Real-time thread priority not implemented for this platform");
#endif
}

// ============================================
// Main Receive Loop
// ============================================

void RadarReceiver::receive_loop() {
    // Set thread priority first
    configure_thread_priority();

    // Buffer for incoming packets
    // Slightly larger than packet size to detect oversized packets
    constexpr size_t BUFFER_SIZE = 64;
    uint8_t buffer[BUFFER_SIZE];

    // Use poll() to allow timeout-based checking of running_ flag
    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;

    log("Receive loop started");

    while (running_.load()) {
        // Poll with 100ms timeout
        int poll_result = ::poll(&pfd, 1, 100);

        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, retry
            }
            log("poll() error: " + std::string(strerror(errno)));
            break;
        }

        if (poll_result == 0) {
            // Timeout - check running_ flag and loop
            continue;
        }

        // Data available - receive it
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);

        ssize_t recv_len = ::recvfrom(
            socket_fd_,
            buffer,
            BUFFER_SIZE,
            0,
            reinterpret_cast<struct sockaddr*>(&sender_addr),
            &addr_len
        );

        if (recv_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            // Socket closed or other error
            if (running_.load()) {
                log("recvfrom() error: " + std::string(strerror(errno)));
            }
            break;
        }

        // Update statistics
        stats_.packets_received.fetch_add(1, std::memory_order_relaxed);
        stats_.bytes_received.fetch_add(static_cast<uint64_t>(recv_len),
                                        std::memory_order_relaxed);

        // Parse packet
        auto packet = FlightPacket::from_bytes(buffer, static_cast<size_t>(recv_len));

        if (!packet.has_value()) {
            stats_.packets_invalid.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        stats_.packets_valid.fetch_add(1, std::memory_order_relaxed);

        // Enqueue for processing
        if (!queue_->push(std::move(*packet))) {
            stats_.packets_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    log("Receive loop exited");
}

// ============================================
// Logging
// ============================================

void RadarReceiver::log(const std::string& message) {
    if (log_callback_) {
        log_callback_("[RadarReceiver] " + message);
    }
}

} // namespace skyguard
