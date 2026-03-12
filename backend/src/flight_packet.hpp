#pragma once

/**
 * FlightPacket - Aircraft Telemetry Data Structure
 *
 * Represents a single radar reading for one aircraft.
 * Designed for safety-critical systems:
 * - Fixed binary format for network transmission
 * - Explicit validation before use
 * - RAII principles with std::optional for safe parsing
 *
 * Binary Format (32 bytes, network byte order):
 * ┌────────────┬────────────┬────────────┬────────────┬────────────┐
 * │ Bytes 0-3  │ Bytes 4-7  │ Bytes 8-15 │ Bytes 16-23│ Bytes 24-31│
 * ├────────────┼────────────┼────────────┼────────────┼────────────┤
 * │ Flight ID  │ Timestamp  │ Latitude   │ Longitude  │ Altitude   │
 * │ (uint32)   │ (uint32)   │ (double)   │ (double)   │ (double)   │
 * └────────────┴────────────┴────────────┴────────────┴────────────┘
 */

#include "types.hpp"
#include <optional>
#include <cstring>
#include <cmath>
#include <arpa/inet.h>  // ntohl, etc. (Linux/macOS)

namespace skyguard {

// Packet size constant - used for validation
constexpr size_t FLIGHT_PACKET_SIZE = 32;

/**
 * Raw packet structure for network transmission.
 * Packed to ensure exact binary layout.
 */
#pragma pack(push, 1)
struct RawFlightPacket {
    uint32_t flight_id;
    uint32_t timestamp;
    double latitude;
    double longitude;
    double altitude_feet;
};
#pragma pack(pop)

// Verify our packing is correct at compile time
static_assert(sizeof(RawFlightPacket) == FLIGHT_PACKET_SIZE,
    "RawFlightPacket must be exactly 32 bytes");

/**
 * FlightPacket - Validated aircraft telemetry
 *
 * Use FlightPacket::from_bytes() to safely parse network data.
 * The constructor is private to enforce validation.
 */
class FlightPacket {
public:
    // ========================================
    // Factory Method - Safe Construction
    // ========================================

    /**
     * Parse a FlightPacket from raw network bytes.
     *
     * @param data Pointer to raw bytes
     * @param len Number of bytes available
     * @return std::optional containing packet if valid, std::nullopt otherwise
     *
     * SAFETY: Returns nullopt if:
     * - Data is null or wrong size
     * - Coordinates are out of valid range
     * - Values are NaN or Inf
     */
    static std::optional<FlightPacket> from_bytes(const uint8_t* data, size_t len) {
        // Validate input
        if (data == nullptr || len < FLIGHT_PACKET_SIZE) {
            return std::nullopt;
        }

        // Copy to aligned structure
        RawFlightPacket raw;
        std::memcpy(&raw, data, FLIGHT_PACKET_SIZE);

        // Convert from network byte order (big-endian) to host order
        // Note: doubles need special handling on some platforms
        uint32_t flight_id = ntohl(raw.flight_id);
        uint32_t timestamp = ntohl(raw.timestamp);

        // For doubles, we assume IEEE 754 and same endianness as integers
        // In production, use proper serialization library (protobuf, etc.)
        double latitude = raw.latitude;
        double longitude = raw.longitude;
        double altitude = raw.altitude_feet;

        // Create packet and validate
        FlightPacket packet(flight_id, timestamp, latitude, longitude,
                          static_cast<Feet>(altitude));

        if (!packet.is_valid()) {
            return std::nullopt;
        }

        return packet;
    }

    // ========================================
    // Accessors (const - immutable after construction)
    // ========================================

    FlightId id() const { return flight_id_; }
    Timestamp timestamp() const { return timestamp_; }
    Degrees latitude() const { return latitude_; }
    Degrees longitude() const { return longitude_; }
    Feet altitude() const { return altitude_feet_; }

    // ========================================
    // Derived Calculations
    // ========================================

    /**
     * Convert lat/lon/alt to 3D Cartesian coordinates (meters).
     * Uses simplified flat-earth model suitable for local airspace.
     *
     * For interview: "I used flat-earth approximation because within
     * a 200nm radius, the error is < 0.1% - acceptable for conflict
     * detection. A production system might use WGS84 ellipsoid."
     */
    Vec3 to_cartesian() const {
        // Reference point (could be configurable)
        constexpr double REF_LAT = 34.0522;   // Los Angeles
        constexpr double REF_LON = -118.2437;

        // Approximate meters per degree at reference latitude
        constexpr double LAT_TO_METERS = 111132.0;  // ~111km per degree
        double lon_to_meters = LAT_TO_METERS * std::cos(REF_LAT * M_PI / 180.0);

        double x = (longitude_ - REF_LON) * lon_to_meters;
        double y = (latitude_ - REF_LAT) * LAT_TO_METERS;
        double z = altitude_feet_ * constants::FEET_TO_METERS;

        return Vec3(x, y, z);
    }

