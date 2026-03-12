#pragma once

/**
 * Logger - Thread-Safe Async Logging
 *
 * Non-blocking logging to avoid impacting real-time performance.
 * Logs to stdout (for journalctl capture) and optionally to file.
 */

#include "types.hpp"
#include <string>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace skyguard {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

/**
 * Get string representation of log level.
 */
inline const char* level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        default:              return "?????";
    }
}

/**
 * Logger class - singleton pattern
 */
class Logger {
public:
    /**
     * Get singleton instance.
     */
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    // Delete copy/move
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * Start the logging thread.
     */
    void start() {
        if (running_.load()) return;

        running_.store(true);
        log_thread_ = std::thread(&Logger::log_loop, this);
    }

    /**
     * Stop logging (flushes pending logs).
     */
    void stop() {
        if (!running_.load()) return;

        running_.store(false);
        cv_.notify_one();

        if (log_thread_.joinable()) {
            log_thread_.join();
        }

        // Flush any remaining
        flush_queue();

        if (file_.is_open()) {
            file_.close();
        }
    }

    /**
     * Set minimum log level.
     */
    void set_level(LogLevel level) {
        min_level_ = level;
    }

    /**
     * Set log file path.
     */
    void set_file(const std::string& path) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (file_.is_open()) {
            file_.close();
        }
        file_.open(path, std::ios::app);
    }

    /**
     * Log a message (non-blocking).
     */
    void log(LogLevel level, const std::string& component,
             const std::string& message) {
        if (level < min_level_) {
            return;
        }

        // Format: [TIMESTAMP] [LEVEL] [COMPONENT] message
        std::ostringstream oss;

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count() % 1000;

        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms
            << " [" << level_to_string(level) << "]"
            << " [" << component << "] "
            << message;

        // Queue for async output
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            log_queue_.push(oss.str());
        }
        cv_.notify_one();
    }

private:
    Logger()
        : min_level_(LogLevel::INFO)
        , running_(false)
    {}

    ~Logger() {
        stop();
    }

    void log_loop() {
        while (running_.load() || !log_queue_.empty()) {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !log_queue_.empty() || !running_.load();
            });

            // Process all queued messages
            while (!log_queue_.empty()) {
                std::string msg = std::move(log_queue_.front());
                log_queue_.pop();
                lock.unlock();

                // Output to stdout
                std::cout << msg << std::endl;

                // Output to file if configured
                {
                    std::lock_guard<std::mutex> flock(file_mutex_);
                    if (file_.is_open()) {
                        file_ << msg << std::endl;
                    }
                }

                lock.lock();
            }
        }
    }

    void flush_queue() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!log_queue_.empty()) {
            std::cout << log_queue_.front() << std::endl;
            log_queue_.pop();
        }
    }

    LogLevel min_level_;
    std::atomic<bool> running_;
    std::thread log_thread_;

    std::queue<std::string> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;

    std::ofstream file_;
    std::mutex file_mutex_;
};

// Convenience macros
#define LOG_DEBUG(component, msg) \
    skyguard::Logger::instance().log(skyguard::LogLevel::DEBUG, component, msg)
#define LOG_INFO(component, msg) \
    skyguard::Logger::instance().log(skyguard::LogLevel::INFO, component, msg)
#define LOG_WARN(component, msg) \
    skyguard::Logger::instance().log(skyguard::LogLevel::WARN, component, msg)
#define LOG_ERROR(component, msg) \
    skyguard::Logger::instance().log(skyguard::LogLevel::ERROR, component, msg)

} // namespace skyguard
