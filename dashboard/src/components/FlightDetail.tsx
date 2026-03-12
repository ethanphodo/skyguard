/**
 * FlightDetail - Selected Flight Information
 *
 * Shows detailed information about the currently selected flight.
 */

import { useFlightStore } from '../stores/flightStore';
import './FlightDetail.css';

export function FlightDetail() {
  const selectedId = useFlightStore((state) => state.selectedFlightId);
  const flight = useFlightStore((state) =>
    selectedId ? state.getFlightById(selectedId) : null
  );
  const isInConflict = useFlightStore((state) =>
    selectedId ? state.isFlightInConflict(selectedId) : false
  );
  const selectFlight = useFlightStore((state) => state.selectFlight);

  if (!flight) {
    return (
      <div className="flight-detail empty">
        <span className="hint">Click an aircraft to view details</span>
      </div>
    );
  }

  const flightLevel = Math.round(flight.alt / 100);

  return (
    <div className={`flight-detail ${isInConflict ? 'conflict' : ''}`}>
      <div className="detail-header">
        <h2>Flight {flight.id}</h2>
        <button className="close-btn" onClick={() => selectFlight(null)}>
          ×
        </button>
      </div>

      {isInConflict && (
        <div className="conflict-warning">
          IN CONFLICT
        </div>
      )}

      <div className="detail-grid">
        <div className="detail-item">
          <span className="label">Altitude</span>
          <span className="value">FL{flightLevel.toString().padStart(3, '0')}</span>
          <span className="unit">{Math.round(flight.alt).toLocaleString()} ft</span>
        </div>

        <div className="detail-item">
          <span className="label">Heading</span>
          <span className="value">{Math.round(flight.hdg).toString().padStart(3, '0')}°</span>
          <span className="unit">{getCardinalDirection(flight.hdg)}</span>
        </div>

        <div className="detail-item">
          <span className="label">Ground Speed</span>
          <span className="value">{Math.round(flight.spd)}</span>
          <span className="unit">knots</span>
        </div>

        <div className="detail-item">
          <span className="label">Position</span>
          <span className="value coords">
            {flight.lat.toFixed(4)}°
            <br />
            {flight.lon.toFixed(4)}°
          </span>
        </div>
      </div>

      <div className="compass">
        <div
          className="compass-arrow"
          style={{ transform: `rotate(${flight.hdg}deg)` }}
        >
          ▲
        </div>
        <div className="compass-ring">
          <span className="n">N</span>
          <span className="e">E</span>
          <span className="s">S</span>
          <span className="w">W</span>
        </div>
      </div>
    </div>
  );
}

function getCardinalDirection(heading: number): string {
  const directions = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
  const index = Math.round(heading / 45) % 8;
  return directions[index];
}