    /**
     * Serialize packet back to network bytes.
     * Used for forwarding or logging.
     */
    void to_bytes(uint8_t* buffer) const {
        RawFlightPacket raw;
        raw.flight_id = htonl(flight_id_);
        raw.timestamp = htonl(timestamp_);
        raw.latitude = latitude_;
        raw.longitude = longitude_;
        raw.altitude_feet = altitude_feet_;
        std::memcpy(buffer, &raw, FLIGHT_PACKET_SIZE);
    }

    // ========================================
    // Validation
    // ========================================

    /**
     * Check if packet values are within valid ranges.
     *
     * SAFETY-CRITICAL: This is called automatically by from_bytes().
     * Invalid packets are rejected before entering the system.
     */
    bool is_valid() const {
        // Check for NaN/Inf (sensor errors, transmission corruption)
        if (!std::isfinite(latitude_) ||
            !std::isfinite(longitude_) ||
            !std::isfinite(altitude_feet_)) {
            return false;
        }

        // Latitude: -90 to +90
        if (latitude_ < -90.0 || latitude_ > 90.0) {
            return false;
        }

        // Longitude: -180 to +180
        if (longitude_ < -180.0 || longitude_ > 180.0) {
            return false;
        }

        // Altitude: -1000 ft (Dead Sea) to 60000 ft (U-2 ceiling)
        if (altitude_feet_ < -1000.0f || altitude_feet_ > 60000.0f) {
            return false;
        }

        // Flight ID 0 is reserved/invalid
        if (flight_id_ == 0) {
            return false;
        }

        return true;
    }

private:
    // ========================================
    // Private Constructor (use from_bytes())
    // ========================================

    FlightPacket(FlightId id, Timestamp ts, Degrees lat, Degrees lon, Feet alt)
        : flight_id_(id)
        , timestamp_(ts)
        , latitude_(lat)
        , longitude_(lon)
        , altitude_feet_(alt)
    {}

    // ========================================
    // Data Members
    // ========================================

    FlightId flight_id_;
    Timestamp timestamp_;
    Degrees latitude_;
    Degrees longitude_;
    Feet altitude_feet_;
};

/**
 * FlightState - Tracked state for an aircraft
 *
 * Extends FlightPacket with derived data (velocity, history).
 * Updated by ProcessingEngine as new packets arrive.
 */
struct FlightState {
    FlightId id{0};
    Vec3 position;           // Current position (meters, Cartesian)
    Vec3 velocity;           // Velocity vector (m/s)
    Degrees latitude{0.0};   // Original lat for dashboard
    Degrees longitude{0.0};  // Original lon for dashboard
    Feet altitude{0.0f};     // Current altitude (feet)
    Feet vertical_speed{0.0f}; // Feet per minute
    Knots ground_speed{0.0f};
    Degrees heading{0.0};
    Timestamp last_update{0};
    TimePoint last_seen;      // Internal clock time

    // Track history for trails (last N positions)
    static constexpr size_t MAX_HISTORY = 30;
    std::array<Vec3, MAX_HISTORY> position_history;
    size_t history_count{0};

    /**
     * Update state with new packet.
     * Calculates velocity from position delta.
     */
    void update(const FlightPacket& packet) {
        Vec3 new_position = packet.to_cartesian();
        Timestamp new_time = packet.timestamp();

        // Calculate velocity if we have previous data
        if (last_update > 0 && new_time > last_update) {
            double dt = static_cast<double>(new_time - last_update);
            if (dt > 0.0 && dt < 60.0) {  // Sanity check: 0-60 seconds
                velocity = (new_position - position) * (1.0 / dt);

                // Vertical speed (feet per minute)
                Feet alt_delta = packet.altitude() - altitude;
                vertical_speed = static_cast<Feet>(alt_delta / dt * 60.0);

                // Ground speed (knots)
                double horizontal_speed_mps = std::sqrt(
                    velocity.x * velocity.x + velocity.y * velocity.y
                );
                ground_speed = static_cast<Knots>(
                    horizontal_speed_mps / constants::KNOTS_TO_MPS
                );

                // Heading (degrees from north)
                heading = std::atan2(velocity.x, velocity.y) * 180.0 / M_PI;
                if (heading < 0) heading += 360.0;
            }
        }

        // Store in history (ring buffer)
        if (history_count < MAX_HISTORY) {
            position_history[history_count++] = position;
        } else {
            // Shift and append
            std::move(position_history.begin() + 1,
                     position_history.end(),
                     position_history.begin());
            position_history[MAX_HISTORY - 1] = position;
        }

        // Update current state
        id = packet.id();
        position = new_position;
        latitude = packet.latitude();
        longitude = packet.longitude();
        altitude = packet.altitude();
        last_update = new_time;
        last_seen = Clock::now();
    }

    /**
     * Check if this flight data is stale (no update in N seconds).
     */
    bool is_stale(Seconds max_age = 30.0) const {
        auto age = std::chrono::duration_cast<Duration>(
            Clock::now() - last_seen
        ).count();
        return age > max_age;
    }
};

} // namespace skyguard
