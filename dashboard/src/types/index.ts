/**
 * SkyGuard Dashboard Types
 *
 * Matches the JSON structures sent by the C++ WebSocket server.
 */

export interface Flight {
  id: number;
  lat: number;
  lon: number;
  alt: number;
  hdg: number;
  spd: number;
}

export interface ConflictAlert {
  flight1: number;
  flight2: number;
  timeToCpa: number;
  minSeparation: number;
  altDiff: number;
  severity: 'warning' | 'critical';
  recommendation: string;
}

export interface SystemMetrics {
  uptime: number;
  activeFlights: number;
  activeConflicts: number;
  packetsPerSecond: number;
  queueDepth: number;
  workersBusy: number;
  workersTotal: number;
}

export interface ConflictResolution {
  flight1: number;
  flight2: number;
}

export type WebSocketMessage =
  | { type: 'flights'; data: Flight[] }
  | { type: 'alert'; data: ConflictAlert }
  | { type: 'resolved'; data: ConflictResolution }
  | { type: 'metrics'; data: SystemMetrics };

export interface ConnectionStatus {
  connected: boolean;
  lastUpdate: Date | null;
  reconnectAttempts: number;
}
