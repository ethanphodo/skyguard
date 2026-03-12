/**
 * AlertsPanel - Conflict Alert Display
 *
 * Shows active conflict alerts with:
 * - Severity indicator (WARNING/CRITICAL)
 * - Time to CPA countdown
 * - Minimum separation
 * - Recommended action
 */

import { useFlightStore } from '../stores/flightStore';
import type { ConflictAlert } from '../types';
import './AlertsPanel.css';

export function AlertsPanel() {
  const alerts = useFlightStore((state) => state.getAlertsArray());
  const selectFlight = useFlightStore((state) => state.selectFlight);

  // Sort by severity (critical first) then by time to CPA
  const sortedAlerts = [...alerts].sort((a, b) => {
    if (a.severity !== b.severity) {
      return a.severity === 'critical' ? -1 : 1;
    }
    return a.timeToCpa - b.timeToCpa;
  });

  return (
    <div className="alerts-panel">
      <h3>
        Conflict Alerts
        {alerts.length > 0 && (
          <span className="alert-count">{alerts.length}</span>
        )}
      </h3>

      {sortedAlerts.length === 0 ? (
        <div className="no-alerts">
          <span className="checkmark">✓</span>
          No active conflicts
        </div>
      ) : (
        <div className="alerts-list">
          {sortedAlerts.map((alert) => (
            <AlertCard
              key={`${alert.flight1}-${alert.flight2}`}
              alert={alert}
              onFlightClick={selectFlight}
            />
          ))}
        </div>
      )}
    </div>
  );
}

interface AlertCardProps {
  alert: ConflictAlert;
  onFlightClick: (id: number) => void;
}

function AlertCard({ alert, onFlightClick }: AlertCardProps) {
  const isCritical = alert.severity === 'critical';

  return (
    <div className={`alert-card ${isCritical ? 'critical' : 'warning'}`}>
      <div className="alert-header">
        <span className={`severity-badge ${alert.severity}`}>
          {alert.severity.toUpperCase()}
        </span>
        <span className="cpa-time">
          CPA in {Math.round(alert.timeToCpa)}s
        </span>
      </div>

      <div className="alert-flights">
        <button
          className="flight-button"
          onClick={() => onFlightClick(alert.flight1)}
        >
          {alert.flight1}
        </button>
        <span className="vs">vs</span>
        <button
          className="flight-button"
          onClick={() => onFlightClick(alert.flight2)}
        >
          {alert.flight2}
        </button>
      </div>

      <div className="alert-details">
        <div className="detail-row">
          <span className="label">Min Sep:</span>
          <span className="value">{alert.minSeparation.toFixed(1)} nm</span>
        </div>
        <div className="detail-row">
          <span className="label">Alt Diff:</span>
          <span className="value">{Math.round(alert.altDiff)} ft</span>
        </div>
      </div>

      {alert.recommendation && (
        <div className="recommendation">
          <span className="rec-label">REC:</span>
          {alert.recommendation}
        </div>
      )}
    </div>
  );
}
