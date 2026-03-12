/**
 * SkyGuard - Flight Data Monitoring & Alerting System
 *
 * Main entry point demonstrating:
 * - Signal handling for graceful shutdown (SIGTERM, SIGINT)
 * - Component initialization and lifecycle management
 * - Real-time system architecture
 *
 * For interview: "This follows the same pattern I use in production
 * systems - clean initialization order, signal handling for graceful
 * shutdown, and clear separation between components."
 */

#include "logger.hpp"
#include "packet_queue.hpp"
#include "radar_receiver.hpp"
#include "processing_engine.hpp"
#include "websocket_server.hpp"
#include <csignal>
#include <iostream>
#include <atomic>
#include <thread>
#include <unistd.h>

using namespace skyguard;

// ============================================
// Global Shutdown Flag
// ============================================

// Atomic flag for signal-safe shutdown
// Using sig_atomic_t wrapped in atomic for extra safety
static std::atomic<bool> g_shutdown_requested{false};

/**
 * Signal handler for graceful shutdown.
 *
 * For interview: "Signal handlers must be async-signal-safe.
 * I only set an atomic flag here - all actual cleanup happens
 * in the main thread. This avoids deadlocks from calling
 * non-reentrant functions in signal context."
 */
void signal_handler(int signum) {
    (void)signum;  // Suppress unused parameter warning
    // Only set the flag - don't do any complex work
    g_shutdown_requested.store(true);

    // Log signal received (write is async-signal-safe)
    const char* msg = "\n[Signal] Shutdown requested\n";
    // Using write() directly as it's async-signal-safe
    (void)write(STDOUT_FILENO, msg, 29);
}

/**
 * Install signal handlers.
 */
void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // No SA_RESTART - we want blocking calls to return

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
}

// ============================================
// Configuration
// ============================================

struct Config {
    // Network
    std::string bind_address = "0.0.0.0";
    int radar_port = 5000;
    int websocket_port = 8080;

    // Processing
    size_t num_workers = 4;
    size_t queue_capacity = 10000;

    // Logging
    LogLevel log_level = LogLevel::INFO;
    std::string log_file = "";  // Empty = stdout only
};

/**
 * Parse command line arguments.
 *
 * For interview: "In production, I'd use a proper argument parser
 * like CLI11 or Boost.Program_options. For this demo, I'm keeping
 * it simple to focus on the core architecture."
 */
Config parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--port" && i + 1 < argc) {
            config.radar_port = std::stoi(argv[++i]);
        }
        else if (arg == "--workers" && i + 1 < argc) {
            config.num_workers = std::stoul(argv[++i]);
        }
        else if (arg == "--debug") {
            config.log_level = LogLevel::DEBUG;
        }
        else if (arg == "--log-file" && i + 1 < argc) {
            config.log_file = argv[++i];
        }
        else if (arg == "--help" || arg == "-h") {
            std::cout << "SkyGuard - Flight Data Monitoring System\n"
                      << "\n"
                      << "Usage: skyguard [options]\n"
                      << "\n"
                      << "Options:\n"
                      << "  --port N        Radar UDP port (default: 5000)\n"
                      << "  --workers N     Number of processing workers (default: 4)\n"
                      << "  --debug         Enable debug logging\n"
                      << "  --log-file F    Write logs to file\n"
                      << "  --help, -h      Show this help\n"
                      << std::endl;
            std::exit(0);
        }
    }

    return config;
}

// ============================================
// Main
// ============================================

