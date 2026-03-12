/**
 * MetricsBar - System Status Display
 *
 * Shows real-time system metrics:
 * - Connection status
 * - Active flights count
 * - Packets per second
 * - Worker utilization
 * - Uptime
 */

import { useState, useEffect } from 'react';
import { useFlightStore } from '../stores/flightStore';
import './MetricsBar.css';

interface Props {
  connected: boolean;
}

export function MetricsBar({ connected }: Props) {
  const metrics = useFlightStore((state) => state.metrics);
  const [utcTime, setUtcTime] = useState(new Date());

  // Update UTC clock every second
  useEffect(() => {
    const interval = setInterval(() => {
      setUtcTime(new Date());
    }, 1000);
    return () => clearInterval(interval);
  }, []);

  const formatUptime = (seconds: number): string => {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);

    if (h > 0) {
      return `${h}h ${m}m ${s}s`;
    } else if (m > 0) {
      return `${m}m ${s}s`;
    } else {
      return `${s}s`;
    }
  };

  const formatUtc = (date: Date): string => {
    return date.toISOString().substring(11, 19) + 'Z';
  };

  return (
    <div className="metrics-bar">
      <div className="metrics-left">
        <div className={`connection-status ${connected ? 'connected' : 'disconnected'}`}>
          <span className="status-dot" />
          {connected ? 'ONLINE' : 'OFFLINE'}
        </div>

        <div className="metric utc-clock">
          <span className="metric-label">UTC</span>
          <span className="metric-value">{formatUtc(utcTime)}</span>
        </div>
      </div>

      <div className="metrics-center">
        <h1 className="title">SKYGUARD</h1>
        <span className="subtitle">ATC Conflict Detection System</span>
      </div>

      <div className="metrics-right">
        {metrics && (
          <>
            <div className="metric">
              <span className="metric-value">{metrics.activeFlights}</span>
              <span className="metric-label">Flights</span>
            </div>

            <div className="metric">
              <span className="metric-value">{metrics.packetsPerSecond}</span>
              <span className="metric-label">Pkts/s</span>
            </div>

            <div className="metric">
              <span className="metric-value">
                {metrics.workersBusy}/{metrics.workersTotal}
              </span>
              <span className="metric-label">Workers</span>
            </div>

            <div className="metric">
              <span className="metric-value">{formatUptime(metrics.uptime)}</span>
              <span className="metric-label">Uptime</span>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
