/**
 * Demo Mode - Simulated Flight Data
 *
 * Generates simulated flight data when the backend isn't connected.
 * Useful for demonstrating the dashboard UI without running the full system.
 *
 * For interview: "I added a demo mode so I can show the dashboard working
 * independently. It generates realistic flight paths with occasional
 * conflicts, demonstrating all the UI features."
 */

import { useEffect, useRef, useCallback } from 'react';
import { useFlightStore } from '../stores/flightStore';
import type { Flight, ConflictAlert } from '../types';

// Demo area bounds
const LAT_MIN = 33.5;
const LAT_MAX = 34.5;
const LON_MIN = -118.8;
const LON_MAX = -117.5;

interface DemoFlight extends Flight {
  vx: number;  // Velocity in lon degrees per second
  vy: number;  // Velocity in lat degrees per second
}

export function useDemoMode(enabled: boolean = true) {
  const flightsRef = useRef<DemoFlight[]>([]);
  const intervalRef = useRef<number | null>(null);
  const startTimeRef = useRef<number>(Date.now());

  const {
    setFlights,
    addAlert,
    removeAlert,
    setMetrics,
    setConnected,
    connection,
  } = useFlightStore();

  // Initialize demo flights
  const initFlights = useCallback(() => {
    const flights: DemoFlight[] = [];

    for (let i = 0; i < 15; i++) {
      const heading = Math.random() * 360;
      const speed = 250 + Math.random() * 300; // 250-550 knots
      const speedMps = speed * 0.514444;

      // Convert to degrees per second
      const latMeters = 111132;
      const lonMeters = latMeters * Math.cos((LAT_MIN + LAT_MAX) / 2 * Math.PI / 180);

      const vx = Math.sin(heading * Math.PI / 180) * speedMps / lonMeters;
      const vy = Math.cos(heading * Math.PI / 180) * speedMps / latMeters;

      flights.push({
        id: 1000 + i,
        lat: LAT_MIN + Math.random() * (LAT_MAX - LAT_MIN),
        lon: LON_MIN + Math.random() * (LON_MAX - LON_MIN),
        alt: (Math.floor(Math.random() * 30) + 10) * 1000, // FL100-FL400
        hdg: heading,
        spd: speed,
        vx,
        vy,
      });
    }

    // Add conflict pair
    const conflictLat = (LAT_MIN + LAT_MAX) / 2;
    const conflictLon = (LON_MIN + LON_MAX) / 2;

    flights.push({
      id: 9001,
      lat: conflictLat,
      lon: conflictLon - 0.1,
      alt: 35000,
      hdg: 90,
      spd: 450,
      vx: 0.00005,
      vy: 0,
    });

    flights.push({
      id: 9002,
      lat: conflictLat + 0.01,
      lon: conflictLon + 0.1,
      alt: 35000,
      hdg: 270,
      spd: 450,
      vx: -0.00005,
      vy: 0,
    });

    flightsRef.current = flights;
    startTimeRef.current = Date.now();
  }, []);

  // Update flight positions
  const updateFlights = useCallback(() => {
    const flights = flightsRef.current;
    const dt = 1; // 1 second update

    flights.forEach((flight) => {
      // Update position
      flight.lon += flight.vx * dt;
      flight.lat += flight.vy * dt;

      // Wrap around
      if (flight.lat < LAT_MIN) flight.lat = LAT_MAX;
      if (flight.lat > LAT_MAX) flight.lat = LAT_MIN;
      if (flight.lon < LON_MIN) flight.lon = LON_MAX;
      if (flight.lon > LON_MAX) flight.lon = LON_MIN;

      // Random heading changes
      if (Math.random() < 0.02) {
        const delta = (Math.random() - 0.5) * 20;
        flight.hdg = (flight.hdg + delta + 360) % 360;

        const speedMps = flight.spd * 0.514444;
        const latMeters = 111132;
        const lonMeters = latMeters * Math.cos(flight.lat * Math.PI / 180);

        flight.vx = Math.sin(flight.hdg * Math.PI / 180) * speedMps / lonMeters;
        flight.vy = Math.cos(flight.hdg * Math.PI / 180) * speedMps / latMeters;
      }
    });

    // Convert to Flight array
    const flightData: Flight[] = flights.map((f) => ({
      id: f.id,
      lat: f.lat,
      lon: f.lon,
      alt: f.alt,
      hdg: f.hdg,
      spd: f.spd,
    }));

    setFlights(flightData);

    // Check for conflict between 9001 and 9002
    const f1 = flights.find((f) => f.id === 9001);
    const f2 = flights.find((f) => f.id === 9002);

    if (f1 && f2) {
      const dist = Math.sqrt(
        Math.pow((f1.lat - f2.lat) * 111132, 2) +
        Math.pow((f1.lon - f2.lon) * 111132 * Math.cos(f1.lat * Math.PI / 180), 2)
      );
      const distNm = dist / 1852;

      if (distNm < 10) {
        const alert: ConflictAlert = {
          flight1: 9001,
          flight2: 9002,
          timeToCpa: Math.max(5, distNm * 3),
          minSeparation: Math.max(0.5, distNm - 2),
          altDiff: 0,
          severity: distNm < 5 ? 'critical' : 'warning',
          recommendation: 'FL9002 descend to FL330',
        };
        addAlert(alert);
      } else {
        removeAlert(9001, 9002);
      }
    }

    // Update metrics
    const uptime = (Date.now() - startTimeRef.current) / 1000;
    setMetrics({
      uptime,
      activeFlights: flights.length,
      activeConflicts: 1,
      packetsPerSecond: flights.length,
      queueDepth: Math.floor(Math.random() * 50),
      workersBusy: Math.floor(Math.random() * 3) + 1,
      workersTotal: 4,
    });
  }, [setFlights, addAlert, removeAlert, setMetrics]);

  // Start/stop demo mode
  useEffect(() => {
    // Only run demo mode if not connected
    if (enabled && !connection.connected) {
      initFlights();
      setConnected(true); // Fake connection for demo

      intervalRef.current = window.setInterval(updateFlights, 1000);

      return () => {
        if (intervalRef.current) {
          clearInterval(intervalRef.current);
        }
      };
    }
  }, [enabled, connection.connected, initFlights, updateFlights, setConnected]);
}
