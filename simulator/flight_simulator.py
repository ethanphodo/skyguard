#!/usr/bin/env python3
"""
SkyGuard Flight Simulator

Generates realistic flight data and sends UDP packets to the C++ backend.
Useful for testing and demonstrating the system without real radar data.

For interview: "I built this simulator to test the backend independently.
It generates realistic flight paths with configurable conflict scenarios.
The binary packet format matches exactly what a real radar feed would send."

Usage:
    python flight_simulator.py --host localhost --port 5000 --flights 20
    python flight_simulator.py --conflict  # Create a conflict scenario
"""

import socket
import struct
import time
import math
import random
import argparse
from dataclasses import dataclass, field
from typing import List, Tuple
from enum import Enum


# ============================================
# Constants
# ============================================

# Packet format: 32 bytes
# uint32 flight_id, uint32 timestamp, double lat, double lon, double alt
PACKET_FORMAT = "!IIiii"  # Network byte order, but we'll pack doubles manually
PACKET_SIZE = 32

# Geographic bounds (Los Angeles area for demo)
LAT_MIN, LAT_MAX = 33.5, 34.5
LON_MIN, LON_MAX = -118.8, -117.5

# Altitude bounds (feet)
ALT_MIN, ALT_MAX = 10000, 40000

# Speed bounds (knots)
SPEED_MIN, SPEED_MAX = 250, 550


# ============================================
# Flight Class
# ============================================

class FlightPhase(Enum):
    CRUISE = "cruise"
    CLIMB = "climb"
    DESCEND = "descend"
    TURN = "turn"


@dataclass
class Flight:
    """Represents a simulated aircraft."""

    flight_id: int
    latitude: float
    longitude: float
    altitude: float  # feet
    heading: float   # degrees (0 = north, 90 = east)
    speed: float     # knots
    vertical_speed: float = 0.0  # feet per minute
    phase: FlightPhase = FlightPhase.CRUISE

    # For turns
    target_heading: float = 0.0
    turn_rate: float = 0.0  # degrees per second

    # For altitude changes
    target_altitude: float = 0.0

    def update(self, dt: float) -> None:
        """
        Update flight position based on heading and speed.

        Args:
            dt: Time delta in seconds
        """
        # Handle turns
        if self.phase == FlightPhase.TURN:
            heading_diff = self._normalize_heading(self.target_heading - self.heading)
            if abs(heading_diff) < abs(self.turn_rate * dt):
                self.heading = self.target_heading
                self.phase = FlightPhase.CRUISE
                self.turn_rate = 0.0
            else:
                self.heading = self._normalize_heading(
                    self.heading + self.turn_rate * dt
                )

        # Handle altitude changes
        if self.phase in (FlightPhase.CLIMB, FlightPhase.DESCEND):
            alt_diff = self.target_altitude - self.altitude
            alt_change = self.vertical_speed * dt / 60.0  # Convert fpm to fps

            if abs(alt_diff) < abs(alt_change):
                self.altitude = self.target_altitude
                self.vertical_speed = 0.0
                self.phase = FlightPhase.CRUISE
            else:
                self.altitude += alt_change

        # Update position based on heading and speed
        # Convert knots to degrees per second (approximate)
        speed_mps = self.speed * 0.514444  # knots to m/s

        # Approximate meters per degree at this latitude
        lat_meters = 111132.0
        lon_meters = lat_meters * math.cos(math.radians(self.latitude))

        # Calculate movement
        heading_rad = math.radians(self.heading)
        dx = math.sin(heading_rad) * speed_mps * dt  # East component
        dy = math.cos(heading_rad) * speed_mps * dt  # North component

        # Update position
        self.latitude += dy / lat_meters
        self.longitude += dx / lon_meters

        # Keep within bounds (wrap around for demo)
        if self.latitude < LAT_MIN:
            self.latitude = LAT_MAX
        elif self.latitude > LAT_MAX:
            self.latitude = LAT_MIN

        if self.longitude < LON_MIN:
            self.longitude = LON_MAX
        elif self.longitude > LON_MAX:
            self.longitude = LON_MIN

    def start_turn(self, target_heading: float, rate: float = 3.0) -> None:
        """
        Begin a turn to a new heading.

        Args:
            target_heading: Target heading in degrees
            rate: Turn rate in degrees per second (standard rate = 3)
        """
        self.target_heading = self._normalize_heading(target_heading)
        heading_diff = self._normalize_heading(self.target_heading - self.heading)
        self.turn_rate = rate if heading_diff > 0 else -rate
        self.phase = FlightPhase.TURN

    def start_climb(self, target_alt: float, rate: float = 2000.0) -> None:
        """Begin climb to target altitude."""
        self.target_altitude = target_alt
        self.vertical_speed = rate
        self.phase = FlightPhase.CLIMB

    def start_descend(self, target_alt: float, rate: float = 1500.0) -> None:
        """Begin descent to target altitude."""
        self.target_altitude = target_alt
        self.vertical_speed = -rate
        self.phase = FlightPhase.DESCEND

    @staticmethod
    def _normalize_heading(heading: float) -> float:
        """Normalize heading to 0-360 range."""
        while heading < 0:
            heading += 360
        while heading >= 360:
            heading -= 360
        return heading

    def to_packet(self) -> bytes:
        """
        Serialize to binary packet format.

        Format (32 bytes):
        - uint32: flight_id (network byte order)
        - uint32: timestamp (network byte order)
        - double: latitude (native byte order - C++ does direct memcpy)
        - double: longitude (native byte order)
        - double: altitude (native byte order)
        """
        timestamp = int(time.time())

        # Pack integers as big-endian (network byte order) - C++ uses ntohl
        packet = struct.pack(
            "!II",  # Two unsigned ints, network byte order
            self.flight_id,
            timestamp
        )

        # Pack doubles as native byte order - C++ does direct memcpy
        # without byte swapping, so we must match the host endianness
        packet += struct.pack("=d", self.latitude)   # Native byte order
        packet += struct.pack("=d", self.longitude)
        packet += struct.pack("=d", self.altitude)

        assert len(packet) == PACKET_SIZE, f"Packet size mismatch: {len(packet)}"
        return packet


