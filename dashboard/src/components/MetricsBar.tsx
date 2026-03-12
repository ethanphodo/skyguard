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

import { useFlightStore } from '../stores/flightStore';
import './MetricsBar.css';

interface Props {
  connected: boolean;
}

export function MetricsBar({ connected }: Props) {
  const metrics = useFlightStore((state) => state.metrics);
  const lastUpdate = useFlightStore((state) => state.connection.lastUpdate);

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

  const formatTime = (date: Date | null): string => {
    if (!date) return '--:--:--';
    return date.toLocaleTimeString();
  };

  return (
    <div className="metrics-bar">
      <div className="metrics-left">
        <div className={`connection-status ${connected ? 'connected' : 'disconnected'}`}>
          <span className="status-dot" />
          {connected ? 'Connected' : 'Disconnected'}
        </div>

        <div className="metric">
          <span className="metric-label">Last Update:</span>
          <span className="metric-value">{formatTime(lastUpdate)}</span>
        </div>
      </div>

      <div className="metrics-center">
        <h1 className="title">SkyGuard</h1>
        <span className="subtitle">Flight Monitoring System</span>
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
