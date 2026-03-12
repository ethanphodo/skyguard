/**
 * Flight Store - Zustand State Management
 *
 * Manages all flight data, alerts, and metrics received from WebSocket.
 *
 * For interview: "I used Zustand for state management - it's simpler than
 * Redux but still provides the reactivity I need. The store updates trigger
 * re-renders only in components that subscribe to changed slices."
 */

import { create } from 'zustand';
import type { Flight, ConflictAlert, SystemMetrics, ConnectionStatus } from '../types';

interface FlightState {
  // Data
  flights: Map<number, Flight>;
  alerts: Map<string, ConflictAlert>;
  metrics: SystemMetrics | null;
  connection: ConnectionStatus;

  // Selected state
  selectedFlightId: number | null;

  // Actions
  setFlights: (flights: Flight[]) => void;
  addAlert: (alert: ConflictAlert) => void;
  removeAlert: (flight1: number, flight2: number) => void;
  setMetrics: (metrics: SystemMetrics) => void;
  setConnected: (connected: boolean) => void;
  incrementReconnectAttempts: () => void;
  resetReconnectAttempts: () => void;
  selectFlight: (id: number | null) => void;

  // Computed
  getFlightsArray: () => Flight[];
  getAlertsArray: () => ConflictAlert[];
  getFlightById: (id: number) => Flight | undefined;
  isFlightInConflict: (id: number) => boolean;
}

// Helper to create alert key
const alertKey = (f1: number, f2: number): string => {
  const [a, b] = f1 < f2 ? [f1, f2] : [f2, f1];
  return `${a}-${b}`;
};

export const useFlightStore = create<FlightState>((set, get) => ({
  // Initial state
  flights: new Map(),
  alerts: new Map(),
  metrics: null,
  connection: {
    connected: false,
    lastUpdate: null,
    reconnectAttempts: 0,
  },
  selectedFlightId: null,

  // Actions
  setFlights: (flights) => {
    const map = new Map<number, Flight>();
    flights.forEach((f) => map.set(f.id, f));
    set({
      flights: map,
      connection: {
        ...get().connection,
        lastUpdate: new Date(),
      },
    });
  },

  addAlert: (alert) => {
    const key = alertKey(alert.flight1, alert.flight2);
    set((state) => {
      const newAlerts = new Map(state.alerts);
      newAlerts.set(key, alert);
      return { alerts: newAlerts };
    });
  },

  removeAlert: (flight1, flight2) => {
    const key = alertKey(flight1, flight2);
    set((state) => {
      const newAlerts = new Map(state.alerts);
      newAlerts.delete(key);
      return { alerts: newAlerts };
    });
  },

  setMetrics: (metrics) => {
    set({ metrics });
  },

  setConnected: (connected) => {
    set((state) => ({
      connection: {
        ...state.connection,
        connected,
      },
    }));
  },

  incrementReconnectAttempts: () => {
    set((state) => ({
      connection: {
        ...state.connection,
        reconnectAttempts: state.connection.reconnectAttempts + 1,
      },
    }));
  },

  resetReconnectAttempts: () => {
    set((state) => ({
      connection: {
        ...state.connection,
        reconnectAttempts: 0,
      },
    }));
  },

  selectFlight: (id) => {
    set({ selectedFlightId: id });
  },

  // Computed getters
  getFlightsArray: () => Array.from(get().flights.values()),

  getAlertsArray: () => Array.from(get().alerts.values()),

  getFlightById: (id) => get().flights.get(id),

  isFlightInConflict: (id) => {
    const alerts = get().alerts;
    for (const alert of alerts.values()) {
      if (alert.flight1 === id || alert.flight2 === id) {
        return true;
      }
    }
    return false;
  },
}));