# ============================================
# Simulator Class
# ============================================

class FlightSimulator:
    """
    Manages multiple simulated flights and sends UDP packets.

    For interview: "The simulator can create specific conflict scenarios
    for testing. I can demonstrate both normal operations and edge cases
    like head-on conflicts or converging flight paths."
    """

    def __init__(
        self,
        host: str = "localhost",
        port: int = 5000,
        update_rate: float = 1.0  # Updates per second
    ):
        self.host = host
        self.port = port
        self.update_interval = 1.0 / update_rate
        self.flights: List[Flight] = []
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.running = False
        self.packets_sent = 0

    def add_flight(self, flight: Flight) -> None:
        """Add a flight to the simulation."""
        self.flights.append(flight)
        print(f"Added flight {flight.flight_id}: "
              f"pos=({flight.latitude:.4f}, {flight.longitude:.4f}), "
              f"alt={flight.altitude:.0f}ft, hdg={flight.heading:.0f}")

    def create_random_flights(self, count: int) -> None:
        """
        Create random flights within the simulation area.
        """
        for i in range(count):
            flight = Flight(
                flight_id=1000 + i,
                latitude=random.uniform(LAT_MIN, LAT_MAX),
                longitude=random.uniform(LON_MIN, LON_MAX),
                altitude=random.randint(ALT_MIN // 1000, ALT_MAX // 1000) * 1000,
                heading=random.uniform(0, 360),
                speed=random.uniform(SPEED_MIN, SPEED_MAX)
            )
            self.add_flight(flight)

    def create_conflict_scenario(self) -> None:
        """
        Create a head-on conflict scenario for testing.

        Two aircraft at same altitude, converging paths.
        Should trigger CRITICAL alert within ~30 seconds.
        """
        print("\n=== Creating Conflict Scenario ===")

        # Center point
        center_lat = (LAT_MIN + LAT_MAX) / 2
        center_lon = (LON_MIN + LON_MAX) / 2

        # Flight 1: Coming from west, heading east
        flight1 = Flight(
            flight_id=9001,
            latitude=center_lat,
            longitude=center_lon - 0.15,  # ~10nm west
            altitude=35000,
            heading=90,  # East
            speed=450
        )

        # Flight 2: Coming from east, heading west
        flight2 = Flight(
            flight_id=9002,
            latitude=center_lat + 0.01,  # Slightly offset for near-miss
            longitude=center_lon + 0.15,  # ~10nm east
            altitude=35000,  # Same altitude = conflict!
            heading=270,  # West
            speed=450
        )

        self.add_flight(flight1)
        self.add_flight(flight2)

        print(f"Conflict scenario: {flight1.flight_id} vs {flight2.flight_id}")
        print(f"Both at FL350, closing at ~900 knots ground speed")
        print("Expected CPA in approximately 40 seconds\n")

    def create_crossing_conflict(self) -> None:
        """
        Create a crossing conflict (90-degree intersection).
        """
        print("\n=== Creating Crossing Conflict ===")

        center_lat = (LAT_MIN + LAT_MAX) / 2
        center_lon = (LON_MIN + LON_MAX) / 2

        # Flight heading north
        flight1 = Flight(
            flight_id=8001,
            latitude=center_lat - 0.1,
            longitude=center_lon,
            altitude=33000,
            heading=0,  # North
            speed=400
        )

        # Flight heading east
        flight2 = Flight(
            flight_id=8002,
            latitude=center_lat,
            longitude=center_lon - 0.1,
            altitude=33000,
            heading=90,  # East
            speed=400
        )

        self.add_flight(flight1)
        self.add_flight(flight2)
        print("Crossing conflict created at FL330\n")

    def run(self, duration: float = None) -> None:
        """
        Run the simulation.

        Args:
            duration: Run for this many seconds (None = indefinitely)
        """
        self.running = True
        start_time = time.time()
        last_update = start_time
        last_status = start_time

        print(f"\nSimulator running - sending to {self.host}:{self.port}")
        print(f"Simulating {len(self.flights)} flights")
        print("Press Ctrl+C to stop\n")

        try:
            while self.running:
                current_time = time.time()

                # Check duration
                if duration and (current_time - start_time) >= duration:
                    break

                # Update flights
                dt = current_time - last_update
                if dt >= self.update_interval:
                    self._update_and_send(dt)
                    last_update = current_time

                # Status update every 5 seconds
                if current_time - last_status >= 5.0:
                    elapsed = current_time - start_time
                    rate = self.packets_sent / elapsed if elapsed > 0 else 0
                    print(f"Status: {self.packets_sent} packets sent "
                          f"({rate:.1f}/s), {len(self.flights)} flights")
                    last_status = current_time

                # Small sleep to prevent busy-waiting
                time.sleep(0.01)

        except KeyboardInterrupt:
            print("\n\nSimulator stopped by user")

        self.running = False
        print(f"Total packets sent: {self.packets_sent}")

    def _update_and_send(self, dt: float) -> None:
        """Update all flights and send packets."""
        for flight in self.flights:
            # Update position
            flight.update(dt)

            # Random maneuvers (5% chance per update)
            if random.random() < 0.05:
                self._random_maneuver(flight)

            # Send packet
            packet = flight.to_packet()
            self.socket.sendto(packet, (self.host, self.port))
            self.packets_sent += 1

    def _random_maneuver(self, flight: Flight) -> None:
        """Apply a random maneuver to keep things interesting."""
        if flight.phase != FlightPhase.CRUISE:
            return  # Already maneuvering

        maneuver = random.choice(["none", "none", "turn", "altitude"])

        if maneuver == "turn":
            # Random heading change (10-30 degrees)
            delta = random.uniform(10, 30) * random.choice([-1, 1])
            flight.start_turn(flight.heading + delta)

        elif maneuver == "altitude":
            # Random altitude change
            current_fl = flight.altitude / 100
            if current_fl > 350:
                # High altitude - maybe descend
                new_alt = random.randint(300, 350) * 100
                flight.start_descend(new_alt)
            elif current_fl < 250:
                # Low altitude - maybe climb
                new_alt = random.randint(250, 350) * 100
                flight.start_climb(new_alt)
            else:
                # Mid altitude - either direction
                delta = random.randint(1, 3) * 1000 * random.choice([-1, 1])
                new_alt = flight.altitude + delta
                if delta > 0:
                    flight.start_climb(new_alt)
                else:
                    flight.start_descend(new_alt)


# ============================================
# Main
# ============================================

def main():
    parser = argparse.ArgumentParser(
        description="SkyGuard Flight Simulator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --flights 20              # Simulate 20 random flights
  %(prog)s --conflict                # Create head-on conflict
  %(prog)s --crossing                # Create crossing conflict
  %(prog)s --flights 10 --conflict   # 10 flights + conflict scenario
        """
    )

    parser.add_argument(
        "--host", default="localhost",
        help="Backend host (default: localhost)"
    )
    parser.add_argument(
        "--port", type=int, default=5000,
        help="Backend UDP port (default: 5000)"
    )
    parser.add_argument(
        "--flights", type=int, default=10,
        help="Number of random flights (default: 10)"
    )
    parser.add_argument(
        "--rate", type=float, default=1.0,
        help="Updates per second (default: 1.0)"
    )
    parser.add_argument(
        "--duration", type=float, default=None,
        help="Run for N seconds (default: indefinite)"
    )
    parser.add_argument(
        "--conflict", action="store_true",
        help="Add head-on conflict scenario"
    )
    parser.add_argument(
        "--crossing", action="store_true",
        help="Add crossing conflict scenario"
    )
    parser.add_argument(
        "--seed", type=int, default=None,
        help="Random seed for reproducibility"
    )

    args = parser.parse_args()

    # Set random seed if provided
    if args.seed is not None:
        random.seed(args.seed)
        print(f"Using random seed: {args.seed}")

    # Create simulator
    sim = FlightSimulator(
        host=args.host,
        port=args.port,
        update_rate=args.rate
    )

    # Add flights
    if args.flights > 0:
        sim.create_random_flights(args.flights)

    if args.conflict:
        sim.create_conflict_scenario()

    if args.crossing:
        sim.create_crossing_conflict()

    if not sim.flights:
        print("No flights to simulate! Use --flights, --conflict, or --crossing")
        return

    # Run simulation
    sim.run(duration=args.duration)


if __name__ == "__main__":
    main()
