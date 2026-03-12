#!/usr/bin/env python3
"""
Predefined Conflict Scenarios

Various scenarios for testing and demonstrating conflict detection.
Each scenario is designed to test specific aspects of the system.

For interview: "I created these scenarios to systematically test edge cases:
head-on conflicts, crossing paths, near-vertical conflicts, and resolution
maneuvers. This helped validate the CPA calculation under different conditions."
"""

from dataclasses import dataclass
from typing import List, Callable
from flight_simulator import Flight, FlightSimulator, FlightPhase


@dataclass
class Scenario:
    """Defines a test scenario."""
    name: str
    description: str
    setup: Callable[[FlightSimulator], None]


def setup_head_on(sim: FlightSimulator) -> None:
    """
    Head-On Conflict

    Two aircraft at same altitude, flying directly toward each other.
    This is the most dangerous type of conflict - closure rate is ~900 knots.
    """
    # Aircraft 1: Heading east at FL350
    sim.add_flight(Flight(
        flight_id=101,
        latitude=34.0,
        longitude=-118.5,
        altitude=35000,
        heading=90,
        speed=450
    ))

    # Aircraft 2: Heading west at FL350
    sim.add_flight(Flight(
        flight_id=102,
        latitude=34.0,
        longitude=-117.8,
        altitude=35000,
        heading=270,
        speed=450
    ))


def setup_crossing_90(sim: FlightSimulator) -> None:
    """
    90-Degree Crossing

    Two aircraft crossing at right angles.
    Tests geometric CPA calculation accuracy.
    """
    sim.add_flight(Flight(
        flight_id=201,
        latitude=33.8,
        longitude=-118.2,
        altitude=33000,
        heading=0,  # North
        speed=400
    ))

    sim.add_flight(Flight(
        flight_id=202,
        latitude=34.0,
        longitude=-118.4,
        altitude=33000,
        heading=90,  # East
        speed=400
    ))


def setup_crossing_45(sim: FlightSimulator) -> None:
    """
    45-Degree Crossing

    Oblique crossing - slower closure than head-on.
    """
    sim.add_flight(Flight(
        flight_id=301,
        latitude=33.9,
        longitude=-118.3,
        altitude=37000,
        heading=45,  # Northeast
        speed=480
    ))

    sim.add_flight(Flight(
        flight_id=302,
        latitude=34.1,
        longitude=-118.1,
        altitude=37000,
        heading=225,  # Southwest
        speed=480
    ))


def setup_overtake(sim: FlightSimulator) -> None:
    """
    Overtake Scenario

    Faster aircraft catching up to slower one.
    Low closure rate - conflict develops slowly.
    """
    # Slow aircraft ahead
    sim.add_flight(Flight(
        flight_id=401,
        latitude=34.0,
        longitude=-118.2,
        altitude=31000,
        heading=90,
        speed=280  # Slower (turboprop?)
    ))

    # Fast aircraft behind
    sim.add_flight(Flight(
        flight_id=402,
        latitude=34.0,
        longitude=-118.35,
        altitude=31000,
        heading=90,
        speed=500  # Faster jet
    ))


def setup_vertical_conflict(sim: FlightSimulator) -> None:
    """
    Vertical Conflict

    Aircraft climbing through another's altitude.
    Tests vertical separation logic.
    """
    # Level aircraft at FL350
    level = Flight(
        flight_id=501,
        latitude=34.0,
        longitude=-118.2,
        altitude=35000,
        heading=90,
        speed=450
    )
    sim.add_flight(level)

    # Climbing aircraft
    climbing = Flight(
        flight_id=502,
        latitude=34.0,
        longitude=-118.25,
        altitude=33000,
        heading=90,
        speed=420
    )
    climbing.start_climb(37000, rate=2500)
    sim.add_flight(climbing)


def setup_near_miss(sim: FlightSimulator) -> None:
    """
    Near Miss - Just Inside Separation

    Aircraft that will pass at exactly 4.5nm horizontal
    (inside 5nm minimum) but with 800ft vertical (inside 1000ft).
    Should trigger WARNING, then CRITICAL.
    """
    sim.add_flight(Flight(
        flight_id=601,
        latitude=34.0,
        longitude=-118.3,
        altitude=35000,
        heading=45,
        speed=450
    ))

    # Offset to pass at ~4.5nm
    sim.add_flight(Flight(
        flight_id=602,
        latitude=34.05,
        longitude=-118.0,
        altitude=35800,  # 800ft higher
        heading=270,
        speed=450
    ))