int main(int argc, char* argv[]) {
    // Parse configuration
    Config config = parse_args(argc, argv);

    // ----------------------------------------
    // 1. Initialize Logger (first, so other components can log)
    // ----------------------------------------
    Logger& logger = Logger::instance();
    logger.set_level(config.log_level);
    if (!config.log_file.empty()) {
        logger.set_file(config.log_file);
    }
    logger.start();

    LOG_INFO("Main", "SkyGuard starting...");
    LOG_INFO("Main", "Configuration:");

    std::ostringstream config_msg;
    config_msg << "  Radar port: " << config.radar_port;
    LOG_INFO("Main", config_msg.str());

    config_msg.str("");
    config_msg << "  Workers: " << config.num_workers;
    LOG_INFO("Main", config_msg.str());

    config_msg.str("");
    config_msg << "  Queue capacity: " << config.queue_capacity;
    LOG_INFO("Main", config_msg.str());

    // ----------------------------------------
    // 2. Set up signal handlers
    // ----------------------------------------
    setup_signal_handlers();
    LOG_DEBUG("Main", "Signal handlers installed");

    // ----------------------------------------
    // 3. Create shared packet queue
    // ----------------------------------------
    // For interview: "The queue decouples the receiver from processors.
    // This is a producer-consumer pattern - the receiver pushes packets,
    // worker threads pop them. This prevents a slow processor from
    // blocking packet reception."
    auto packet_queue = std::make_shared<PacketQueue>(config.queue_capacity);
    LOG_DEBUG("Main", "Packet queue created");

    // ----------------------------------------
    // 4. Create and start processing engine
    // ----------------------------------------
    // Start processing engine FIRST so workers are ready to consume
    ProcessingEngine engine(config.num_workers, packet_queue);

    // Set up callbacks
    engine.set_log_callback([](const std::string& msg) {
        LOG_INFO("Engine", msg);
    });

    // Note: alert callback set after WebSocket server creation

    engine.set_metrics_callback([](const SystemMetrics& metrics) {
        std::ostringstream oss;
        oss << "Metrics: "
            << metrics.active_flights << " flights, "
            << metrics.active_conflicts << " conflicts, "
            << metrics.packets_per_second << " pkt/s, "
            << metrics.workers_busy << "/" << metrics.workers_total << " workers busy";
        LOG_DEBUG("Metrics", oss.str());
    });

    engine.start();
    LOG_INFO("Main", "Processing engine started");

    // ----------------------------------------
    // 5. Create and start WebSocket server (for dashboard)
    // ----------------------------------------
    WebSocketServer ws_server(config.websocket_port);

    ws_server.set_log_callback([](const std::string& msg) {
        LOG_INFO("WebSocket", msg);
    });

    // Connect engine alerts to WebSocket broadcast
    engine.set_alert_callback([&ws_server](const ConflictAlert& alert) {
        std::ostringstream oss;
        oss << "CONFLICT ALERT: " << alert.flight1 << " <-> " << alert.flight2
            << " | CPA in " << static_cast<int>(alert.time_to_cpa) << "s"
            << " | Sep: " << std::fixed << std::setprecision(1) << alert.min_separation << "nm"
            << " | " << (alert.severity == AlertSeverity::CRITICAL ? "CRITICAL" : "WARNING");
        LOG_WARN("Alert", oss.str());

        // Broadcast to dashboard
        ws_server.broadcast_alert(alert);
    });

    if (!ws_server.start()) {
        LOG_WARN("Main", "Failed to start WebSocket server (dashboard unavailable)");
    } else {
        std::ostringstream ws_msg;
        ws_msg << "WebSocket server started on port " << config.websocket_port;
        LOG_INFO("Main", ws_msg.str());
    }

    // ----------------------------------------
    // 7. Create and start radar receiver
    // ----------------------------------------
    RadarReceiver receiver(config.bind_address, config.radar_port, packet_queue);

    receiver.set_log_callback([](const std::string& msg) {
        LOG_INFO("Receiver", msg);
    });

    if (!receiver.start()) {
        LOG_ERROR("Main", "Failed to start radar receiver");
        engine.stop();
        logger.stop();
        return 1;
    }
    LOG_INFO("Main", "Radar receiver started");

    // ----------------------------------------
    // 8. Main loop - wait for shutdown
    // ----------------------------------------
    LOG_INFO("Main", "SkyGuard running. Press Ctrl+C to stop.");

    // Periodic updates
    auto last_status = Clock::now();
    auto last_dashboard_update = Clock::now();
    constexpr auto STATUS_INTERVAL = std::chrono::seconds(30);
    constexpr auto DASHBOARD_UPDATE_INTERVAL = std::chrono::milliseconds(500);

    while (!g_shutdown_requested.load()) {
        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto now = Clock::now();

        // Broadcast flight positions to dashboard
        if (ws_server.is_running() && ws_server.client_count() > 0) {
            if (now - last_dashboard_update >= DASHBOARD_UPDATE_INTERVAL) {
                ws_server.broadcast_flights(engine.get_all_flights());
                ws_server.broadcast_metrics(engine.get_metrics());
                last_dashboard_update = now;
            }
        }

        // Periodic status report
        if (now - last_status >= STATUS_INTERVAL) {
            SystemMetrics metrics = engine.get_metrics();

            std::ostringstream oss;
            oss << "Status: " << metrics.active_flights << " flights tracked, "
                << metrics.active_conflicts << " active conflicts, "
                << "uptime " << static_cast<int>(metrics.uptime_seconds) << "s";
            LOG_INFO("Main", oss.str());

            // Receiver stats
            auto [received, dropped, invalid] = receiver.get_stats();
            oss.str("");
            oss << "Packets: " << received << " received, "
                << dropped << " dropped, " << invalid << " invalid";
            LOG_INFO("Main", oss.str());

            last_status = now;
        }
    }

    // ----------------------------------------
    // 9. Graceful shutdown (reverse order)
    // ----------------------------------------
    LOG_INFO("Main", "Shutting down...");

    // Stop receiver first (stop producing)
    receiver.stop();
    LOG_INFO("Main", "Radar receiver stopped");

    // Stop WebSocket server
    ws_server.stop();
    LOG_INFO("Main", "WebSocket server stopped");

    // Stop processing engine (stop consuming)
    engine.stop();
    LOG_INFO("Main", "Processing engine stopped");

    // Final stats
    auto [received, dropped, invalid] = receiver.get_stats();
    std::ostringstream final_stats;
    final_stats << "Final stats: " << received << " packets received, "
                << dropped << " dropped, " << invalid << " invalid";
    LOG_INFO("Main", final_stats.str());

    LOG_INFO("Main", "SkyGuard shutdown complete");

    // Stop logger last
    logger.stop();

    return 0;
}
