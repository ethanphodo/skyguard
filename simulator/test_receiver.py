#!/usr/bin/env python3
"""
Test Receiver - Verify Packet Format

Simple UDP receiver to verify the simulator is sending correctly formatted
packets. Useful for testing without the C++ backend.

For interview: "I built this test receiver to validate the binary protocol
independently. It helped me catch endianness issues early before integrating
with the C++ backend."

Usage:
    python test_receiver.py --port 5000
"""

import socket
import struct
import argparse
from datetime import datetime


PACKET_SIZE = 32


def decode_packet(data: bytes) -> dict:
    """
    Decode a flight packet.

    Format (32 bytes, network byte order):
    - uint32: flight_id
    - uint32: timestamp
    - double: latitude
    - double: longitude
    - double: altitude
    """
    if len(data) != PACKET_SIZE:
        raise ValueError(f"Invalid packet size: {len(data)}, expected {PACKET_SIZE}")

    # Unpack header
    flight_id, timestamp = struct.unpack("!II", data[0:8])

    # Unpack doubles
    latitude = struct.unpack("!d", data[8:16])[0]
    longitude = struct.unpack("!d", data[16:24])[0]
    altitude = struct.unpack("!d", data[24:32])[0]

    return {
        "flight_id": flight_id,
        "timestamp": timestamp,
        "latitude": latitude,
        "longitude": longitude,
        "altitude": altitude,
        "time_str": datetime.fromtimestamp(timestamp).strftime("%H:%M:%S")
    }


def main():
    parser = argparse.ArgumentParser(description="Test UDP packet receiver")
    parser.add_argument("--port", type=int, default=5000, help="UDP port")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show all packets")
    args = parser.parse_args()

    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", args.port))
    sock.settimeout(1.0)

    print(f"Listening on port {args.port}...")
    print("Press Ctrl+C to stop\n")

    packet_count = 0
    flight_ids = set()
    last_status = datetime.now()

    try:
        while True:
            try:
                data, addr = sock.recvfrom(1024)
                packet = decode_packet(data)
                packet_count += 1
                flight_ids.add(packet["flight_id"])

                if args.verbose:
                    print(f"[{packet['time_str']}] Flight {packet['flight_id']:5d}: "
                          f"({packet['latitude']:8.4f}, {packet['longitude']:9.4f}) "
                          f"alt={packet['altitude']:6.0f}ft")

                # Status every 5 seconds
                now = datetime.now()
                if (now - last_status).seconds >= 5:
                    print(f"\n--- Status: {packet_count} packets from "
                          f"{len(flight_ids)} flights ---\n")
                    last_status = now

            except socket.timeout:
                continue

    except KeyboardInterrupt:
        print(f"\n\nReceived {packet_count} packets from {len(flight_ids)} unique flights")
        print("Flight IDs:", sorted(flight_ids))


if __name__ == "__main__":
    main()