def setup_resolution_success(sim: FlightSimulator) -> None:
    """
    Resolution Scenario

    Conflict that gets resolved when one aircraft descends.
    Demonstrates the system detecting conflict then resolution.
    """
    # This flight will continue level
    sim.add_flight(Flight(
        flight_id=701,
        latitude=34.0,
        longitude=-118.4,
        altitude=35000,
        heading=90,
        speed=450
    ))

    # This flight will descend after a delay
    descending = Flight(
        flight_id=702,
        latitude=34.0,
        longitude=-117.9,
        altitude=35000,
        heading=270,
        speed=450
    )
    sim.add_flight(descending)

    # Note: To simulate resolution, external code would need to
    # call descending.start_descend(33000) after ~10 seconds


def setup_multiple_conflicts(sim: FlightSimulator) -> None:
    """
    Multiple Simultaneous Conflicts

    Three aircraft in a triangular pattern, all conflicting.
    Tests system handling of multiple alerts.
    """
    # Aircraft 1 - heading east
    sim.add_flight(Flight(
        flight_id=801,
        latitude=34.0,
        longitude=-118.4,
        altitude=35000,
        heading=90,
        speed=450
    ))

    # Aircraft 2 - heading southwest
    sim.add_flight(Flight(
        flight_id=802,
        latitude=34.1,
        longitude=-118.0,
        altitude=35000,
        heading=225,
        speed=450
    ))

    # Aircraft 3 - heading northwest
    sim.add_flight(Flight(
        flight_id=803,
        latitude=33.9,
        longitude=-118.0,
        altitude=35000,
        heading=315,
        speed=450
    ))


def setup_busy_airspace(sim: FlightSimulator) -> None:
    """
    Busy Airspace

    20 aircraft with random but safe paths, plus one conflict pair.
    Tests system performance under load.
    """
    # Add random safe flights first
    sim.create_random_flights(18)

    # Add one conflict pair
    sim.add_flight(Flight(
        flight_id=9001,
        latitude=34.0,
        longitude=-118.3,
        altitude=35000,
        heading=90,
        speed=450
    ))

    sim.add_flight(Flight(
        flight_id=9002,
        latitude=34.0,
        longitude=-118.0,
        altitude=35000,
        heading=270,
        speed=450
    ))


# ============================================
# Scenario Registry
# ============================================

SCENARIOS = {
    "head-on": Scenario(
        name="Head-On Conflict",
        description="Two aircraft flying directly toward each other at same altitude",
        setup=setup_head_on
    ),
    "crossing-90": Scenario(
        name="90-Degree Crossing",
        description="Two aircraft crossing at right angles",
        setup=setup_crossing_90
    ),
    "crossing-45": Scenario(
        name="45-Degree Crossing",
        description="Two aircraft crossing at oblique angle",
        setup=setup_crossing_45
    ),
    "overtake": Scenario(
        name="Overtake",
        description="Faster aircraft catching slower one from behind",
        setup=setup_overtake
    ),
    "vertical": Scenario(
        name="Vertical Conflict",
        description="Aircraft climbing through another's altitude",
        setup=setup_vertical_conflict
    ),
    "near-miss": Scenario(
        name="Near Miss",
        description="Aircraft passing just inside separation minimums",
        setup=setup_near_miss
    ),
    "resolution": Scenario(
        name="Resolution",
        description="Conflict that gets resolved by descent",
        setup=setup_resolution_success
    ),
    "multiple": Scenario(
        name="Multiple Conflicts",
        description="Three aircraft all conflicting with each other",
        setup=setup_multiple_conflicts
    ),
    "busy": Scenario(
        name="Busy Airspace",
        description="20 aircraft including one conflict pair",
        setup=setup_busy_airspace
    ),
}


def list_scenarios() -> None:
    """Print available scenarios."""
    print("\nAvailable Scenarios:")
    print("-" * 60)
    for key, scenario in SCENARIOS.items():
        print(f"  {key:15s} - {scenario.description}")
    print()


def run_scenario(name: str, host: str = "localhost", port: int = 5000,
                 duration: float = None) -> None:
    """Run a named scenario."""
    if name not in SCENARIOS:
        print(f"Unknown scenario: {name}")
        list_scenarios()
        return

    scenario = SCENARIOS[name]
    print(f"\n=== {scenario.name} ===")
    print(scenario.description)
    print()

    sim = FlightSimulator(host=host, port=port)
    scenario.setup(sim)
    sim.run(duration=duration)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Run conflict scenarios")
    parser.add_argument("scenario", nargs="?", help="Scenario name")
    parser.add_argument("--list", "-l", action="store_true", help="List scenarios")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--duration", type=float, default=None)

    args = parser.parse_args()

    if args.list or not args.scenario:
        list_scenarios()
    else:
        run_scenario(args.scenario, args.host, args.port, args.duration)
