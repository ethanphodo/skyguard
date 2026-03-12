#pragma once

/**
 * SkyGuard Common Types
 *
 * Shared type definitions used across all components.
 * Designed for safety-critical systems:
 * - Explicit fixed-width types (no implicit sizing)
 * - Clear semantic naming
 * - Compile-time constants where possible
 */

#include <cstdint>
#include <string>
#include <chrono>

namespace skyguard {

// ============================================
// Type Aliases for Clarity
// ============================================

using FlightId = uint32_t;
using Timestamp = uint32_t;  // Unix timestamp (seconds)
using Meters = double;
using Feet = float;
using Knots = float;
using Degrees = double;      // Lat/Lon or heading
using NauticalMiles = double;
using Seconds = double;

// High-resolution clock for internal timing
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = std::chrono::duration<double>;

// ============================================
// Constants
// ============================================

namespace constants {
    // Conversion factors
    constexpr double FEET_TO_METERS = 0.3048;
    constexpr double METERS_TO_FEET = 3.28084;
    constexpr double KNOTS_TO_MPS = 0.514444;   // meters per second
    constexpr double NM_TO_METERS = 1852.0;      // nautical miles to meters

    // Safety thresholds (FAA standards)
    constexpr NauticalMiles MIN_HORIZONTAL_SEPARATION = 5.0;  // 5 nm
    constexpr Feet MIN_VERTICAL_SEPARATION = 1000.0;          // 1000 ft
    constexpr Seconds CONFLICT_LOOKAHEAD = 60.0;              // 60 seconds

    // System limits
    constexpr size_t MAX_TRACKED_FLIGHTS = 1000;
    constexpr size_t MAX_QUEUE_SIZE = 10000;
    constexpr int DEFAULT_UDP_PORT = 4000;
    constexpr int DEFAULT_WS_PORT = 8080;

    // Thread priorities (Linux SCHED_FIFO: 1-99, higher = more priority)
    constexpr int RECEIVER_THREAD_PRIORITY = 50;
    constexpr int WORKER_THREAD_PRIORITY = 25;
    constexpr int LOGGER_THREAD_PRIORITY = 10;
}

// ============================================
// 3D Vector for Position/Velocity
// ============================================

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    // Vector operations
    Vec3 operator+(const Vec3& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }

    Vec3 operator-(const Vec3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    Vec3 operator*(double scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }

    // Dot product
    double dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    // Magnitude squared (avoids sqrt for performance)
    double magnitude_squared() const {
        return x * x + y * y + z * z;
    }

    // Magnitude
    double magnitude() const;  // Defined in cpp to avoid cmath include here
};

// ============================================
// Alert Severity Levels
// ============================================

enum class AlertSeverity {
    INFO,       // Informational
    WARNING,    // Potential issue (e.g., CPA in 45-60s)
    CRITICAL    // Immediate attention (e.g., CPA < 30s)
};

// ============================================
// Conflict Alert Structure
// ============================================

struct ConflictAlert {
    FlightId flight1;
    FlightId flight2;
    Seconds time_to_cpa;           // Time until closest point of approach
    NauticalMiles min_separation;   // Minimum horizontal distance
    Feet altitude_difference;       // Vertical separation at CPA
    AlertSeverity severity;
    TimePoint detected_at;
    std::string recommendation;     // e.g., "FL456 descend to FL340"

    // Unique identifier for this conflict pair (order-independent)
    uint64_t conflict_id() const {
        FlightId a = std::min(flight1, flight2);
        FlightId b = std::max(flight1, flight2);
        return (static_cast<uint64_t>(a) << 32) | b;
    }
};

// ============================================
// System Metrics for Dashboard
// ============================================

struct SystemMetrics {
    size_t packets_per_second{0};
    size_t queue_depth{0};
    size_t active_flights{0};
    size_t active_conflicts{0};
    size_t workers_busy{0};
    size_t workers_total{0};
    double uptime_seconds{0.0};
};

} // namespace skyguard
